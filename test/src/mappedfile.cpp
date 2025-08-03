// Copyright (c) 2024 Pyarelal Knowles, MIT License

#include <algorithm>
#include <cstdio>
#include <decodeless/mappedfile.hpp>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ostream>
#include <span>

using namespace decodeless;

class MappedFileFixture : public ::testing::Test {
protected:
    void SetUp() override {
        std::ofstream ofile(m_tmpFile, std::ios::binary);
        int           d = 42;
        ofile.write(reinterpret_cast<char*>(&d), sizeof(d));
    }

    void TearDown() override { fs::remove(m_tmpFile); }

    fs::path m_tmpFile = fs::path{testing::TempDir()} / "test.dat";
};

TEST_F(MappedFileFixture, ReadOnly) {
    file mapped(m_tmpFile);
    EXPECT_EQ(*static_cast<const int*>(mapped.data()), 42);
}

TEST_F(MappedFileFixture, Writable) {
    {
        writable_file mapped(m_tmpFile);
        ASSERT_GE(mapped.size(), sizeof(int));
        *static_cast<int*>(mapped.data()) = 123;
    }
    {
        std::ifstream ifile(m_tmpFile, std::ios::binary);
        int           contents;
        ifile.read(reinterpret_cast<char*>(&contents), sizeof(contents));
        EXPECT_EQ(contents, 123);
    }
}

#ifdef _WIN32

TEST_F(MappedFileFixture, FileHandle) {
    detail::FileHandle file(m_tmpFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    EXPECT_TRUE(file); // a bit pointless - would have thrown if not
}

TEST_F(MappedFileFixture, Create) {
    fs::path tmpFile2 = fs::path{testing::TempDir()} / "test2.dat";
    EXPECT_FALSE(fs::exists(tmpFile2));
    if (fs::exists(tmpFile2))
        fs::remove(tmpFile2);
    {
        detail::FileHandle file(tmpFile2, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        EXPECT_EQ(file.size(), 0);
        file.setPointer(sizeof(int));
        file.setEndOfFile();
        EXPECT_EQ(file.size(), sizeof(int));
        detail::FileMappingHandle mapped(file, nullptr, PAGE_READWRITE, file.size());
        detail::FileMappingView   view(mapped, FILE_MAP_WRITE);
        *reinterpret_cast<int*>(view.address()) = 42;
    }
    {
        std::ifstream ifile(tmpFile2, std::ios::binary);
        int           contents;
        ifile.read(reinterpret_cast<char*>(&contents), sizeof(contents));
        EXPECT_EQ(contents, 42);
    }
    EXPECT_TRUE(fs::exists(tmpFile2));
    fs::remove(tmpFile2);
    EXPECT_FALSE(fs::exists(tmpFile2));
}

TEST_F(MappedFileFixture, Reserve) {
    fs::path tmpFile2 = fs::path{testing::TempDir()} / "test2.dat";
    {
        // Create a new file
        detail::FileHandle file(tmpFile2, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        EXPECT_EQ(fs::file_size(tmpFile2), 0);

        // https://stackoverflow.com/questions/44101966/adding-new-bytes-to-memory-mapped-file -
        // thanks, RbMm!
        size_t                          initialSize = sizeof(int);
        detail::NtifsSection            ntifs;
        detail::Section<PAGE_READWRITE> section(
            ntifs, SECTION_MAP_WRITE | SECTION_MAP_READ | SECTION_EXTEND_SIZE, 0, initialSize,
            SEC_COMMIT, file);
        EXPECT_EQ(fs::file_size(tmpFile2), initialSize);

        // Reserve 1mb of memory and map the file
        LONGLONG                            reserved = 1024 * 1024;
        detail::SectionView<PAGE_READWRITE> view(ntifs, section,
                                                 detail::NtifsSection::CurrentProcess(), 0, 0, 0,
                                                 reserved, detail::ViewUnmap, MEM_RESERVE);
        EXPECT_EQ(view.query().Type, MEM_MAPPED);
        EXPECT_EQ(view.query().State, MEM_COMMIT);
        EXPECT_EQ(view.query(detail::pageSize()).Type, MEM_MAPPED);
        EXPECT_EQ(view.query(detail::pageSize()).State, MEM_RESERVE); // not COMMIT

        // Write to it
        *reinterpret_cast<int*>(view.address()) = 42;

        // Resize the file
        auto newSize = detail::pageSize() * 2 + 31;
        section.extend(newSize);
        EXPECT_EQ(fs::file_size(tmpFile2), newSize);
        EXPECT_EQ(view.query(detail::pageSize()).Type, MEM_MAPPED);
        EXPECT_EQ(view.query(detail::pageSize()).State, MEM_COMMIT); // now COMMIT

        // Check the contents is still there and write to the new region
        EXPECT_EQ(*reinterpret_cast<int*>(view.address()), 42);
        reinterpret_cast<char*>(view.address())[section.size() - 1] = 'M';
    }
    fs::remove(tmpFile2);
}

// TODO: FILE_MAP_LARGE_PAGES

TEST_F(MappedFileFixture, WinAllocationGranulairty) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    EXPECT_GE(info.dwAllocationGranularity, sizeof(std::max_align_t));
    EXPECT_GE(info.dwAllocationGranularity, 4096u);
}

#else

TEST_F(MappedFileFixture, LinuxFileDescriptor) {
    detail::FileDescriptor fd(m_tmpFile, O_RDONLY);
    EXPECT_NE(fd, -1);
}

TEST_F(MappedFileFixture, LinuxCreate) {
    fs::path tmpFile2 = fs::path{testing::TempDir()} / "test2.dat";
    EXPECT_FALSE(fs::exists(tmpFile2));
    {
        detail::FileDescriptor fd(tmpFile2, O_CREAT | O_RDWR);
        EXPECT_EQ(fd.size(), 0);
        fd.truncate(sizeof(int));
        EXPECT_EQ(fd.size(), sizeof(int));
        detail::MemoryMap<PROT_READ | PROT_WRITE> mapped(nullptr, fd.size(), MAP_SHARED, fd, 0);
        *reinterpret_cast<int*>(mapped.address()) = 42;
    }
    {
        std::ifstream ifile(tmpFile2, std::ios::binary);
        int           contents;
        ifile.read(reinterpret_cast<char*>(&contents), sizeof(contents));
        EXPECT_EQ(contents, 42);
    }
    EXPECT_TRUE(fs::exists(tmpFile2));
    fs::remove(tmpFile2);
    EXPECT_FALSE(fs::exists(tmpFile2));
}

TEST_F(MappedFileFixture, LinuxReserve) {
    detail::MemoryMap<PROT_NONE> reserved(nullptr, detail::pageSize() * 4,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    detail::FileDescriptor       fd(m_tmpFile, O_RDONLY);
    detail::MemoryMapRO mapped(reserved.address(), fd.size(), MAP_FIXED | MAP_SHARED_VALIDATE, fd,
                               0);
    EXPECT_EQ(mapped.address(), reserved.address());
    EXPECT_EQ(*reinterpret_cast<const int*>(mapped.address()), 42);
}

TEST_F(MappedFileFixture, LinuxResize) {
    // Reserve some virtual address space
    detail::MemoryMap<PROT_NONE> reserved(nullptr, detail::pageSize() * 4,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // Create a new file
    fs::path               tmpFile2 = fs::path{testing::TempDir()} / "test2.dat";
    detail::FileDescriptor fd(tmpFile2, O_CREAT | O_RDWR);
    EXPECT_EQ(fs::file_size(tmpFile2), 0);
    fd.truncate(sizeof(int));
    EXPECT_EQ(fs::file_size(tmpFile2), fd.size());

    // Map it to the reserved range and write to it
    int* originalPointer;
    {
        detail::MemoryMapRW mapped(reserved.address(), fd.size(), MAP_FIXED | MAP_SHARED_VALIDATE,
                                   fd, 0);
        originalPointer = reinterpret_cast<int*>(mapped.address());
        *originalPointer = 42;
    }

    // Verify trying to use the reserved address space doesn't work
    void* addressUsed = reinterpret_cast<std::byte*>(reserved.address()) + detail::pageSize();
    EXPECT_THROW(detail::MemoryMap<PROT_NONE>(addressUsed, detail::pageSize(),
                                              MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS, -1,
                                              0),
                 mapping_error);

    // Try to map the reserved range with a hint, but expect to get a different address
    {
        detail::MemoryMapRW differentReserved(addressUsed, detail::pageSize(),
                                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        EXPECT_NE(reserved.address(), differentReserved.address());
        EXPECT_NE(addressUsed, differentReserved.address());
    }

    // Resize the file
    auto newSize = detail::pageSize() * 2 + 31;
    fd.truncate(newSize);
    EXPECT_EQ(fs::file_size(tmpFile2), fd.size());

    // Map it again to the same virtual address range, check the contents is
    // still there and write to the new region
    {
        detail::MemoryMapRW mapped(reserved.address(), fd.size(), MAP_FIXED | MAP_SHARED_VALIDATE,
                                   fd, 0);
        EXPECT_EQ(originalPointer, reinterpret_cast<int*>(mapped.address()));
        EXPECT_EQ(*originalPointer, 42);
        reinterpret_cast<char*>(mapped.address())[mapped.size() - 1] = 'M';
    }

    // Validate the new region
    {
        EXPECT_EQ(fs::file_size(tmpFile2), newSize);
        std::ifstream ifile(tmpFile2, std::ios::binary);
        EXPECT_TRUE(ifile.good());
        ifile.seekg(newSize - 1);
        EXPECT_TRUE(ifile.good());
        EXPECT_EQ(ifile.get(), 'M');
    }
    fs::remove(tmpFile2);
    EXPECT_FALSE(fs::exists(tmpFile2));
}

// TODO:
// - MAP_HUGETLB
// - MAP_HUGE_2MB, MAP_HUGE_1GB

#endif

TEST(MappedMemory, ResizeMemory) {
    const char str[] = "hello world!";
    {
        resizable_memory file(0, 10000);
        EXPECT_EQ(file.data(), nullptr);
        EXPECT_EQ(file.size(), 0);
#ifdef _WIN32
        EXPECT_THROW(file.resize(10001), std::bad_alloc);
#else
        EXPECT_THROW(file.resize(10001), std::bad_alloc);
#endif
        file.resize(13);
        EXPECT_EQ(file.size(), 13);
        std::span fileStr(reinterpret_cast<char*>(file.data()), std::size(str));
        std::ranges::copy(str, fileStr.begin());
        EXPECT_TRUE(std::ranges::equal(str, fileStr));
        void* before = file.data();
        file.resize(1500);
        EXPECT_EQ(file.size(), 1500);
        EXPECT_EQ(file.data(), before);
        EXPECT_TRUE(std::ranges::equal(str, fileStr));
        file.resize(10000);
        EXPECT_EQ(file.size(), 10000);
        EXPECT_EQ(file.data(), before);
        EXPECT_TRUE(std::ranges::equal(str, fileStr));
        std::ranges::copy(std::string("EOF"),
                          reinterpret_cast<char*>(file.data()) + file.size() - 3);

        // Test the move assignment operator. On linux the file is briefly
        // mapped twice. Windows gets an error ("The requested operation cannot
        // be performed on a file with a user-mapped section open.") so another
        // temporary file is mapped.
#ifdef _WIN32
        file = resizable_memory(0, 1500);
        EXPECT_NE(file.data(), before);
#endif
        file = resizable_memory(0, 1500);

#ifndef _WIN32
        EXPECT_NE(file.data(), before);
#endif
    }
}

TEST(MappedMemory, ResizeMemoryExtended) {
    size_t           nextBytes = 1;
    resizable_memory memory(nextBytes, 1llu << 32); // 4gb of virtual memory pls
    void*            data = memory.data();
    uint8_t*         bytes = reinterpret_cast<uint8_t*>(data);
    bytes[0] = 1;

    // Grow
    while ((nextBytes *= 2) <= 256 * 1024 * 1024) {
        EXPECT_NO_THROW(memory.resize(nextBytes));
        EXPECT_EQ(data, memory.data());
        uint8_t b = 0;
        // Verify memory from previous resizes remains intact
        for (size_t i = 1; i < nextBytes; i *= 2)
            EXPECT_EQ(bytes[i - 1], ++b);
        bytes[nextBytes - 1] = ++b;
    }

    // Shrink
    while ((nextBytes /= 2) > 1) {
        EXPECT_NO_THROW(memory.resize(nextBytes));
        EXPECT_EQ(data, memory.data());
        uint8_t b = 0;
        // Verify memory from previous resizes remains intact
        for (size_t i = 1; i < nextBytes; i *= 2)
            EXPECT_EQ(bytes[i - 1], ++b);
    }
}

TEST_F(MappedFileFixture, ResizeFile) {
    fs::path   tmpFile2 = fs::path{testing::TempDir()} / "test2.dat";
    const char str[] = "hello world!";
    {
        resizable_file file(tmpFile2, 10000);
        EXPECT_EQ(file.data(), nullptr);
        EXPECT_EQ(file.size(), 0);
        EXPECT_THROW(file.resize(10001), std::bad_alloc);
        file.resize(13);
        EXPECT_EQ(file.size(), 13);
        std::span fileStr(reinterpret_cast<char*>(file.data()), std::size(str));
        std::ranges::copy(str, fileStr.begin());
        EXPECT_TRUE(std::ranges::equal(str, fileStr));
        void* before = file.data();
        file.resize(1500);
        EXPECT_EQ(file.size(), 1500);
        EXPECT_EQ(file.data(), before);
        EXPECT_TRUE(std::ranges::equal(str, fileStr));
        file.resize(10000);
        EXPECT_EQ(file.size(), 10000);
        EXPECT_EQ(file.data(), before);
        EXPECT_TRUE(std::ranges::equal(str, fileStr));
        std::ranges::copy(std::string("EOF"),
                          reinterpret_cast<char*>(file.data()) + file.size() - 3);

        // Test the move assignment operator. On linux the file is briefly
        // mapped twice. Windows gets an error ("The requested operation cannot
        // be performed on a file with a user-mapped section open.") so another
        // temporary file is mapped.
#ifdef _WIN32
        file = resizable_file(m_tmpFile, 10000);
        EXPECT_NE(file.data(), before);
#endif
        file = resizable_file(tmpFile2, 10000);

#ifndef _WIN32
        EXPECT_NE(file.data(), before);
#endif

        EXPECT_EQ(file.size(), 10000);
        std::span eof(reinterpret_cast<char*>(file.data()) + file.size() - 3, 3);
        EXPECT_TRUE(std::ranges::equal(std::string("EOF"), eof));
    }
    EXPECT_THROW((void)resizable_file(tmpFile2, 1499), std::bad_alloc);
    {
        resizable_file file(tmpFile2, 20000);
        EXPECT_EQ(file.size(), 10000);
        std::span eof(reinterpret_cast<char*>(file.data()) + file.size() - 3, 3);
        EXPECT_TRUE(std::ranges::equal(std::string("EOF"), eof));
        file.resize(13);
        std::span fileStr(reinterpret_cast<char*>(file.data()), std::size(str));
        EXPECT_TRUE(std::ranges::equal(str, fileStr));
    }
    fs::remove(tmpFile2);
    EXPECT_FALSE(fs::exists(tmpFile2));
}

TEST_F(MappedFileFixture, ResizeFileExtended) {
    fs::path   tmpFile2 = fs::path{testing::TempDir()} / "test2.dat";
    {
        resizable_file file(tmpFile2, 1llu << 32); // 4gb of virtual memory pls
        file.resize(1);
        void*          data = file.data();
        uint8_t*       bytes = reinterpret_cast<uint8_t*>(data);
        bytes[0] = 1;

        // Grow
        size_t nextBytes = 1;
        while ((nextBytes *= 2) <= 256 * 1024 * 1024) {
            EXPECT_NO_THROW(file.resize(nextBytes));
            EXPECT_EQ(data, file.data());
            uint8_t b = 0;
            // Verify memory from previous resizes remains intact
            for (size_t i = 1; i < nextBytes; i *= 2)
                EXPECT_EQ(bytes[i - 1], ++b);
            bytes[nextBytes - 1] = ++b;
        }

        // Shrink
        while ((nextBytes /= 2) > 1) {
            EXPECT_NO_THROW(file.resize(nextBytes));
            EXPECT_EQ(data, file.data());
            uint8_t b = 0;
            // Verify memory from previous resizes remains intact
            for (size_t i = 1; i < nextBytes; i *= 2)
                EXPECT_EQ(bytes[i - 1], ++b);
        }
    }
    fs::remove(tmpFile2);
    EXPECT_FALSE(fs::exists(tmpFile2));
}

TEST_F(MappedFileFixture, ResizableFileSize) {
    size_t lastSize = fs::file_size(m_tmpFile);
    size_t sizes[] = {0, 1, 2, 4000, 4095, 4096, 4097, 10000, 0, 4097, 4096, 4095, 42};
    for (size_t size : sizes) {
        resizable_file file(m_tmpFile, 10000);
        EXPECT_EQ(file.size(), lastSize);
        file.resize(size);
        EXPECT_EQ(file.size(), size);
        lastSize = size;
    }
    EXPECT_EQ(fs::file_size(m_tmpFile), lastSize);
}

TEST_F(MappedFileFixture, Readme) {
    fs::path       tmpFile2 = fs::path{testing::TempDir()} / "test2.dat";
    {
        size_t         maxSize = 4096;
        resizable_file file(tmpFile2, maxSize);
        EXPECT_EQ(file.size(), 0);
        EXPECT_EQ(file.data(), nullptr);

        // Resize and write some data
        file.resize(sizeof(int) * 10);
        int* numbers = reinterpret_cast<int*>(file.data());
        numbers[9] = 9;

        // Resize again. Pointer remains valid and there's more space
        file.resize(sizeof(int) * 100);
        EXPECT_EQ(numbers[9], 9);
        numbers[99] = 99;
    }
    fs::remove(tmpFile2);
    EXPECT_FALSE(fs::exists(tmpFile2));
}

#ifndef _WIN32
std::vector<uint8_t> getResidency(void* base, size_t size) {
    std::vector<unsigned char> result(size / getpagesize(), 0u);
    int                        ret = mincore(base, size, result.data());
    if (ret != 0)
        throw detail::LastError();
    return result;
}

TEST(MappedMemory, PageResidencyAfterDecommit) {
    const size_t page_size = getpagesize();
    const size_t reserve_size = page_size * 64; // 64 pages total
    const size_t commit_size = page_size * 4;   // We'll use 4 pages

    // Reserve virtual address space (uncommitted, inaccessible)
    void* base =
        mmap(nullptr, reserve_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    ASSERT_NE(base, MAP_FAILED) << "Failed to mmap reserved space";
    EXPECT_TRUE(std::ranges::all_of(getResidency(base, commit_size),
                                    [](uint8_t c) { return (c & 1u) == 0; }));

    // Commit a portion with PROT_READ | PROT_WRITE
    int prot_result = mprotect(base, commit_size, PROT_READ | PROT_WRITE);
    ASSERT_EQ(prot_result, 0) << "Failed to mprotect committed region";
    EXPECT_TRUE(std::ranges::all_of(getResidency(base, commit_size),
                                    [](uint8_t c) { return (c & 1u) == 0; }));

    // Touch the memory to ensure it's backed by RAM
    std::span committed(static_cast<std::byte*>(base), commit_size);
    std::ranges::fill(committed, std::byte(0xAB));

    // Verify pages are resident using mincore
    EXPECT_TRUE(std::ranges::all_of(getResidency(base, commit_size),
                                    [](uint8_t c) { return (c & 1u) == 1; }));

    // Decommit
    #if 0
    void* remap = mmap(base, commit_size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    ASSERT_EQ(remap, base) << "Failed to remap to decommit pages";
    #else
    // See MADV_FREE discussion here: https://github.com/golang/go/issues/42330
    prot_result = mprotect(base, commit_size, PROT_NONE);
    ASSERT_EQ(prot_result, 0) << "Failed to mprotect committed region back to PROT_NONE";
    int madvise_result = madvise(base, commit_size, MADV_DONTNEED);
    ASSERT_EQ(madvise_result, 0) << "Failed to release pages with madvise";
    #endif
    EXPECT_TRUE(std::ranges::all_of(getResidency(base, commit_size),
                                    [](uint8_t c) { return (c & 1u) == 0; }));

    // Cleanup
    munmap(base, reserve_size);
}
#endif
