// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "image_file.h"

#include <vespa/vespalib/util/exceptions.h>
#include <vespa/vespalib/util/stringfmt.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>
#include <sys/mman.h>
#include <unistd.h>

namespace vespalib::image {

using alloc::Alloc;
using alloc::MemoryAllocator;
using alloc::PtrAndSize;

namespace {

constexpr uint64_t MAGIC = 0x56494d4731ULL; // "VIMG1"
constexpr uint32_t VERSION = 1;
constexpr uint64_t PAGE = 4096;
constexpr size_t   TRAILER_SIZE = 8 + 4 + 8 + 8;

uint64_t round_up(uint64_t v, uint64_t align) noexcept {
    return (v + align - 1) & ~(align - 1);
}

/*
 * Wraps the allocator a structure normally uses. Image mappings it hands out are unmapped on free; everything
 * else, including allocations made later by create() when the structure grows, goes to the base allocator.
 */
class ImageMappingAllocator : public MemoryAllocator {
public:
    explicit ImageMappingAllocator(const MemoryAllocator* base) noexcept : _base(base) {}
    PtrAndSize alloc(size_t sz) const override { return _base->alloc(sz); }
    void free(PtrAndSize memory) const noexcept override {
        {
            std::lock_guard guard(_lock);
            auto            it = _mappings.find(memory.get());
            if (it != _mappings.end()) {
                _mappings.erase(it);
                munmap(memory.get(), memory.size());
                return;
            }
        }
        _base->free(memory);
    }
    size_t resize_inplace(PtrAndSize current, size_t newSize) const override {
        std::lock_guard guard(_lock);
        if (_mappings.contains(current.get())) {
            return 0;
        }
        return _base->resize_inplace(current, newSize);
    }
    void add_mapping(void* p) const {
        std::lock_guard guard(_lock);
        _mappings.insert(p);
    }

    static const ImageMappingAllocator* wrap(const MemoryAllocator* base) {
        static std::mutex                                                              lock;
        static std::map<const MemoryAllocator*, std::unique_ptr<ImageMappingAllocator>> wrappers;
        if (auto self = dynamic_cast<const ImageMappingAllocator*>(base)) {
            return self;
        }
        std::lock_guard guard(lock);
        auto&           wrapper = wrappers[base];
        if (!wrapper) {
            // Kept for the process lifetime: allocations made through a wrapper may outlive any one structure.
            wrapper = std::make_unique<ImageMappingAllocator>(base);
        }
        return wrapper.get();
    }

private:
    const MemoryAllocator*  _base;
    mutable std::mutex      _lock;
    mutable std::set<void*> _mappings;
};

} // namespace

bool enabled() noexcept {
    static const bool value = [] {
        const char* env = std::getenv("VESPA_IMAGE_RESTART");
        return env != nullptr && std::strcmp(env, "1") == 0;
    }();
    return value;
}

ImageWriter::ImageWriter(const std::string& path) : _path(path), _fd(-1), _pos(0), _meta() {
    _fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (_fd < 0) {
        throw IllegalStateException(make_string("Cannot create image file '%s': %s", path.c_str(), strerror(errno)));
    }
}

ImageWriter::~ImageWriter() {
    if (_fd >= 0) {
        ::close(_fd);
    }
}

void ImageWriter::write_all(const void* data, size_t len) {
    const char* p = static_cast<const char*>(data);
    while (len > 0) {
        ssize_t n = ::write(_fd, p, std::min(len, size_t(1) << 30));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw IllegalStateException(make_string("Write to image file '%s' failed: %s", _path.c_str(),
                                                    strerror(errno)));
        }
        p += n;
        len -= n;
        _pos += n;
    }
}

Section ImageWriter::write_section(const void* data, size_t len) {
    static const char zeros[PAGE] = {};
    uint64_t          aligned = round_up(_pos, PAGE);
    write_all(zeros, aligned - _pos);
    Section section{_pos, len};
    if (len > 0) {
        write_all(data, len);
    }
    return section;
}

uint64_t ImageWriter::finish() {
    // Pad so that the last page of the last section, which is mapped whole, holds zeros past the section.
    write_section(nullptr, 0);
    uint64_t meta_offset = _pos;
    uint64_t meta_length = _meta.size();
    write_all(_meta.peek(), meta_length);
    write_all(&MAGIC, sizeof(MAGIC));
    write_all(&VERSION, sizeof(VERSION));
    write_all(&meta_offset, sizeof(meta_offset));
    write_all(&meta_length, sizeof(meta_length));
    if (::fsync(_fd) != 0) {
        throw IllegalStateException(make_string("fsync of image file '%s' failed: %s", _path.c_str(),
                                                strerror(errno)));
    }
    ::close(_fd);
    _fd = -1;
    return _pos;
}

ImageReader::ImageReader(const std::string& path) : _path(path), _fd(-1), _meta() {
    _fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (_fd < 0) {
        throw IllegalStateException(make_string("Cannot open image file '%s': %s", path.c_str(), strerror(errno)));
    }
    off_t size = ::lseek(_fd, 0, SEEK_END);
    if (size < off_t(TRAILER_SIZE)) {
        throw IllegalStateException(make_string("Image file '%s' is truncated", path.c_str()));
    }
    char trailer[TRAILER_SIZE];
    if (::pread(_fd, trailer, TRAILER_SIZE, size - TRAILER_SIZE) != ssize_t(TRAILER_SIZE)) {
        throw IllegalStateException(make_string("Cannot read trailer of image file '%s'", path.c_str()));
    }
    uint64_t magic, meta_offset, meta_length;
    uint32_t version;
    memcpy(&magic, trailer, 8);
    memcpy(&version, trailer + 8, 4);
    memcpy(&meta_offset, trailer + 12, 8);
    memcpy(&meta_length, trailer + 20, 8);
    if (magic != MAGIC || version != VERSION || meta_offset + meta_length + TRAILER_SIZE != uint64_t(size)) {
        throw IllegalStateException(make_string("Image file '%s' has a bad trailer", path.c_str()));
    }
    std::string buf(meta_length, '\0');
    if (::pread(_fd, buf.data(), meta_length, meta_offset) != ssize_t(meta_length)) {
        throw IllegalStateException(make_string("Cannot read metadata of image file '%s'", path.c_str()));
    }
    _meta.write(buf.data(), buf.size());
}

ImageReader::~ImageReader() {
    if (_fd >= 0) {
        ::close(_fd);
    }
}

Alloc ImageReader::map(Section section, size_t capacity, const MemoryAllocator* base) const {
    if (base == nullptr) {
        base = Alloc::alloc().get_allocator();
    }
    auto* wrapper = ImageMappingAllocator::wrap(base);
    if (capacity == 0) {
        return Alloc::alloc_with_allocator(wrapper);
    }
    if (section.length > capacity || (section.offset % PAGE) != 0) {
        throw IllegalStateException(make_string("Image file '%s': bad section (offset %lu, length %lu, capacity %zu)",
                                                _path.c_str(), section.offset, section.length, capacity));
    }
    size_t reserve = round_up(capacity, PAGE);
    void*  p = mmap(nullptr, reserve, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        throw IllegalStateException(make_string("Cannot reserve %zu bytes for image '%s': %s", reserve,
                                                _path.c_str(), strerror(errno)));
    }
    if (section.length > 0) {
        void* q = mmap(p, round_up(section.length, PAGE), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, _fd,
                       section.offset);
        if (q == MAP_FAILED) {
            int err = errno;
            munmap(p, reserve);
            throw IllegalStateException(make_string("Cannot map image '%s' section at %lu: %s", _path.c_str(),
                                                    section.offset, strerror(err)));
        }
    }
    wrapper->add_mapping(p);
    return Alloc::adopt(wrapper, PtrAndSize(p, capacity));
}

} // namespace vespalib::image
