# decodeless_mappedfile

[`decodeless`](https://github.com/decodeless) is a collection of utility
libraries for conveniently reading and writing files via memory mapping.
Components can be used individually or combined.

`decodeless_mappedfile` is a small cross platform file mapping abstraction. It
provides simple objects to read and write to existing files. However, the more
interesting feature is a resizable mapped file. It works by reserving some
virtual address space and growing a file mapping into it for a really convenient
and fast way to write binary files.

[decodeless_writer](https://github.com/decodeless/writer) conbines this and
[decodeless_allocator](https://github.com/decodeless/allocator) to conveniently
write complex data structures directly as binary data.

**Advantages:**

- Data written is still accessible and can be random-access populated
- Pointers remain valid after growing the file size
- Write a file in one pass without computing the total size first
- RAII intended
- Memory mapping in general works well when combined with
  [offset_ptr](https://github.com/decodeless/offset_ptr)

**Disadvantages:**

- If writing data structures directly, binary compatibility becomes a concern.
  For that, [decodeless::allocator](https://github.com/decodeless/allocator) can
  handle alignment and
  [decodeless::header](https://github.com/decodeless/header) adds some basics to
  detect if an architecture difference could be a problem.
- Still needs a ballpark maximum size, but then virtual address space is cheap

## Code Example

```cpp
// Memory map a read-only file
decodeless::file mapped(filename);
const int* numbers = reinterpret_cast<const int*>(mapped.data());
...
```

```cpp
// Create a file
size_t maxSize = 1 << 30;  // reserved virtual address; can be huge
decodeless::resizable_file file(filename, maxSize);
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
```

## Building

This is a header-only C++20 library with CMake integration. Use any of:

- ```cmake
  add_subdirectory(path/to/mappedfile)
  ```

- ```cmake
  include(FetchContent)
  FetchContent_Declare(
      decodeless_mappedfile
      GIT_REPOSITORY https://github.com/decodeless/mappedfile.git
      GIT_TAG release_tag
      GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(decodeless_mappedfile)
  ```

- ```cmake
  find_package(decodeless_mappedfile REQUIRED CONFIG PATHS paths/to/search)
  ```

Then,

```cmake
target_link_libraries(myproject PRIVATE decodeless::mappedfile)
```

## Notes

- Windows implementation uses unofficial section API for `NtExtendSection` from
  `wdm.h`/`ntdll.dll`/"WDK". Please leave a comment if you know of an
  alternative. It works well, but technically could change at any time.
- Linux `resizable_file` maps more than the file size and truncates without
  remapping. Simple and very fast, although not explicitly supported in the man
  pages. Tests indicate the right thing still happens.

## Contributing

Issues and pull requests are most welcome, thank you! Note the
[DCO](CONTRIBUTING) and MIT [LICENSE](LICENSE).
