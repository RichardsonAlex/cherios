
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(SRCS
    ${INIT_ASM}
    src/main.c
    src/zlib.c
)

add_cherios_executable(zlib_test ADD_TO_FILESYSTEM LINKER_SCRIPT sandbox.ld SOURCES ${SRCS})
