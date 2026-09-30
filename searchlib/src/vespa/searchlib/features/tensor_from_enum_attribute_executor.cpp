// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "tensor_from_enum_attribute_executor.h"

#include <vespa/eval/eval/fast_value.h>
#include <vespa/searchcommon/attribute/iattributevector.h>
#include <vespa/searchlib/tensor/fast_value_view.h>

#include <vespa/vespalib/stllike/hash_map.hpp>

using search::attribute::CollectionType;
using search::attribute::IAttributeVector;
using search::tensor::FastValueView;
using vespalib::string_id;
using vespalib::eval::FastValueBuilderFactory;
using vespalib::eval::TypedCells;
using vespalib::eval::ValueType;

namespace search::features {

namespace {

std::unique_ptr<vespalib::eval::Value> make_empty(const ValueType& type) {
    auto builder = FastValueBuilderFactory::get().create_value_builder<double>(type, 1, 1, 0);
    return builder->build(std::move(builder));
}

} // namespace

TensorFromEnumAttributeExecutor::TensorFromEnumAttributeExecutor(const IAttributeVector& attribute,
                                                                 const ValueType&        type)
    : _attribute(attribute),
      _type(type),
      _attr_buffer(),
      _cached_label_ids(),
      _cached_labels(),
      _uncached_labels(),
      _hits(0),
      _misses(0),
      _caching(true),
      _labels(),
      _cells(),
      _empty_output(make_empty(_type)),
      _output(),
      _is_single_value(attribute.getCollectionType() == CollectionType::SINGLE) {
    _attr_buffer.allocate(_attribute.getMaxValueCount());
    _labels.reserve(_attr_buffer.capacity());
    _cells.reserve(_attr_buffer.capacity());
}

TensorFromEnumAttributeExecutor::~TensorFromEnumAttributeExecutor() = default;

string_id TensorFromEnumAttributeExecutor::resolve_label(EnumHandle value) {
    const char* label = _attribute.getStringFromEnum(value);
    if (_caching) {
        if (_cached_label_ids.size() < max_cached_labels) {
            string_id id = _cached_labels.add(label);
            _cached_label_ids.insert(std::make_pair(value, id));
            if (_cached_label_ids.size() == max_cached_labels) {
                _hits = 0;
                _misses = 0;
            }
            return id;
        }
        ++_misses;
    }
    return _uncached_labels.add(label);
}

void TensorFromEnumAttributeExecutor::check_cache_use() {
    // A full cache serving few lookups is not worth keeping: the labels it holds make resolving
    // the other labels slower, since the SharedStringRepo gets more entries.
    if ((_hits * 4) < (_hits + _misses)) {
        _cached_label_ids = LabelIdMap();
        _cached_labels.clear();
        _caching = false;
    }
    _hits = 0;
    _misses = 0;
}

void TensorFromEnumAttributeExecutor::execute(uint32_t docid) {
    // Only the output for the previous document references these labels
    _uncached_labels.clear();
    if (_caching && ((_hits + _misses) >= max_cached_labels)) {
        check_cache_use();
    }
    _attr_buffer.fill(_attribute, docid);
    if ((_attr_buffer.size() == 0) || (_is_single_value && _attribute.isUndefined(docid))) {
        outputs().set_object(0, *_empty_output);
        return;
    }
    _labels.clear();
    _cells.clear();
    for (const auto& value : _attr_buffer) {
        auto itr = _cached_label_ids.find(value.value());
        if (itr != _cached_label_ids.end()) {
            ++_hits;
            _labels.push_back(itr->second);
        } else {
            _labels.push_back(resolve_label(value.value()));
        }
        _cells.push_back(value.weight());
    }
    _output = std::make_unique<FastValueView>(_type, _labels, TypedCells(_cells), 1, _cells.size());
    outputs().set_object(0, *_output);
}

} // namespace search::features
