// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include "alloc.h"

#include <vespa/vespalib/objects/nbostream.h>

#include <cstdint>
#include <string>
#include <type_traits>

namespace vespalib::image {

/*
 * Memory images: a structure is saved as the bytes it has in memory, and loaded by mapping those bytes
 * (MAP_PRIVATE, so writes after load are copy-on-write) instead of reading and rebuilding it. Load time is then
 * independent of the structure's size; pages fault in on first touch.
 *
 * File layout: page-aligned data sections, then a metadata stream, then a fixed trailer
 * {magic, version, meta offset, meta length}. Metadata refers to sections by (offset, length).
 *
 * Enabled per process by VESPA_IMAGE_RESTART=1 (see enabled()).
 */
bool enabled() noexcept;

/*
 * Whether values of T can be saved as raw bytes and mapped back in another process. Owning pointers make a
 * type non-trivially destructible, which is what this checks; std::atomic members (trivially destructible but
 * not trivially copyable) are fine.
 */
template <typename T> struct image_copyable : std::bool_constant<std::is_trivially_destructible_v<T>> {};
template <typename T> constexpr bool image_copyable_v = image_copyable<T>::value;

struct Section {
    uint64_t offset = 0;
    uint64_t length = 0;
};

class ImageWriter {
public:
    explicit ImageWriter(const std::string& path);
    ~ImageWriter();
    ImageWriter(const ImageWriter&) = delete;
    ImageWriter& operator=(const ImageWriter&) = delete;
    // Writes len bytes at the next page-aligned offset of the file.
    Section write_section(const void* data, size_t len);
    nbostream& meta() noexcept { return _meta; }
    // Writes metadata and trailer, fsyncs and closes. Returns bytes written.
    uint64_t finish();
    const std::string& path() const noexcept { return _path; }

private:
    void write_all(const void* data, size_t len);
    std::string _path;
    int         _fd;
    uint64_t    _pos;
    nbostream   _meta;
};

class ImageReader {
public:
    explicit ImageReader(const std::string& path);
    ~ImageReader();
    ImageReader(const ImageReader&) = delete;
    ImageReader& operator=(const ImageReader&) = delete;
    nbostream& meta() noexcept { return _meta; }
    /*
     * Returns an allocation of capacity bytes whose first section.length bytes map the section (copy-on-write).
     * Bytes past the section are zero. Later allocations made with create() on the returned Alloc come from
     * base (nullptr means the default allocator), and freeing it unmaps the mapping.
     */
    alloc::Alloc map(Section section, size_t capacity, const alloc::MemoryAllocator* base) const;
    // Copies a section into memory the caller owns (for structures that cannot use a mapping).
    void read(Section section, void* dst) const;
    const std::string& path() const noexcept { return _path; }

private:
    std::string _path;
    int         _fd;
    nbostream   _meta;
};

inline nbostream& operator<<(nbostream& os, const Section& s) {
    os << s.offset << s.length;
    return os;
}
inline nbostream& operator>>(nbostream& is, Section& s) {
    is >> s.offset >> s.length;
    return is;
}

} // namespace vespalib::image
