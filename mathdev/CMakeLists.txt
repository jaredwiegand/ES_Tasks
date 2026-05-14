cmake_minimum_required(VERSION 3.18)
project(mathdev
    VERSION     1.0.0
    DESCRIPTION "Linux kernel math chardev + IPC server + clients"
    LANGUAGES   C
)

# ── Options ──────────────────────────────────────────────────────────────────
option(BUILD_C_CLIENT  "Build the C client"             ON)
option(BUILD_TESTS     "Build basic smoke-tests"        OFF)

# ── Global C standard ────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD   11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── cJSON (fetched automatically if not found on system) ─────────────────────
find_package(cJSON QUIET)
if(NOT cJSON_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG        v1.7.17
    )
    set(ENABLE_CJSON_TEST      OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
    # cJSON's CMakeLists.txt uses cmake_minimum_required < 3.5; suppress the
    # resulting CMake deprecation warning since it comes from the dependency.
    set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(cjson)
    set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)
endif()

# ── C client ─────────────────────────────────────────────────────────────────
if(BUILD_C_CLIENT)
    add_executable(mathdev-client
        client_c/client.c
    )

    target_compile_options(mathdev-client PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )

    target_link_libraries(mathdev-client PRIVATE cjson)
    if(NOT cJSON_FOUND)
        target_include_directories(mathdev-client PRIVATE ${cjson_SOURCE_DIR})
    endif()

    target_compile_definitions(mathdev-client PRIVATE
        DEFAULT_SOCKET_PATH="/tmp/mathdev.sock"
    )

    install(TARGETS mathdev-client DESTINATION bin)
endif()

# ── Kernel module (custom target – delegates to kbuild) ──────────────────────
set(KERNEL_DIR "/lib/modules/${CMAKE_SYSTEM_VERSION}/build"
    CACHE PATH "Kernel build directory")

if(EXISTS "${KERNEL_DIR}")
    add_custom_target(kernel_module ALL
        COMMAND ${CMAKE_MAKE_PROGRAM}
                -C ${KERNEL_DIR}
                M=${CMAKE_CURRENT_SOURCE_DIR}/kernel
                modules
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/kernel
        COMMENT "Building mathdev.ko kernel module"
        VERBATIM
    )

    add_custom_target(kernel_clean
        COMMAND ${CMAKE_MAKE_PROGRAM}
                -C ${KERNEL_DIR}
                M=${CMAKE_CURRENT_SOURCE_DIR}/kernel
                clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/kernel
        COMMENT "Cleaning kernel module build artefacts"
        VERBATIM
    )

    add_custom_target(kernel_load
        COMMAND sudo insmod "${CMAKE_CURRENT_SOURCE_DIR}/kernel/mathdev.ko"
        COMMENT "Loading mathdev kernel module"
        VERBATIM
    )

    add_custom_target(kernel_unload
        COMMAND sudo rmmod mathdev || true
        COMMENT "Unloading mathdev kernel module"
        VERBATIM
    )
else()
    message(STATUS
        "Kernel build directory not found at ${KERNEL_DIR} -- "
        "kernel module will not be built. "
        "Install linux-headers-$(uname -r) or set -DKERNEL_DIR=<path>."
    )
endif()

# ── Summary ──────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "mathdev build configuration:")
message(STATUS "  C client     : ${BUILD_C_CLIENT}")
message(STATUS "  Build tests  : ${BUILD_TESTS}")
message(STATUS "  Kernel dir   : ${KERNEL_DIR}")
message(STATUS "")
