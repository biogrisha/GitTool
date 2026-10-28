cmake_minimum_required(VERSION 3.21)

project(GitTool VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

find_package(libgit2 CONFIG REQUIRED)

set(
    TREE_SITTER_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/tree-sitter"
    CACHE PATH
    "Path to the Tree-sitter source checkout"
)

if(NOT EXISTS "${TREE_SITTER_SOURCE_DIR}/lib/src/lib.c")
    message(FATAL_ERROR "Tree-sitter source not found at ${TREE_SITTER_SOURCE_DIR}")
endif()

add_library(tree_sitter_runtime STATIC
    "${TREE_SITTER_SOURCE_DIR}/lib/src/lib.c"
)

target_include_directories(tree_sitter_runtime
    PUBLIC
        "${TREE_SITTER_SOURCE_DIR}/lib/include"
    PRIVATE
        "${TREE_SITTER_SOURCE_DIR}/lib/src"
)

set_target_properties(tree_sitter_runtime PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

FetchContent_Declare(
    tree_sitter_cpp_source
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(tree_sitter_cpp_source)
if(NOT tree_sitter_cpp_source_POPULATED)
    FetchContent_Populate(tree_sitter_cpp_source)
endif()

add_library(tree_sitter_cpp_grammar STATIC
    "${tree_sitter_cpp_source_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_cpp_source_SOURCE_DIR}/src/scanner.c"
)

target_include_directories(tree_sitter_cpp_grammar
    PRIVATE
        "${tree_sitter_cpp_source_SOURCE_DIR}/src"
)

add_executable(gittool
    src/CodeParser.cpp
    src/main.cpp
    src/SourceDiff.cpp
    src/TreeDiff.cpp
)

target_compile_features(gittool PRIVATE cxx_std_17)
target_link_libraries(gittool
    PRIVATE
        libgit2::libgit2package
        tree_sitter_runtime
        tree_sitter_cpp_grammar
)
