// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "array_store_config.h"
#include "buffer_type.h"

#include <vespa/vespalib/util/array.h>

#include <memory>

namespace vespalib::alloc {
class MemoryAllocator;
}

namespace vespalib::datastore {

/*
 * Class representing buffer type for large arrays in ArrayStore
 */
template <typename ElemT> class LargeArrayBufferType : public BufferType<Array<ElemT>> {
    using AllocSpec = ArrayStoreConfig::AllocSpec;
    using ArrayType = Array<ElemT>;
    using ParentType = BufferType<ArrayType>;
    using ParentType::empty_entry;
    using CleanContext = typename ParentType::CleanContext;
    std::shared_ptr<alloc::MemoryAllocator> _memory_allocator;

public:
    LargeArrayBufferType(const AllocSpec& spec, std::shared_ptr<alloc::MemoryAllocator> memory_allocator) noexcept;
    template <typename TypeMapper>
    LargeArrayBufferType(const AllocSpec& spec, std::shared_ptr<alloc::MemoryAllocator> memory_allocator,
                         TypeMapper&) noexcept
        : LargeArrayBufferType(spec, std::move(memory_allocator)) {}
    ~LargeArrayBufferType() override;
    void clean_hold(void* buffer, size_t offset, EntryCount num_entries, CleanContext cleanCtx) override;
    const vespalib::alloc::MemoryAllocator* get_memory_allocator() const override;
    // Memory images: every large array is written to the metadata stream and rebuilt on load.
    bool image_serializable() const noexcept override { return image::image_copyable_v<ElemT>; }
    void save_image_entries(const void* buffer, EntryCount num_entries, image::ImageWriter& writer) const override {
        auto* entries = static_cast<const ArrayType*>(buffer);
        auto& meta = writer.meta();
        for (EntryCount i = 0; i < num_entries; ++i) {
            meta << uint64_t(entries[i].size());
            meta.write(entries[i].data(), entries[i].size() * sizeof(ElemT));
        }
    }
    void load_image_entries(void* buffer, EntryCount num_entries, EntryCount reserved_entries,
                            image::ImageReader& reader) override {
        auto* entries = static_cast<ArrayType*>(buffer);
        auto& meta = reader.meta();
        for (EntryCount i = 0; i < num_entries; ++i) {
            uint64_t size = 0;
            meta >> size;
            ArrayType array(initial_alloc());
            array.resize(size);
            meta.read(array.data(), size * sizeof(ElemT));
            if (i < reserved_entries) {
                entries[i] = std::move(array);
            } else {
                new (static_cast<void*>(entries + i)) ArrayType(std::move(array));
            }
        }
    }
    alloc::Alloc initial_alloc() const noexcept {
        return _memory_allocator ? alloc::Alloc::alloc_with_allocator(_memory_allocator.get())
                                 : alloc::Alloc::alloc(0, alloc::MemoryAllocator::HUGEPAGE_SIZE);
    }
};

extern template class LargeArrayBufferType<uint8_t>;
extern template class LargeArrayBufferType<uint32_t>;
extern template class LargeArrayBufferType<int32_t>;
extern template class LargeArrayBufferType<std::string>;
extern template class LargeArrayBufferType<AtomicEntryRef>;

} // namespace vespalib::datastore
