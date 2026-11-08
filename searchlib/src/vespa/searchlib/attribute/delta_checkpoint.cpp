// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "delta_checkpoint.h"

#include "iattributesavetarget.h"

#include <vespa/check_require.h>
#include <vespa/searchlib/util/file_settings.h>
#include <vespa/vespalib/data/databuffer.h>
#include <vespa/vespalib/data/fileheader.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <random>
#include <unistd.h>

#include <vespa/log/log.h>
LOG_SETUP(".searchlib.attribute.delta_checkpoint");

using vespalib::GenerationGuard;

namespace search::attribute {

namespace {

uint64_t env_or(const char* name, uint64_t fallback) {
    const char* value = getenv(name);
    return (value != nullptr && *value != '\0') ? std::strtoull(value, nullptr, 10) : fallback;
}

} // namespace

const std::string DeltaCheckpoints::checkpoint_id_tag = "delta.checkpoint_id";
const std::string DeltaCheckpoints::delta_entries_tag = "delta.entries";
const std::string DeltaCheckpoints::base_suffix = ".base.dat";

bool DeltaCheckpoints::enabled() {
    return env_or("VESPA_DELTA_ATTRIBUTE_CHECKPOINTS", 0) != 0;
}

double DeltaCheckpoints::full_ratio() {
    return env_or("VESPA_DELTA_ATTRIBUTE_FULL_PERCENT", 10) / 100.0;
}

uint64_t DeltaCheckpoints::make_checkpoint_id() {
    // Positive as a signed header integer, never 0.
    static std::mutex lock;
    static std::mt19937_64 generator(std::random_device{}());
    std::lock_guard guard(lock);
    return (generator() >> 2) | 1;
}

bool DeltaCheckpoints::is_delta(const vespalib::GenericHeader& header) {
    return header.hasTag(delta_entries_tag);
}

DeltaCheckpoints::Base DeltaCheckpoints::base() const {
    std::lock_guard guard(_lock);
    return _base;
}

void DeltaCheckpoints::set_base(Base base) {
    std::lock_guard guard(_lock);
    _base = std::move(base);
}

void DeltaCheckpoints::clear_base() {
    set_base(Base());
}

std::unique_ptr<AttributeSaver> DeltaCheckpointSaver::full(const AttributeHeader& header,
                                                           std::shared_ptr<DeltaCheckpoints> state, uint64_t id,
                                                           const void* data, size_t size) {
    AttributeHeader tagged(header);
    tagged.get_extra_tags().putTag(vespalib::GenericHeader::Tag(DeltaCheckpoints::checkpoint_id_tag, id));
    DeltaCheckpoints::Base becomes{header.getFileName() + ".dat", id};
    std::unique_ptr<DeltaCheckpointSaver> saver(
        new DeltaCheckpointSaver(tagged, std::move(state), std::move(becomes), false, size));
    if (size > 0) {
        memcpy(saver->_buf->getFree(), data, size);
        saver->_buf->moveFreeToData(size);
    }
    return saver;
}

std::unique_ptr<AttributeSaver> DeltaCheckpointSaver::delta(const AttributeHeader& header,
                                                            std::shared_ptr<DeltaCheckpoints> state,
                                                            DeltaCheckpoints::Base base, uint64_t entries,
                                                            std::initializer_list<std::span<const std::byte>> parts) {
    CHECK(base.id != 0);
    AttributeHeader tagged(header);
    tagged.get_extra_tags().putTag(vespalib::GenericHeader::Tag(DeltaCheckpoints::checkpoint_id_tag, base.id));
    tagged.get_extra_tags().putTag(vespalib::GenericHeader::Tag(DeltaCheckpoints::delta_entries_tag, entries));
    size_t size = 0;
    for (auto part : parts) {
        size += part.size();
    }
    std::unique_ptr<DeltaCheckpointSaver> saver(
        new DeltaCheckpointSaver(tagged, std::move(state), std::move(base), true, size));
    for (auto part : parts) {
        if (!part.empty()) {
            memcpy(saver->_buf->getFree(), part.data(), part.size());
            saver->_buf->moveFreeToData(part.size());
        }
    }
    return saver;
}

DeltaCheckpointSaver::DeltaCheckpointSaver(const AttributeHeader& header, std::shared_ptr<DeltaCheckpoints> state,
                                           DeltaCheckpoints::Base base, bool is_delta, size_t size)
    : AttributeSaver(GenerationGuard(), header),
      _state(std::move(state)),
      _base(std::move(base)),
      _is_delta(is_delta),
      _buf(),
      _tracker() {
    auto lock = _tracker.acquire_lock();
    _buf = std::make_unique<IAttributeFileWriter::BufferBuf>(size, FileSettings::DIRECTIO_ALIGNMENT);
    CHECK(_buf->getFreeLen() >= size);
    _tracker.set_transient_memory(std::move(lock), size);
}

DeltaCheckpointSaver::~DeltaCheckpointSaver() {
    if (_buf) {
        auto lock = _tracker.acquire_lock();
        _buf.reset();
        _tracker.set_transient_memory(std::move(lock), 0);
    }
}

bool DeltaCheckpointSaver::onSave(IAttributeSaveTarget& saveTarget) {
    if (_is_delta) {
        const std::string link_name = get_file_name() + DeltaCheckpoints::base_suffix;
        unlink(link_name.c_str()); // a leftover from an earlier failed save of the same snapshot
        if (link(_base.dat_file.c_str(), link_name.c_str()) != 0) {
            LOG(warning, "Unable to link delta checkpoint base '%s' to '%s': %s. The next checkpoint is full.",
                _base.dat_file.c_str(), link_name.c_str(), std::strerror(errno));
            _state->clear_base();
            return false;
        }
        saveTarget.datWriter().write_buf(std::move(_buf), std::move(_tracker));
        // Link the next delta to this snapshot's copy: older snapshots are deleted once this one is valid.
        _state->set_base({link_name, _base.id});
        return true;
    }
    saveTarget.datWriter().write_buf(std::move(_buf), std::move(_tracker));
    _state->set_base(_base);
    return true;
}

} // namespace search::attribute
