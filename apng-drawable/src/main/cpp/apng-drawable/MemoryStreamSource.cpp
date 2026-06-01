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

#include "MemoryStreamSource.h"
#include <cstring>
#include "Error.h"
#include "ApngImage.h"

namespace apng_drawable {

MemoryStreamSource::MemoryStreamSource(const uint8_t *data, size_t size)
    : mData(data), mSize(size), mCursor(0), mError(SUCCESS) {
}

int32_t MemoryStreamSource::checkPngSignature() {
  if (mCursor + PNG_SIG_SIZE > mSize) {
    return ERR_UNEXPECTED_EOF;
  }
  if (png_sig_cmp(reinterpret_cast<png_const_bytep>(mData + mCursor), 0, PNG_SIG_SIZE) != 0) {
    return ERR_INVALID_FILE_FORMAT;
  }
  mCursor += PNG_SIG_SIZE;
  return SUCCESS;
}

void MemoryStreamSource::init(png_structp png) {
  png_set_read_fn(png, this, reader);
}

void MemoryStreamSource::reader(png_structp png, png_bytep data, png_size_t length) {
  auto *source = static_cast<MemoryStreamSource *>(png_get_io_ptr(png));
  if (source->mCursor + length > source->mSize) {
    source->mError = ERR_UNEXPECTED_EOF;
    png_error(png, "");
    return;
  }
  memcpy(data, source->mData + source->mCursor, length);
  source->mCursor += length;
}
}
