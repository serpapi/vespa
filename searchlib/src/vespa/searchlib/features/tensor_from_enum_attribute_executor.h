// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include <vespa/eval/eval/fast_value_index.h>
#include <vespa/eval/eval/value.h>
#include <vespa/eval/eval/value_type.h>
#include <vespa/searchcommon/attribute/attributecontent.h>
#include <vespa/searchlib/fef/featureexecutor.h>
#include <vespa/vespalib/stllike/hash_map.h>
#include <vespa/vespalib/util/shared_string_repo.h>

#include <memory>
#include <optional>
#include <vector>

namespace search::features {

/**
 * Feature executor that converts the content of a string attribute with an enum store into a
 * tensor with one mapped dimension, using the attribute values as labels and the weights as cell
 * values. Produces the same tensor as TensorFromAttributeExecutor<WeightedConstCharContent>.
 *
 * The labels of the first max_cached_labels distinct enum values seen are resolved in the
 * SharedStringRepo once and kept, instead of being resolved and released again for every
 * document. Other labels are resolved per document. When the cache is full and fewer than a
 * quarter of the next max_cached_labels lookups hit it, the cache is dropped and all labels are
 * resolved per document from then on. The output tensor references these labels and is reused for
 * every document, so it is valid until the next call to execute.
 *
 * Enum handles are used as cache keys. The executor only lives while the query holds a read guard
 * on the attribute, and an enum handle is not reused for another value while a read guard is held.
 * A value moved by enum store compaction gets a new handle, which only adds a cache entry.
 */
class TensorFromEnumAttributeExecutor : public fef::FeatureExecutor {
public:
    static constexpr size_t max_cached_labels = 16384;

private:
    // Tensor with the labels and cells of the current document, indexed anew for each document
    class Output final : public vespalib::eval::Value {
        const vespalib::eval::ValueType&              _type;
        const vespalib::StringIdVector&               _labels;
        std::optional<vespalib::eval::FastValueIndex> _index;
        vespalib::eval::TypedCells                    _cells;

    public:
        Output(const vespalib::eval::ValueType& type, const vespalib::StringIdVector& labels);
        ~Output() override;
        void reset(std::span<const double> cells);
        const vespalib::eval::ValueType& type() const override { return _type; }
        const Index& index() const override { return *_index; }
        vespalib::eval::TypedCells cells() const override { return _cells; }
        vespalib::MemoryUsage get_memory_usage() const override;
    };

    using EnumHandle = attribute::IAttributeVector::EnumHandle;
    using LabelIdMap = vespalib::hash_map<EnumHandle, vespalib::string_id, vespalib::hash<EnumHandle>,
                                          std::equal_to<>, vespalib::hashtable_base::and_modulator>;

    const attribute::IAttributeVector&     _attribute;
    const vespalib::eval::ValueType        _type;
    attribute::WeightedEnumContent         _attr_buffer;
    LabelIdMap                             _cached_label_ids;
    vespalib::SharedStringRepo::Handles    _cached_labels;
    vespalib::SharedStringRepo::Handles    _uncached_labels;
    size_t                                 _hits;
    size_t                                 _misses;
    bool                                   _caching;
    vespalib::StringIdVector               _labels;
    std::vector<double>                    _cells;
    std::unique_ptr<vespalib::eval::Value> _empty_output;
    Output                                 _output;
    const bool                             _is_single_value;

    vespalib::string_id resolve_label(EnumHandle value);
    void check_cache_use();

public:
    TensorFromEnumAttributeExecutor(const attribute::IAttributeVector& attribute,
                                    const vespalib::eval::ValueType&   type);
    ~TensorFromEnumAttributeExecutor() override;
    void execute(uint32_t docid) override;
};

} // namespace search::features
