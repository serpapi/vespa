// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "unique_store_string_allocator.hpp"

#include "buffer_type.hpp"

#include <vespa/check_require.h>
#include <vespa/vespalib/util/size_literals.h>

#include <algorithm>

namespace vespalib::datastore {

namespace {

constexpr size_t NUM_ARRAYS_FOR_NEW_UNIQUESTORE_BUFFER = 1_Ki;
constexpr float  ALLOC_GROW_FACTOR = 0.2;

} // namespace

namespace string_allocator {

std::vector<size_t> array_sizes = {16, 24, 32, 40, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 256};

const size_t small_string_entry_value_offset = UniqueStoreSmallStringEntry().value_offset();

uint32_t get_type_id(size_t string_len) {
    auto len = small_string_entry_value_offset + string_len + 1;
    auto itr = std::lower_bound(array_sizes.cbegin(), array_sizes.cend(), len);
    if (itr != array_sizes.end()) {
        return itr - array_sizes.cbegin() + 1;
    } else {
        return 0;
    }
}

} // namespace string_allocator

UniqueStoreSmallStringBufferType::UniqueStoreSmallStringBufferType(
    uint32_t array_size, uint32_t max_arrays, std::shared_ptr<vespalib::alloc::MemoryAllocator> memory_allocator)
    : BufferType<char>(array_size, 2u, max_arrays, NUM_ARRAYS_FOR_NEW_UNIQUESTORE_BUFFER, ALLOC_GROW_FACTOR),
      _memory_allocator(std::move(memory_allocator)) {
}

UniqueStoreSmallStringBufferType::~UniqueStoreSmallStringBufferType() = default;

void UniqueStoreSmallStringBufferType::destroy_entries(void*, EntryCount) {
    static_assert(std::is_trivially_destructible<UniqueStoreSmallStringEntry>::value,
                  "UniqueStoreSmallStringEntry must be trivially destructable");
}

void UniqueStoreSmallStringBufferType::fallback_copy(void* newBuffer, const void* oldBuffer, EntryCount num_entries) {
    static_assert(std::is_trivially_copyable<UniqueStoreSmallStringEntry>::value,
                  "UniqueStoreSmallStringEntry must be trivially copyable");
    if (num_entries > 0) {
        memcpy(newBuffer, oldBuffer, num_entries * getArraySize());
    }
}

void UniqueStoreSmallStringBufferType::clean_hold(void* buffer, size_t offset, EntryCount num_entries, CleanContext) {
    size_t array_size = getArraySize();
    void*  e = static_cast<char*>(buffer) + offset * array_size;
    void*  e_end = static_cast<char*>(e) + num_entries * array_size;
    while (e < e_end) {
        static_cast<UniqueStoreSmallStringEntry*>(e)->clean_hold(array_size);
        e = static_cast<char*>(e) + array_size;
    }
    CHECK(e == e_end);
}

const vespalib::alloc::MemoryAllocator* UniqueStoreSmallStringBufferType::get_memory_allocator() const {
    return _memory_allocator.get();
}

UniqueStoreExternalStringBufferType::UniqueStoreExternalStringBufferType(
    uint32_t array_size, uint32_t max_arrays, std::shared_ptr<vespalib::alloc::MemoryAllocator> memory_allocator)
    : BufferType<UniqueStoreEntry<std::string>>(array_size, 0u, max_arrays, NUM_ARRAYS_FOR_NEW_UNIQUESTORE_BUFFER,
                                                ALLOC_GROW_FACTOR),
      _memory_allocator(std::move(memory_allocator)) {
}

UniqueStoreExternalStringBufferType::~UniqueStoreExternalStringBufferType() = default;

void UniqueStoreExternalStringBufferType::clean_hold(void* buffer, size_t offset, EntryCount num_entries,
                                                     CleanContext cleanCtx) {
    UniqueStoreEntry<std::string>* elem = static_cast<UniqueStoreEntry<std::string>*>(buffer) + offset;
    for (size_t i = 0; i < num_entries; ++i) {
        cleanCtx.extraBytesCleaned(elem->value().size() + 1);
        std::string().swap(elem->value());
        ++elem;
    }
}

void UniqueStoreExternalStringBufferType::save_image_entries(const void* buffer, EntryCount num_entries,
                                                             image::ImageWriter& writer) const {
    auto* entries = static_cast<const UniqueStoreEntry<std::string>*>(buffer);
    auto& os = writer.meta();
    for (EntryCount i = 0; i < num_entries * getArraySize(); ++i) {
        os << entries[i].get_ref_count() << entries[i].value();
    }
}

void UniqueStoreExternalStringBufferType::load_image_entries(void* buffer, EntryCount num_entries,
                                                             EntryCount reserved_entries,
                                                             image::ImageReader& reader) {
    auto*       entries = static_cast<UniqueStoreEntry<std::string>*>(buffer);
    auto&       is = reader.meta();
    uint32_t    ref_count = 0;
    std::string value;
    for (EntryCount i = 0; i < num_entries * getArraySize(); ++i) {
        is >> ref_count >> value;
        if (i < reserved_entries * getArraySize()) {
            entries[i].value() = value;
        } else {
            new (static_cast<void*>(entries + i)) UniqueStoreEntry<std::string>(value);
        }
        entries[i].set_ref_count(ref_count);
    }
}

const vespalib::alloc::MemoryAllocator* UniqueStoreExternalStringBufferType::get_memory_allocator() const {
    return _memory_allocator.get();
}

template class UniqueStoreStringAllocator<EntryRefT<22>>;
template class BufferType<UniqueStoreEntry<std::string>>;

} // namespace vespalib::datastore
