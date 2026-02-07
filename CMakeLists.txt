cmake_minimum_required(VERSION 3.13)
project(ngmips)

set(CMAKE_CXX_STANDARD 20)
set(TARGET_LIB ngmips-lib)

include(FetchContent)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        a33701196adfad74917046096bf5a2aa0ab0bb50) # 9.1.0
FetchContent_MakeAvailable(fmt)

set(MIPS_SOURCES
    src/mips_base.cpp
    src/mips_cop0.cpp
    src/mips_tlb_normal.cpp
    src/mips_fpu.cpp
    src/mips_cache.cpp
    src/mips_decode.cpp
)

add_library(${TARGET_LIB} STATIC ${MIPS_SOURCES})

target_include_directories(${TARGET_LIB} PRIVATE
    util
)

target_include_directories(${TARGET_LIB} PUBLIC
    include
    external/unordered_dense/include
)

# fmt is provided by parent CMakeLists.txt via FetchContent
target_link_libraries(${TARGET_LIB} fmt::fmt)

install (TARGETS ${TARGET_LIB} DESTINATION .)