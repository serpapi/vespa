// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "attributesaver.h"
#include "iattributefilewriter.h"

#include <vespa/vespalib/util/transient_memory_tracker.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <initializer_list>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace vespalib {
class GenericHeader;
}

namespace search::attribute {

/*
 * Delta checkpoints for fixed-width attributes (prototype, enabled with VESPA_DELTA_ATTRIBUTE_CHECKPOINTS=1).
 *
 * A full checkpoint is the normal .dat file with a checkpoint id in its header. A delta checkpoint is a
 * snapshot whose <name>.base.dat is a hard link to the last full .dat, and whose <name>.dat holds the entries
 * (lid, value) of every lid changed since that full checkpoint, so a checkpoint writes O(changes) instead of
 * O(docs). Every snapshot directory is self-contained: deleting older snapshots never breaks a newer one.
 * A full checkpoint is written again when the delta reaches full_ratio() of the full size.
 */
class DeltaCheckpoints {
public:
    // Header tag of a full .dat: its own checkpoint id. Of a delta .dat: the id of its base.
    static const std::string checkpoint_id_tag;
    // Header tag of a delta .dat only: the number of entries.
    static const std::string delta_entries_tag;
    static const std::string base_suffix; // ".base.dat"

    static bool enabled();
    static double full_ratio();
    static uint64_t make_checkpoint_id();
    static bool is_delta(const vespalib::GenericHeader& header);

    /*
     * The full checkpoint new deltas are based on: a path where its .dat is linked and its id. Shared between
     * the attribute writer thread (which chooses full or delta) and the flush thread (which learns whether a
     * save succeeded).
     */
    struct Base {
        std::string dat_file;
        uint64_t    id = 0; // 0: no usable base, the next checkpoint must be full
    };
    Base base() const;
    void set_base(Base base);
    void clear_base();

private:
    mutable std::mutex _lock;
    Base               _base;
};

/*
 * Saves a full or a delta checkpoint of a fixed-width attribute from data copied by the attribute writer
 * thread. A delta save fails if its base can't be linked; the next checkpoint is then full.
 */
class DeltaCheckpointSaver : public AttributeSaver {
public:
    using Buffer = IAttributeFileWriter::Buffer;

    // Full checkpoint of size bytes at data, with the checkpoint id in header.
    static std::unique_ptr<AttributeSaver> full(const AttributeHeader& header, std::shared_ptr<DeltaCheckpoints> state,
                                                uint64_t id, const void* data, size_t size);
    // Delta checkpoint of entries entries, written as the concatenation of parts. header may carry more tags.
    static std::unique_ptr<AttributeSaver> delta(const AttributeHeader& header, std::shared_ptr<DeltaCheckpoints> state,
                                                 DeltaCheckpoints::Base base, uint64_t entries,
                                                 std::initializer_list<std::span<const std::byte>> parts);
    ~DeltaCheckpointSaver() override;

private:
    DeltaCheckpointSaver(const AttributeHeader& header, std::shared_ptr<DeltaCheckpoints> state,
                         DeltaCheckpoints::Base base, bool is_delta, size_t size);
    bool onSave(IAttributeSaveTarget& saveTarget) override;

    std::shared_ptr<DeltaCheckpoints> _state;
    DeltaCheckpoints::Base            _base; // for a full save: the base it becomes
    bool                              _is_delta;
    Buffer                            _buf;
    vespalib::TransientMemoryTracker  _tracker;
};

} // namespace search::attribute
