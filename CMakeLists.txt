cmake_minimum_required(VERSION 3.20)
project(NexusMatch VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_NATIVE_ARCH "Enable -march=native for host optimization" OFF)
option(BUILD_BENCHMARKS "Build benchmark executables" ON)
option(BUILD_TESTS "Build test suite" ON)

# Compiler warnings and options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror=return-type
    )

    if(ENABLE_NATIVE_ARCH)
        add_compile_options(-march=native)
    endif()

    if(ENABLE_ASAN_UBSAN)
        message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()

    if(ENABLE_TSAN)
        message(STATUS "Enabling ThreadSanitizer")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -no-pie)
        add_link_options(-fsanitize=thread -no-pie)
    endif()
endif()

include_directories(include .)

# Check which core sources exist
set(CORE_SOURCES "")
foreach(SRC_FILE 
    src/order_arena.cpp
    src/order_id_index.cpp
    src/price_bitmap.cpp
    src/order_book.cpp
    src/matching_engine.cpp
    src/spsc_queue.cpp
    src/command_wal.cpp
)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}")
        list(APPEND CORE_SOURCES "${SRC_FILE}")
    endif()
endforeach()

if(CORE_SOURCES)
    add_library(nexusmatch_core ${CORE_SOURCES})
    target_include_directories(nexusmatch_core PUBLIC include)
    target_link_libraries(nexusmatch_core PUBLIC pthread)
endif()

# Main CLI demo executable
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" AND TARGET nexusmatch_core)
    add_executable(nexusmatch_demo src/main.cpp)
    target_link_libraries(nexusmatch_demo PRIVATE nexusmatch_core)
endif()

# Web server gateway engine executable
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/server_main.cpp" AND TARGET nexusmatch_core)
    add_executable(nexusmatch_server src/server_main.cpp)
    target_link_libraries(nexusmatch_server PRIVATE nexusmatch_core)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()

    macro(add_nexus_test target_name source_file)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
            add_executable(${target_name} ${source_file})
            if(TARGET nexusmatch_core)
                target_link_libraries(${target_name} PRIVATE nexusmatch_core pthread)
            else()
                target_link_libraries(${target_name} PRIVATE pthread)
            endif()
            add_test(NAME ${target_name} COMMAND ${target_name})
        endif()
    endmacro()

    add_nexus_test(test_smoke tests/test_smoke.cpp)
    add_nexus_test(test_semantics tests/test_semantics.cpp)
    add_nexus_test(test_arena tests/test_arena.cpp)
    add_nexus_test(test_order_id_index tests/test_order_id_index.cpp)
    add_nexus_test(test_intrusive_queue tests/test_intrusive_queue.cpp)
    add_nexus_test(test_price_bitmap tests/test_price_bitmap.cpp)
    add_nexus_test(test_matching tests/test_matching.cpp)
    add_nexus_test(test_invariants tests/test_invariants.cpp)
    add_nexus_test(test_differential tests/test_differential.cpp)
    add_nexus_test(test_spsc tests/test_spsc.cpp)
    add_nexus_test(test_wal tests/test_wal.cpp)
endif()

# Benchmarks
if(BUILD_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/bench_main.cpp" AND TARGET nexusmatch_core)
    add_executable(nexusmatch_bench benchmarks/bench_main.cpp)
    target_link_libraries(nexusmatch_bench PRIVATE nexusmatch_core pthread)
endif()
