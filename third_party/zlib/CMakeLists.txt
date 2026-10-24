# Pinned, offline subset. Do not import upstream build settings into Goat.
add_library(goat_zlib STATIC
    adler32.c crc32.c deflate.c inffast.c inflate.c inftrees.c trees.c zutil.c)
target_include_directories(goat_zlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(goat_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(goat_minizip STATIC
    contrib/minizip/zip.c contrib/minizip/unzip.c contrib/minizip/ioapi.c)
target_include_directories(goat_minizip PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contrib/minizip")
target_link_libraries(goat_minizip PUBLIC goat_zlib)
target_compile_definitions(goat_minizip PRIVATE NOCRYPT NOUNCRYPT USE_FILE32API)
set_target_properties(goat_minizip PROPERTIES POSITION_INDEPENDENT_CODE ON)
