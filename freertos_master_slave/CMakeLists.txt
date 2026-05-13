cmake_minimum_required(VERSION 3.18)
project(freertos_master_slave C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler flags ─────────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter   # suppress warnings in FreeRTOS portable-layer headers
)

# ── FreeRTOS port selection ────────────────────────────────────────────────
if(UNIX)
    set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)
    set(FREERTOS_HEAP "3"         CACHE STRING "" FORCE)  # heap_3: uses system malloc/free
elseif(WIN32)
    set(FREERTOS_PORT "MSVC_MingW" CACHE STRING "" FORCE)
    set(FREERTOS_HEAP "4"          CACHE STRING "" FORCE)  # heap_4: internal heap allocator
else()
    message(FATAL_ERROR "Unsupported platform — add a FREERTOS_PORT entry for this OS")
endif()

# FreeRTOSConfig.h is in the project root; tell the kernel where to find it.
set(FREERTOS_CONFIG_FILE_DIRECTORY "${CMAKE_SOURCE_DIR}" CACHE STRING "" FORCE)

# ── Fetch FreeRTOS-Kernel ──────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        V11.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(freertos_kernel)

# ── libstatemachine — pure C, no FreeRTOS dependency ──────────────────────
# Compiled with BUILD_TESTS so FreeRTOS task functions and headers are
# excluded.  Linked by the unit-test target; never by freertos_sim.
add_library(statemachine STATIC
    src/device_a.c
    src/device_b.c
)
target_include_directories(statemachine PUBLIC src)
# PUBLIC: consumers (freertos_tests) also see BUILD_TESTS without repeating it.
target_compile_definitions(statemachine PUBLIC BUILD_TESTS)

# ── freertos_sim — main simulation executable ─────────────────────────────
add_executable(freertos_sim
    src/main.c
    src/ipc.c
    src/logger.c
    src/device_a.c
    src/device_b.c
)
target_include_directories(freertos_sim PRIVATE src)
target_link_libraries(freertos_sim PRIVATE freertos_kernel)
if(UNIX)
    target_link_libraries(freertos_sim PRIVATE pthread)
endif()

# ── Unit tests — Unity framework ──────────────────────────────────────────
option(BUILD_TESTS "Build and run Unity unit tests" OFF)

if(BUILD_TESTS)
    FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG        v2.5.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(unity)

    add_executable(freertos_tests
        tests/test_device_a_sm.c
        tests/test_device_b_sm.c
        tests/test_ipc_types.c
    )
    # statemachine's PUBLIC BUILD_TESTS propagates to freertos_tests, so all
    # headers included by test files also see BUILD_TESTS automatically.
    target_link_libraries(freertos_tests PRIVATE statemachine unity)

    enable_testing()
    add_test(NAME unit_tests COMMAND freertos_tests)
endif()
