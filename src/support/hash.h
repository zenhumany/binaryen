/*
 * Copyright 2015 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_support_hash_h
#define wasm_support_hash_h

#include <stdint.h>

namespace wasm {

typedef uint32_t HashResult;

// TODO: optimize (for x already being a hash, and y being 8 or 17 bits)
inline HashResult rehash(HashResult x, HashResult y) { // see http://www.cse.yorku.ca/~oz/hash.html
  HashResult hash = 5381;
  while (x) {
    hash = ((hash << 5) + hash) ^ (x & 0xff);
    x >>= 8;
  }
  while (y) {
    hash = ((hash << 5) + hash) ^ (y & 0xff);
    y >>= 8;
  }
  return hash;
}

} // namespace wasm

#endif // wasm_support_hash_h
