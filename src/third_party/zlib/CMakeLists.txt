SET(zlib_SOURCES
  adler32.c
  binaryen-interface.cpp
  compress.c
  crc32.c
  deflate.c
  trees.c
  zutil.c
)
ADD_LIBRARY(zlib STATIC ${zlib_SOURCES})
TARGET_LINK_LIBRARIES(zlib ${CMAKE_THREAD_LIBS_INIT})
