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

#include "ApngStreamDecoder.h"
#include <cmath>
#include <cstring>
#include "png.h"
#include "Error.h"
#include "ApngImage.h"   // PNG_SIG_SIZE
#include "ApngCompose.h"
#include "Log.h"

namespace apng_drawable {

ApngStreamDecoder::~ApngStreamDecoder() {
  closeSession();
}

std::unique_ptr<ApngStreamDecoder> ApngStreamDecoder::create(
    std::vector<uint8_t> encoded,
    int32_t &result
) {
  auto decoder = std::unique_ptr<ApngStreamDecoder>(new ApngStreamDecoder());
  decoder->mEncoded = std::move(encoded);
  decoder->mSource = std::make_unique<MemoryStreamSource>(
      decoder->mEncoded.data(),
      decoder->mEncoded.size()
  );

  int32_t r = decoder->preScan();
  if (r != SUCCESS) {
    result = r;
    return nullptr;
  }

  r = decoder->openSession();
  if (r != SUCCESS) {
    result = r;
    return nullptr;
  }

  result = SUCCESS;
  return decoder;
}

uint64_t ApngStreamDecoder::getAllFrameByteCount() const {
  return static_cast<uint64_t>(mCanvasSize) * 3
      + static_cast<uint64_t>(getFrameByteCount())
      + static_cast<uint64_t>(mEncoded.size());
}

void ApngStreamDecoder::closeSession() {
  if (mPngPtr) {
    png_destroy_read_struct(&mPngPtr, &mInfoPtr, nullptr);
    mPngPtr = nullptr;
    mInfoPtr = nullptr;
  }
}

int32_t ApngStreamDecoder::preScan() {
  mSource->reset();
  int32_t sig = mSource->checkPngSignature();
  if (sig != SUCCESS) {
    return sig;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!png_ptr || !info_ptr) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return ERR_OUT_OF_MEMORY;
  }

  if (setjmp(png_jmpbuf(png_ptr)) != 0) { // NOLINT(cert-err52-cpp)
    int32_t e = mSource->getError();
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return e ? e : ERR_INVALID_FILE_FORMAT;
  }

  mSource->init(png_ptr);
  png_set_sig_bytes(png_ptr, PNG_SIG_SIZE);
  png_read_info(png_ptr, info_ptr);

  png_set_expand(png_ptr);
  png_set_strip_16(png_ptr);
  png_set_gray_to_rgb(png_ptr);
  png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
  png_set_interlace_handling(png_ptr);
  png_read_update_info(png_ptr, info_ptr);

  auto width = static_cast<uint32_t>(png_get_image_width(png_ptr, info_ptr));
  auto height = static_cast<uint32_t>(png_get_image_height(png_ptr, info_ptr));
  uint32_t channels = png_get_channels(png_ptr, info_ptr);
  size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);

  if (width == 0 || height == 0) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return ERR_INVALID_FILE_FORMAT;
  }
  if (channels != 4) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return ERR_UNSUPPORTED_TYPE;
  }
  // Check unsigned integer wrapping by `height * row_bytes` and `height * width`.
  if (row_bytes != 0 && height > SIZE_MAX / row_bytes) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return ERR_INVALID_FILE_FORMAT;
  }
  if (height > SIZE_MAX / width) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return ERR_INVALID_FILE_FORMAT;
  }

  png_uint_32 frames = 1;
  png_uint_32 plays = 0;
  bool has_acTL = png_get_acTL(png_ptr, info_ptr, &frames, &plays) != 0;
  if (!has_acTL) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return ERR_INVALID_FILE_FORMAT;
  }

  mWidth = width;
  mHeight = height;
  mFrameCount = static_cast<uint32_t>(frames);
  mLoopCount = static_cast<uint32_t>(plays);
  mRowBytes = row_bytes;
  mCanvasSize = height * row_bytes;

  // Allocate the running canvas once; openSession / composeNext reuse it.
  mPFrame.reset(new(std::nothrow) uint8_t[mCanvasSize]());
  mPBuffer.reset(new(std::nothrow) uint8_t[mCanvasSize]());
  mPPreviousFrame.reset(new(std::nothrow) uint8_t[mCanvasSize]());
  mRowsFrame.reset(new(std::nothrow) png_bytep[height]);
  mRowsBuffer.reset(new(std::nothrow) png_bytep[height]);
  mDisplayPixels.reset(new(std::nothrow) uint32_t[static_cast<size_t>(width) * height]);
  if (!mPFrame || !mPBuffer || !mPPreviousFrame || !mRowsFrame || !mRowsBuffer
      || !mDisplayPixels) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return ERR_OUT_OF_MEMORY;
  }
  for (uint32_t j = 0; j < height; j++) {
    mRowsFrame[j] = mPFrame.get() + j * row_bytes;
    mRowsBuffer[j] = mPBuffer.get() + j * row_bytes;
  }

  // Walk every fcTL recording only the delay; pixel data is read into the
  // throwaway buffer and discarded.
  mDurations.clear();
  mDurations.reserve(frames);
  png_uint_32 frame_width = width;
  png_uint_32 frame_height = height;
  png_uint_32 x_offset = 0;
  png_uint_32 y_offset = 0;
  uint16_t delay_num = 1;
  uint16_t delay_den = 100;
  uint8_t dispose_op = 0;
  uint8_t blend_op = 0;
  for (uint32_t i = 0; i < frames; ++i) {
    png_read_frame_head(png_ptr, info_ptr);
    png_get_next_frame_fcTL(png_ptr,
                            info_ptr,
                            &frame_width,
                            &frame_height,
                            &x_offset,
                            &y_offset,
                            &delay_num,
                            &delay_den,
                            &dispose_op,
                            &blend_op);
    auto duration =
        static_cast<uint32_t>(std::lround(static_cast<float>(delay_num) / delay_den * 1000.F));
    mDurations.push_back(duration);
    png_read_image(png_ptr, mRowsBuffer.get());
  }

  png_read_end(png_ptr, info_ptr);
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  return SUCCESS;
}

int32_t ApngStreamDecoder::openSession() {
  closeSession();
  mSource->reset();
  int32_t sig = mSource->checkPngSignature();
  if (sig != SUCCESS) {
    return sig;
  }

  mPngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  mInfoPtr = png_create_info_struct(mPngPtr);
  if (!mPngPtr || !mInfoPtr) {
    closeSession();
    return ERR_OUT_OF_MEMORY;
  }

  if (setjmp(png_jmpbuf(mPngPtr)) != 0) { // NOLINT(cert-err52-cpp)
    int32_t e = mSource->getError();
    closeSession();
    return e ? e : ERR_INVALID_FILE_FORMAT;
  }

  mSource->init(mPngPtr);
  png_set_sig_bytes(mPngPtr, PNG_SIG_SIZE);
  png_read_info(mPngPtr, mInfoPtr);

  png_set_expand(mPngPtr);
  png_set_strip_16(mPngPtr);
  png_set_gray_to_rgb(mPngPtr);
  png_set_add_alpha(mPngPtr, 0xff, PNG_FILLER_AFTER);
  png_set_interlace_handling(mPngPtr);
  png_read_update_info(mPngPtr, mInfoPtr);

  mFirst = (png_get_first_frame_is_hidden(mPngPtr, mInfoPtr) != 0) ? 1 : 0;

  // Reset the canvas to fully transparent so frame 0 composes correctly.
  memset(mPFrame.get(), 0, mCanvasSize);
  mCurrentIndex = -1;
  return SUCCESS;
}

int32_t ApngStreamDecoder::composeNext() {
  if (mCurrentIndex + 1 >= static_cast<int64_t>(mFrameCount)) {
    return ERR_FRAME_INDEX_OUT_OF_RANGE;
  }
  if (!mPngPtr) {
    int32_t r = openSession();
    if (r != SUCCESS) {
      return r;
    }
  }

  if (setjmp(png_jmpbuf(mPngPtr)) != 0) { // NOLINT(cert-err52-cpp)
    int32_t e = mSource->getError();
    closeSession();
    return e ? e : ERR_INVALID_FILE_FORMAT;
  }

  auto i = static_cast<uint32_t>(mCurrentIndex + 1);
  png_uint_32 x_offset = 0;
  png_uint_32 y_offset = 0;
  png_uint_32 frame_width = mWidth;
  png_uint_32 frame_height = mHeight;
  uint16_t delay_num = 1;
  uint16_t delay_den = 100;
  uint8_t dispose_op = 0;
  uint8_t blend_op = 0;

  png_read_frame_head(mPngPtr, mInfoPtr);
  png_get_next_frame_fcTL(mPngPtr,
                          mInfoPtr,
                          &frame_width,
                          &frame_height,
                          &x_offset,
                          &y_offset,
                          &delay_num,
                          &delay_den,
                          &dispose_op,
                          &blend_op);

  if (i == mFirst) {
    blend_op = PNG_BLEND_OP_SOURCE;
    if (dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
      dispose_op = PNG_DISPOSE_OP_BACKGROUND;
    }
  }

  png_read_image(mPngPtr, mRowsBuffer.get());

  composeFrame(mRowsFrame.get(),
               mRowsBuffer.get(),
               mPFrame.get(),
               mPPreviousFrame.get(),
               mDisplayPixels.get(),
               mWidth,
               mHeight,
               x_offset,
               y_offset,
               frame_width,
               frame_height,
               dispose_op,
               blend_op,
               mCanvasSize);

  mCurrentIndex = i;

  // Once the last frame is consumed close the session; a later seek will
  // recreate it (reset/loop-wrap), and this lets png_read_end run cleanly.
  if (i + 1 == mFrameCount) {
    png_read_end(mPngPtr, mInfoPtr);
    closeSession();
  }
  return SUCCESS;
}

int32_t ApngStreamDecoder::seekTo(uint32_t target) {
  if (target >= mFrameCount) {
    return ERR_FRAME_INDEX_OUT_OF_RANGE;
  }
  if (static_cast<int64_t>(target) == mCurrentIndex) {
    return SUCCESS;
  }
  // Backward seek (incl. loop-wrap) rewinds to before frame 0, then steps forward.
  if (static_cast<int64_t>(target) < mCurrentIndex) {
    int32_t r = openSession();
    if (r != SUCCESS) {
      return r;
    }
  }
  while (mCurrentIndex < static_cast<int64_t>(target)) {
    int32_t r = composeNext();
    if (r != SUCCESS) {
      return r;
    }
  }
  return SUCCESS;
}

void ApngStreamDecoder::blitInto(uint32_t *dst) {
  if (!dst || !mDisplayPixels) {
    return;
  }
  memcpy(dst, mDisplayPixels.get(), static_cast<size_t>(mWidth) * mHeight * sizeof(uint32_t));
}
}
