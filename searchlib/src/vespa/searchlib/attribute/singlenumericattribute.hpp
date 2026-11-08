// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include "attributevector.hpp"
#include "load_utils.h"
#include "numeric_matcher.h"
#include "numeric_range_matcher.h"
#include "primitivereader.h"
#include "single_numeric_search_context.h"
#include "singlenumericattribute.h"
#include "singlenumericattributesaver.h"
#include "valuemodifier.h"

#include <vespa/check_require.h>
#include <vespa/fastos/file.h>
#include <vespa/searchcommon/attribute/config.h>
#include <vespa/searchlib/query/query_term_simple.h>
#include <vespa/searchlib/util/file_with_header.h>
#include <vespa/searchlib/util/fileutil.h>

#include <algorithm>

namespace search {

template <typename B>
SingleValueNumericAttribute<B>::SingleValueNumericAttribute(const std::string& baseFileName)
    : SingleValueNumericAttribute(
          baseFileName, attribute::Config(attribute::BasicType::fromType(T()), attribute::CollectionType::SINGLE)) {
}

template <typename B>
SingleValueNumericAttribute<B>::SingleValueNumericAttribute(const std::string&             baseFileName,
                                                            const AttributeVector::Config& c)
    : B(baseFileName, c), _data(c.getGrowStrategy(), getGenerationHolder(), this->get_initial_alloc()) {
}

template <typename B> SingleValueNumericAttribute<B>::~SingleValueNumericAttribute() {
    getGenerationHolder().reclaim_all();
}

template <typename B> void SingleValueNumericAttribute<B>::onCommit() {
    this->checkSetMaxValueCount(1);

    {
        // apply updates
        typename B::ValueModifier valueGuard(this->getValueModifier());
        for (const auto& change : this->_changes.getInsertOrder()) {
            if (_delta_checkpoints) {
                _changed_since_full_checkpoint.push_back(change._doc);
            }
            if (change._type == ChangeBase::UPDATE) {
                vespalib::atomic::store_ref_relaxed(_data[change._doc], change._data);
            } else if (change._type >= ChangeBase::ADD && change._type <= ChangeBase::DIV) {
                vespalib::atomic::store_ref_relaxed(
                    _data[change._doc], this->template applyArithmetic<T, typename B::Change::DataType>(
                                            _data[change._doc], change._data.getArithOperand(), change._type));
            } else if (change._type == ChangeBase::CLEARDOC) {
                vespalib::atomic::store_ref_relaxed(_data[change._doc], this->_defaultValue._data);
            }
        }
    }

    this->reclaim_unused_memory();

    this->_changes.clear();
}

template <typename B> void SingleValueNumericAttribute<B>::onUpdateStat(CommitParam::UpdateStats updateStats) {
    if (updateStats == CommitParam::UpdateStats::SIZES_ONLY) {
        this->updateSizes(_data.size(), _data.size());
    } else if (updateStats == CommitParam::UpdateStats::FORCE) {
        vespalib::MemoryUsage usage = _data.getMemoryUsage();
        usage.mergeGenerationHeldBytes(getGenerationHolder().get_held_bytes());
        usage.merge(this->getChangeVectorMemoryUsage());
        this->updateStatistics(_data.size(), _data.size(), usage.allocatedBytes(), usage.usedBytes(),
                               usage.deadBytes(), usage.allocatedBytesOnHold());
    }
}

template <typename B> void SingleValueNumericAttribute<B>::onAddDocs(DocId lidLimit) {
    _data.reserve(lidLimit);
}

template <typename B> bool SingleValueNumericAttribute<B>::addDoc(DocId& doc) {
    bool incGen = _data.isFull();
    _data.push_back(B::defaultValue());
    std::atomic_thread_fence(std::memory_order_release);
    B::incNumDocs();
    doc = B::getNumDocs() - 1;
    this->updateUncommittedDocIdLimit(doc);
    if (incGen) {
        this->incGeneration();
    } else {
        this->reclaim_unused_memory();
    }
    return true;
}

template <typename B> void SingleValueNumericAttribute<B>::reclaim_memory(vespalib::Generation oldest_used_gen) {
    getGenerationHolder().reclaim(oldest_used_gen);
}

template <typename B> void SingleValueNumericAttribute<B>::before_inc_generation(vespalib::Generation current_gen) {
    getGenerationHolder().assign_generation(current_gen);
}

template <typename B> bool SingleValueNumericAttribute<B>::onLoadEnumerated(ReaderBase& attrReader) {
    uint32_t numDocs = attrReader.getEnumCount();

    this->setNumDocs(numDocs);
    this->setCommittedDocIdLimit(numDocs);
    _data.unsafe_reserve(numDocs);

    auto udatBuffer = attribute::LoadUtils::loadUDAT(*this);
    CHECK((udatBuffer->size() % sizeof(T)) == 0);
    this->set_size_on_disk(attrReader.size_on_disk() + udatBuffer->size_on_disk());
    this->set_last_flush_duration(attrReader.flush_duration());
    std::span<const T> map(reinterpret_cast<const T*>(udatBuffer->buffer()), udatBuffer->size() / sizeof(T));
    attribute::loadFromEnumeratedSingleValue(_data, getGenerationHolder(), attrReader, map,
                                             std::span<const uint32_t>(), attribute::NoSaveLoadedEnum());
    return true;
}

template <typename B> bool SingleValueNumericAttribute<B>::onLoad(vespalib::Executor*) {
    PrimitiveReader<T> attrReader(*this);
    bool               ok(attrReader.getHasLoadData());

    if (!ok) {
        return false;
    }

    this->setCreateSerialNum(attrReader.getCreateSerialNum());

    if (attribute::DeltaCheckpoints::is_delta(attrReader.getDatHeader())) {
        return onLoadDeltaCheckpoint(attrReader);
    }
    if (attrReader.getEnumerated()) {
        return onLoadEnumerated(attrReader);
    }

    const size_t sz(attrReader.getDataCount());
    getGenerationHolder().reclaim_all();
    _data.reset();
    _data.unsafe_reserve(sz);
    for (uint32_t i = 0; i < sz; ++i) {
        _data.push_back(attrReader.getNextData());
    }

    B::setNumDocs(sz);
    B::setCommittedDocIdLimit(sz);
    this->set_size_on_disk(attrReader.size_on_disk());
    this->set_last_flush_duration(attrReader.flush_duration());
    const auto& header = attrReader.getDatHeader();
    if (header.hasTag(attribute::DeltaCheckpoints::checkpoint_id_tag)) {
        enable_delta_checkpoints();
        const auto id = header.getTag(attribute::DeltaCheckpoints::checkpoint_id_tag).asInteger();
        _delta_checkpoints->set_base({this->getBaseFileName() + ".dat", static_cast<uint64_t>(id)});
        _changed_since_full_checkpoint.clear();
    }

    return true;
}

template <typename B> bool SingleValueNumericAttribute<B>::onLoadDeltaCheckpoint(ReaderBase& attrReader) {
    using attribute::DeltaCheckpoints;
    const auto&    header = attrReader.getDatHeader();
    const uint64_t base_id = header.getTag(DeltaCheckpoints::checkpoint_id_tag).asInteger();
    const size_t   entries = header.getTag(DeltaCheckpoints::delta_entries_tag).asInteger();
    const uint32_t doc_id_limit = attrReader.getDocIdLimit();
    // A delta that doesn't match its base can't be repaired from the transaction log, which is pruned up to
    // this checkpoint: fail loudly.
    const std::string base_file = this->getBaseFileName() + DeltaCheckpoints::base_suffix;
    FileWithHeader    base(FileUtil::openFile(base_file));
    CHECK(base.valid());
    CHECK(base.header().hasTag(DeltaCheckpoints::checkpoint_id_tag));
    CHECK(static_cast<uint64_t>(base.header().getTag(DeltaCheckpoints::checkpoint_id_tag).asInteger()) == base_id);

    const size_t base_docs = std::min<size_t>(base.data_size() / sizeof(T), doc_id_limit);
    getGenerationHolder().reclaim_all();
    _data.reset();
    _data.unsafe_reserve(doc_id_limit);
    _data.unsafe_resize(doc_id_limit);
    if (base_docs > 0) {
        FileReader<T>(&base.file()).readNHostOrder(&_data[0], base_docs);
    }
    for (uint32_t lid = base_docs; lid < doc_id_limit; ++lid) {
        _data[lid] = B::defaultValue();
    }

    FileWithHeader        delta(FileUtil::openFile(this->getBaseFileName() + ".dat"));
    CHECK(delta.data_size() == entries * (sizeof(uint32_t) + sizeof(T)));
    std::vector<uint32_t> lids(entries);
    std::vector<T>        values(entries);
    FileReader<uint32_t>(&delta.file()).readNHostOrder(lids.data(), entries);
    FileReader<T>(&delta.file()).readNHostOrder(values.data(), entries);
    for (size_t i = 0; i < entries; ++i) {
        CHECK(lids[i] < doc_id_limit);
        _data[lids[i]] = values[i];
    }

    B::setNumDocs(doc_id_limit);
    B::setCommittedDocIdLimit(doc_id_limit);
    this->set_size_on_disk(attrReader.size_on_disk() + base.size_on_disk());
    this->set_last_flush_duration(attrReader.flush_duration());
    enable_delta_checkpoints();
    _delta_checkpoints->set_base({base_file, base_id});
    _changed_since_full_checkpoint = std::move(lids);
    return true;
}

template <typename B> void SingleValueNumericAttribute<B>::enable_delta_checkpoints() {
    if (!_delta_checkpoints) {
        _delta_checkpoints = std::make_shared<attribute::DeltaCheckpoints>();
    }
}

template <typename B>
std::unique_ptr<attribute::SearchContext>
SingleValueNumericAttribute<B>::getSearch(QueryTermSimple::UP                   qTerm,
                                          const attribute::SearchContextParams& params) const {
    (void)params;
    QueryTermSimple::RangeResult<T> res = qTerm->getRange<T>();
    auto                            data = _data.make_read_view(this->getCommittedDocIdLimit());
    if (res.isEqual()) {
        return std::make_unique<attribute::SingleNumericSearchContext<T, attribute::NumericMatcher<T>>>(
            std::move(qTerm), *this, data);
    } else {
        return std::make_unique<attribute::SingleNumericSearchContext<T, attribute::NumericRangeMatcher<T>>>(
            std::move(qTerm), *this, data);
    }
}

template <typename B>
void SingleValueNumericAttribute<B>::clearDocs(DocId lidLow, DocId lidLimit, bool in_shrink_lid_space) {
    CHECK(lidLow <= lidLimit);
    CHECK(lidLimit <= this->getNumDocs());
    uint32_t           count = 0;
    constexpr uint32_t commit_interval = 1000;
    for (DocId lid = lidLow; lid < lidLimit; ++lid) {
        if (!attribute::isUndefined(_data[lid])) {
            this->clearDoc(lid);
        }
        if ((++count % commit_interval) == 0) {
            if (in_shrink_lid_space) {
                this->clear_uncommitted_doc_id_limit();
            }
            this->commit();
        }
    }
}

template <typename B> void SingleValueNumericAttribute<B>::onShrinkLidSpace() {
    uint32_t committedDocIdLimit = this->getCommittedDocIdLimit();
    CHECK(_data.size() >= committedDocIdLimit);
    _data.shrink(committedDocIdLimit);
    this->setNumDocs(committedDocIdLimit);
    if (_delta_checkpoints) {
        // Lids past the new limit that are added again must not read their old values from the base.
        _delta_checkpoints->clear_base();
    }
}

template <typename B>
std::unique_ptr<AttributeSaver> SingleValueNumericAttribute<B>::onInitSave(std::string_view fileName) {
    if (_delta_checkpoints) {
        return initDeltaCheckpointSave(fileName);
    }
    const uint32_t numDocs(this->getCommittedDocIdLimit());
    CHECK(numDocs <= _data.size());
    return std::make_unique<SingleValueNumericAttributeSaver>(this->createAttributeHeader(fileName), &_data[0],
                                                              numDocs * sizeof(T));
}

template <typename B>
std::unique_ptr<AttributeSaver> SingleValueNumericAttribute<B>::initDeltaCheckpointSave(std::string_view fileName) {
    using attribute::DeltaCheckpointSaver;
    using attribute::DeltaCheckpoints;
    // Called by the attribute writer thread, which also records the changed lids.
    const uint32_t numDocs(this->getCommittedDocIdLimit());
    CHECK(numDocs <= _data.size());
    auto& lids = _changed_since_full_checkpoint;
    std::sort(lids.begin(), lids.end());
    lids.erase(std::unique(lids.begin(), lids.end()), lids.end());
    lids.erase(std::lower_bound(lids.begin(), lids.end(), numDocs), lids.end());
    const DeltaCheckpoints::Base base = _delta_checkpoints->base();
    const double delta_bytes = lids.size() * double(sizeof(uint32_t) + sizeof(T));
    if (base.id != 0 && delta_bytes < DeltaCheckpoints::full_ratio() * numDocs * sizeof(T)) {
        std::vector<T> values(lids.size());
        for (size_t i = 0; i < lids.size(); ++i) {
            values[i] = _data[lids[i]];
        }
        return DeltaCheckpointSaver::delta(this->createAttributeHeader(fileName), _delta_checkpoints, base, lids.size(),
                                           {std::as_bytes(std::span(lids)), std::as_bytes(std::span(values))});
    }
    // A full checkpoint: deltas are based on it once it is saved, and on nothing until then.
    _delta_checkpoints->clear_base();
    lids.clear();
    return DeltaCheckpointSaver::full(this->createAttributeHeader(fileName), _delta_checkpoints,
                                      DeltaCheckpoints::make_checkpoint_id(), &_data[0], numDocs * sizeof(T));
}

template <typename B>
size_t SingleValueNumericAttribute<B>::reserved_memory_for_flush(bool slow_disk) const noexcept {
    uint32_t committedDocIdLimit = this->getCommittedDocIdLimit();
    return slow_disk ? this->getEstimatedSaveByteSize() : committedDocIdLimit * sizeof(T);
}

} // namespace search
