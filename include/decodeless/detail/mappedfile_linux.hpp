// Copyright (c) 2024 Pyarelal Knowles, MIT License

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <decodeless/detail/mappedfile_common.hpp>
#include <string.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace decodeless {

namespace detail {

inline long pageSize() {
    static const long initPagesize = sysconf(_SC_PAGESIZE);
    return initPagesize;
}

class LastError : public mapping_error {
public:
    LastError()
        : mapping_error(std::string(strerror(errno))) {}
};

class LastMappedFileError : public mapped_file_error {
public:
    LastMappedFileError(const fs::path& context)
        : mapped_file_error(std::string(strerror(errno)), context,
                            std::error_code(errno, std::system_category())) {}
};

class FileDescriptor {
public:
    using StatResult = struct stat;
    FileDescriptor(const fs::path& path, int flags, int mode = 0666 /* octal permissions */)
        : m_fd(open(path.c_str(), flags, mode)) {
        if (!*this) {
            throw LastMappedFileError(path);
        }
    }
    FileDescriptor() = delete;
    FileDescriptor(const FileDescriptor& other) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept
        : m_fd(other.m_fd) {
        other.m_fd = -1;
    }
    FileDescriptor& operator=(const FileDescriptor& other) = delete;
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        close(m_fd);
        m_fd = other.m_fd;
        other.m_fd = -1;
        return *this;
    }
    ~FileDescriptor() { close(m_fd); }
    operator int() const { return m_fd; }
    StatResult stat() const {
        StatResult result;
        if (fstat(m_fd, &result) == -1)
            throw LastError();
        return result;
    }
    size_t size() const { return static_cast<size_t>(stat().st_size); }
    void   truncate(size_t size) {
        if (ftruncate(m_fd, size) == -1)
            throw LastError();
    }

private:
    int m_fd;
};

template <int MemoryProtection>
class MemoryMap {
public:
    static constexpr bool ProtNone = MemoryProtection == PROT_NONE;
    static constexpr bool Writable = (MemoryProtection & PROT_WRITE) != 0;
    using address_type = std::conditional_t<Writable || ProtNone, void*, const void*>;
    MemoryMap(address_type addr, size_t length, int flags, int fd, off_t offset)
        : m_size(length)
        , m_address(mmap(const_cast<void*>(addr), length, MemoryProtection, flags, fd, offset))
        , m_fixed((flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) != 0) {
        if (m_address == MAP_FAILED) {
            throw LastError();
        }
    }
    MemoryMap() = delete;
    MemoryMap(const MemoryMap& other) = delete;
    MemoryMap(MemoryMap&& other) noexcept
        : m_size(other.m_size)
        , m_address(other.m_address) {
        other.m_address = MAP_FAILED;
    }
    MemoryMap& operator=(const MemoryMap& other) = delete;
    MemoryMap& operator=(MemoryMap&& other) noexcept {
        // DANGER: throws
        unmap();
        m_size = other.m_size;
        m_address = other.m_address;
        other.m_address = MAP_FAILED;
        return *this;
    }
    ~MemoryMap() {
        // DANGER: throws
        unmap();
    }
    address_type address() const { return m_address; }
    size_t       size() const { return m_size; }
    void         sync(int flags = MS_SYNC | MS_INVALIDATE)
        requires Writable
    {
        // ENOMEM "Cannot allocate memory" here likely means something remapped
        // the range before this object went out of scope. I haven't found a
        // good way to avoid this other than the user being careful to delete
        // the object before remapping.
        if (msync(const_cast<void*>(m_address), m_size, flags) == -1)
            throw LastError();
    }

private:
    void unmap() {
        if (m_address != MAP_FAILED) {
            // Perhaps controversial to do unconditionally, but safer/less
            // surprising?
            if constexpr (Writable)
                sync();

            // If the mapping was created with a specific address and MAP_FIXED,
            // restore the original mapping to PROT_NONE to keep the range
            // reserved. Otherwise, unmap.
            if (m_fixed) {
                if (mmap(const_cast<void*>(m_address), m_size, PROT_NONE,
                         MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
                    throw LastError();
            } else if (munmap(const_cast<void*>(m_address), m_size) == -1)
                throw LastError();
        }
    }

    size_t       m_size;
    address_type m_address;
    bool         m_fixed;
};

using MemoryMapRO = detail::MemoryMap<PROT_READ>;
using MemoryMapRW = detail::MemoryMap<PROT_READ | PROT_WRITE>;

template <bool Writable>
class MappedFile {
public:
    using data_type = std::conditional_t<Writable, void*, const void*>;
    MappedFile(const fs::path& path, int mapFlags = MAP_PRIVATE)
        : m_file(path, Writable ? O_RDWR : O_RDONLY)
        , m_mapped(nullptr, m_file.size(), mapFlags, m_file, 0) {}

    data_type data() const { return m_mapped.address(); }
    size_t    size() const { return m_mapped.size(); }

private:
    static constexpr int MapMemoryProtection = Writable ? PROT_READ : PROT_READ | PROT_WRITE;
    FileDescriptor       m_file;
    MemoryMap<MapMemoryProtection> m_mapped;
};

class ResizableMappedFile {
public:
    ResizableMappedFile() = delete;
    ResizableMappedFile(const ResizableMappedFile& other) = delete;
    ResizableMappedFile(ResizableMappedFile&& other) noexcept = default;
    ResizableMappedFile(const fs::path& path, size_t maxSize)
        : m_reserved(nullptr, maxSize, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        , m_file(path, O_CREAT | O_RDWR, 0666) {
        size_t size = throwIfAbove(m_file.size(), m_reserved.size());
        if (size)
            map(size);
    }
    ResizableMappedFile& operator=(const ResizableMappedFile& other) = delete;
    void*                data() const { return m_mapped ? m_mapped->address() : nullptr; }
    size_t               size() const { return m_mapped ? m_mapped->size() : 0; }
    size_t               capacity() const { return m_reserved.size(); }
    void                 resize(size_t size) {
        size = throwIfAbove(size, m_reserved.size());
        m_mapped.reset();
        m_file.truncate(size);
        if (size)
            map(size);
    }

    // Override default move assignment so m_reserved outlives m_mapped
    ResizableMappedFile& operator=(ResizableMappedFile&& other) noexcept {
        m_mapped = std::move(other.m_mapped);
        m_file = std::move(other.m_file);
        m_reserved = std::move(other.m_reserved);
        return *this;
    }

private:
    void map(size_t size) {
        m_mapped.emplace(m_reserved.address(), size, MAP_FIXED | MAP_SHARED_VALIDATE, m_file, 0);
    }
    static size_t throwIfAbove(size_t v, size_t limit) {
        if (v > limit)
            throw std::bad_alloc();
        return v;
    }
    detail::MemoryMap<PROT_NONE>       m_reserved;
    FileDescriptor                     m_file;
    std::optional<detail::MemoryMapRW> m_mapped;
};

static_assert(std::is_move_constructible_v<ResizableMappedFile>);
static_assert(std::is_move_assignable_v<ResizableMappedFile>);

} // namespace detail

} // namespace decodeless
