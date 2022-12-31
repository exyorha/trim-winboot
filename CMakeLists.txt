cmake_minimum_required(VERSION 3.20)
project(trim-winboot)

enable_language(ASM_NASM)

find_package(PkgConfig REQUIRED)
pkg_check_modules(lz4 REQUIRED IMPORTED_TARGET liblz4)

include(CheckIncludeFile)
include(CheckLibraryExists)

CHECK_INCLUDE_FILE(x86emu.h X86EMU_HEADER_AVAILABLE)
CHECK_LIBRARY_EXISTS(x86emu x86emu_new "" X86EMU_LIBRARY_AVAILABLE)

if(NOT X86EMU_HEADER_AVAILABLE OR NOT X86EMU_LIBRARY_AVAILABLE)
    message(SEND_ERROR "libx86emu is required. Check that x86emu.h and libx86emu are available")
endif()

add_executable(makebin EXCLUDE_FROM_ALL makebin.cpp)

set_target_properties(makebin PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED TRUE
)
add_executable(trim-winboot
    CMDecompressor.cpp
    CMDecompressor.h
    CompressionStream.cpp
    CompressionStream.h
    DOSTypes.h
    main.cpp
    WinbootImage.cpp
    WinbootImage.h
    ${CMAKE_CURRENT_BINARY_DIR}/msload_extension.h
)

set_target_properties(trim-winboot PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED TRUE
)
target_link_libraries(trim-winboot PRIVATE PkgConfig::lz4 x86emu)
target_include_directories(trim-winboot PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/msload_extension.bin
        ${CMAKE_CURRENT_BINARY_DIR}/msload_extension.h
    COMMAND
        ${CMAKE_ASM_NASM_COMPILER}
        -fbin
        -o ${CMAKE_CURRENT_BINARY_DIR}/msload_extension.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/msload_extension.asm
    COMMAND
        makebin
        ${CMAKE_CURRENT_BINARY_DIR}/msload_extension.bin
        ${CMAKE_CURRENT_BINARY_DIR}/msload_extension.h
        msload_extension
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/msload_extension.asm
    DEPENDS $<TARGET_FILE:makebin>
    VERBATIM
)
