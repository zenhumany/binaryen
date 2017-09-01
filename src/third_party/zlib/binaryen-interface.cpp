#include "zlib.h"
#include "binaryen-interface.h"

namespace zlib {

size_t getCompressedSize(uint8_t* data, size_t size) {
  unsigned long bound = compressBound(size);
  void* buffer = malloc(bound);
  unsigned long compressedSize;
  compress((unsigned char*)buffer, &compressedSize, data, size);
  free(buffer);
  return compressedSize;
};

}

