# Copyright (c) 2024-2025 Pyarelal Knowles, MIT License

cmake_minimum_required(VERSION 3.20)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
endif()

# Unit tests
add_executable(${PROJECT_NAME}_tests src/mappedfile.cpp)
target_link_libraries(${PROJECT_NAME}_tests decodeless::mappedfile gtest_main
                      gmock_main)

if(MSVC)
  target_compile_options(${PROJECT_NAME}_tests PRIVATE /W4 /WX)
  target_compile_definitions(${PROJECT_NAME}_tests PRIVATE WIN32_LEAN_AND_MEAN=1
                                                           NOMINMAX)
else()
  target_compile_options(${PROJECT_NAME}_tests PRIVATE -Wall -Wextra -Wpedantic
                                                       -Werror)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(${PROJECT_NAME}_tests
                               PRIVATE $<$<CONFIG:Debug>:_GLIBCXX_DEBUG>)

    # Ugly detection for a working _GLIBCXX_DEBUG_BACKTRACE config, but the
    # feature itself is useful
    include(glibcxx_debug_backtrace.cmake)
    if(GLIBCXX_DEBUG_BACKTRACE_SUPPORTED)
      target_compile_definitions(
        ${PROJECT_NAME}_tests
        PRIVATE $<$<CONFIG:Debug>:_GLIBCXX_DEBUG_BACKTRACE>)
      target_link_libraries(${PROJECT_NAME}_tests
                            $<$<CONFIG:Debug>:${GLIBCXX_DEBUG_BACKTRACE_LIBRARY}>)
      target_compile_features(${PROJECT_NAME}_tests
                              PRIVATE ${GLIBCXX_DEBUG_BACKTRACE_CXX_FEATURE})
    endif()
  endif()
endif()

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_tests)
