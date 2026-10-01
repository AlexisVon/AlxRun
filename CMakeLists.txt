cmake_minimum_required(VERSION 3.19)
project(AlxRun VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Compiler / standard
# ---------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type - use empty to avoid adding default flags
set(CMAKE_BUILD_TYPE "" CACHE STRING "" FORCE)

# Optimization level
set(ALXRUN_OPT_LEVEL "2" CACHE STRING "Optimization level (0/1/2/3)")
set_property(CACHE ALXRUN_OPT_LEVEL PROPERTY STRINGS 0 1 2 3)
add_compile_options(-O${ALXRUN_OPT_LEVEL})

# Split debug info
option(ALXRUN_SPLIT_DEBUG "Separate debug info into .debug files" OFF)
if(ALXRUN_SPLIT_DEBUG)
    add_compile_options(-g)
endif()

# Fully static link: the shipped binary then depends on nothing but the kernel (the engine's
# dlopen path sits behind the link gate and is unreachable, so the static-dlopen caveat is inert).
option(ALXRUN_STATIC "Link the binary fully static" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# ---------------------------------------------------------------------------
# What a build *is* — its dimensions, capability switches and fs rules — lives in
# src/config.h, one config point (design.md §6). Nothing is set here, so the tree and a build
# script cannot disagree, and a value cannot survive a package swap in the CMake cache.
# What stays here is how it is compiled: optimization, debug split, the AlxLib paths.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# AlxLib paths — vendored, static only.
# Static linking is deliberate: a dynamic build needs LD_LIBRARY_PATH or an
# rpath, and an env prefix breaks the client's rule matching (design.md §1).
# ---------------------------------------------------------------------------
set(ALXLIB_DIR ${CMAKE_SOURCE_DIR}/lib/alxlib)
set(ALXLIB_INC ${ALXLIB_DIR}/include)
set(ALXLIB_LIB ${ALXLIB_DIR}/lib)

# ---------------------------------------------------------------------------
# alxrun — single binary, three faces over one kernel
#   mcp  → MCP server on stdio (product face)
#   -e / file → CLI debug face
# ---------------------------------------------------------------------------
# Kernel + registry + the language-layer tables: shared with the fuzz harness, which must
# fuzz the same program the user runs (a harness on a different build proves nothing).
set(ALXRUN_KERNEL_SOURCES
    src/kernel/kernel.cpp
    src/registry/registry.cpp
    src/capability/type.cpp
    src/capability/notation.cpp
    src/capability/func.cpp
)
# Every module is always compiled; which ones reach the binary is config.h's business (each file
# guards its own body with #if). A module switched off contributes an empty translation unit —
# no code, no symbols, no descriptors — and the switch has exactly one home.
set(ALXRUN_CAP_SOURCES
    src/capability/str.cpp
    src/capability/vec.cpp
    src/capability/map.cpp
    src/capability/io.cpp
    src/capability/math.cpp
    src/capability/hash.cpp
    src/capability/regex.cpp
    src/capability/json.cpp
    src/capability/fs.cpp
    src/capability/util.cpp
)

set(ALXRUN_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mcp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/registry
    ${CMAKE_CURRENT_SOURCE_DIR}/src/capability
    ${ALXLIB_INC}/alxbase
    ${ALXLIB_INC}/alxcore
    ${ALXLIB_INC}/alxscpt
    ${ALXLIB_INC}/alxcomm
)

add_executable(alxrun
    src/cli/main.cpp
    src/cli/install.cpp
    src/mcp/mcp.cpp
    src/mcp/supervisor.cpp
    ${ALXRUN_KERNEL_SOURCES}
)

# ---------------------------------------------------------------------------
# Capability modules — the switches live in src/config.h (design.md §6); here they are
# merely compiled, and a switched-off module compiles to nothing.
# ---------------------------------------------------------------------------
target_sources(alxrun PRIVATE ${ALXRUN_CAP_SOURCES})
target_include_directories(alxrun PRIVATE ${ALXRUN_INCLUDE_DIRS})

target_link_libraries(alxrun PRIVATE
    ${ALXLIB_LIB}/libalxlib.a
    pthread
    dl
    m
)
if(ALXRUN_STATIC)
    target_link_options(alxrun PRIVATE -static)
endif()

# The version lives in project() above, one source; the CLI's -v and the MCP serverInfo print it
# from here rather than from a second copy.
target_compile_definitions(alxrun PRIVATE ALXRUN_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Split debug info
# ---------------------------------------------------------------------------
if(ALXRUN_SPLIT_DEBUG)
    add_custom_command(TARGET alxrun POST_BUILD
        COMMAND objcopy --only-keep-debug $<TARGET_FILE:alxrun> $<TARGET_FILE:alxrun>.debug
        COMMAND objcopy --strip-debug $<TARGET_FILE:alxrun>
        COMMAND objcopy --add-gnu-debuglink=$<TARGET_FILE:alxrun>.debug $<TARGET_FILE:alxrun>
        COMMENT "Splitting debug info for alxrun"
    )
endif()

# ---------------------------------------------------------------------------
# Fuzz harness (test/fuzz) — libFuzzer + ASan/UBSan over the same kernel, registry
# and capability set as the shipped binary (todo.md P5-3). Separate target and a
# separate output directory: bin/alxrun is never touched by a fuzz build.
# The engine must be instrumented too, so it links an AlxLib built with clang and
# the sanitizers — test/fuzz/run.sh builds both and passes the path in.
# ---------------------------------------------------------------------------
option(ALXRUN_FUZZ "Build the libFuzzer harness (test/fuzz)" OFF)
if(ALXRUN_FUZZ)
    set(ALXRUN_FUZZ_ALXLIB "" CACHE FILEPATH "clang+sanitizer libalxlib.a (test/fuzz/run.sh builds it)")
    if(NOT EXISTS ${ALXRUN_FUZZ_ALXLIB})
        message(FATAL_ERROR "ALXRUN_FUZZ=ON needs -DALXRUN_FUZZ_ALXLIB=<path>; test/fuzz/run.sh builds it")
    endif()
    add_executable(alxrun_fuzz
        test/fuzz/fuzz_main.cpp
        ${ALXRUN_KERNEL_SOURCES}
        ${ALXRUN_CAP_SOURCES}
    )
    # A fuzz input that loops costs the wall clock and nothing else: one second, the shortest window
    # the engine still explores the slow families (regex backtracking) in. This define is the whole
    # difference between the two builds — config.h branches on it (no memory wall, that window).
    target_compile_definitions(alxrun_fuzz PRIVATE ALXRUN_FUZZ_BUILD)
    target_include_directories(alxrun_fuzz PRIVATE ${ALXRUN_INCLUDE_DIRS})
    target_link_libraries(alxrun_fuzz PRIVATE ${ALXRUN_FUZZ_ALXLIB} pthread dl m)
    target_compile_options(alxrun_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -O1 -g)
    target_link_options(alxrun_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    set_target_properties(alxrun_fuzz PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
