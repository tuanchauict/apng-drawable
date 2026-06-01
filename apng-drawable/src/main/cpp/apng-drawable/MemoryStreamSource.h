//
// Copyright 2018 LINE Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <cstdint>
#include <cstddef>
#include "png.h"
#include "Error.h"

namespace apng_drawable {

/**
 * A libpng read source backed by an in-native byte buffer with a resettable
 * cursor.
 *
 * Unlike {@link StreamSource}, which wraps a one-shot Java `InputStream` and
 * cannot replay, this source serves libpng's read callback from a buffer that
 * has already been read fully into native memory. `reset()` rewinds the cursor
 * to the start so the streaming decoder can re-decode from frame 0 when looping
 * or seeking backward.
 *
 * The buffer is **not owned** by this source; the caller (the streaming
 * decoder) owns the bytes and must keep them alive for the source's lifetime.
 */
class MemoryStreamSource {
 public:
  MemoryStreamSource(const uint8_t *data, size_t size);
  MemoryStreamSource() = delete;
  ~MemoryStreamSource() = default;

  /**
   * Reads and validates the PNG signature at the current cursor, advancing past
   * it. Returns SUCCESS or an ERR_* code.
   */
  int32_t checkPngSignature();

  /** Wires this source into `png` as its read function. */
  void init(png_structp png);

  int32_t getError() { return mError; }

  /** Rewinds the cursor to the start of the buffer and clears any error. */
  void reset() {
    mCursor = 0;
    mError = SUCCESS;
  }

  size_t position() const { return mCursor; }

 private:
  const uint8_t *mData;
  size_t mSize;
  size_t mCursor;
  int32_t mError;

  static void reader(png_structp png, png_bytep data, png_size_t length);

  // Do not copy this object.
  MemoryStreamSource(const MemoryStreamSource &ref);
  MemoryStreamSource &operator=(const MemoryStreamSource &ref);
};
}
