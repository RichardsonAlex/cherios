
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(FATFS_SRCS
    ${INIT_ASM}
    src/main.c
    src/diskio.c
    src/ff.c
    src/ff_sync.c
)

add_cherios_executable(fatfs ADD_TO_FILESYSTEM LINKER_SCRIPT sandbox.ld SOURCES ${FATFS_SRCS})
