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

#include "ApngDecoder.h"
#include <memory>
#include <cmath>
#include "zlib.h"
#include "png.h"
#include "Error.h"
#include "StreamSource.h"
#include "ApngCompose.h"
#include "Log.h"

namespace apng_drawable {

std::unique_ptr<ApngImage> ApngDecoder::decode(
    std::unique_ptr<StreamSource> source,
    int32_t &result
) {
  // Check signature
  LOGV(" | check signature");
  int32_t format_check_result = source->checkPngSignature();
  if (format_check_result < SUCCESS) {
    result = format_check_result;
    return nullptr;
  }

  // Create structure
  LOGV(" | create structure");
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!png_ptr || !info_ptr) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_OUT_OF_MEMORY;
    return nullptr;
  }

  // Point to handle error (Read header and acTL)
  if (setjmp(png_jmpbuf(png_ptr)) != 0) { // NOLINT(cert-err52-cpp)
    result = source->getError();
    if (!result) {
      result = ERR_INVALID_FILE_FORMAT;
    }
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return nullptr;
  }

  // Read header
  LOGV(" | read header");
  source->init(png_ptr);
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
  uint channels = png_get_channels(png_ptr, info_ptr);
  size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);

  // check decode bound
  LOGV(" | check decode bound (w: %d, h: %d)", width, height);
  if (width <= 0 || height <= 0) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_INVALID_FILE_FORMAT;
    return nullptr;
  }
  // check channel (supported only 4 channel apng)
  if (channels != 4) {
    result = ERR_UNSUPPORTED_TYPE;
    return nullptr;
  }

  // Read acTL
  LOGV(" | read acTL");
  png_uint_32 frames = 1;
  png_uint_32 plays = 0;
  bool has_acTL = png_get_acTL(png_ptr, info_ptr, &frames, &plays) != 0;
  LOGV(" | acTL result=%d, frames=%d, plays=%d", has_acTL, frames, plays);
  if (!has_acTL) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_INVALID_FILE_FORMAT;
    return nullptr;
  }

  // Allocate buffers
  LOGV(" | allocate buffers");
  // Check unsigned integer wrapping
  if (height > SIZE_MAX / row_bytes) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_INVALID_FILE_FORMAT;
    return nullptr;
  }
  size_t size = height * row_bytes;
  std::unique_ptr<uint8_t[]> p_frame(new uint8_t[size]());
  std::unique_ptr<uint8_t[]> p_buffer(new uint8_t[size]());
  std::unique_ptr<uint8_t[]> p_previous_frame(new uint8_t[size]());
  // Check unsigned integer wrapping
  if (height > SIZE_MAX / sizeof(png_bytep)) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_INVALID_FILE_FORMAT;
    return nullptr;
  }
  std::unique_ptr<png_bytep[]> rows_frame(new png_bytep[height]);
  std::unique_ptr<png_bytep[]> rows_buffer(new png_bytep[height]);
  if (!p_frame || !p_buffer || !p_previous_frame || !rows_frame || !rows_buffer) {
    LOGV(" | failed to allocate buffers");
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_OUT_OF_MEMORY;
    return nullptr;
  }
  for (uint32_t j = 0; j < height; j++) {
    rows_frame[j] = p_frame.get() + j * row_bytes;
    rows_buffer[j] = p_buffer.get() + j * row_bytes;
  }

  std::unique_ptr<ApngImage> png;
  try {
    png = std::make_unique<ApngImage>(
        width,
        height,
        static_cast<uint32_t>(frames),
        static_cast<uint32_t>(plays)
    );
  } catch (const std::bad_alloc &) {
    LOGV(" | failed to allocate ApngImage due to std::bad_alloc");
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_OUT_OF_MEMORY;
    return nullptr;
  }

  if (!png) {
    LOGV(" | failed to allocate ApngImage");
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    result = ERR_OUT_OF_MEMORY;
    return nullptr;
  }

  // Point to handle error (everything until done decoding)
  if (setjmp(png_jmpbuf(png_ptr)) != 0) { // NOLINT(cert-err52-cpp)
    // set error code
    result = source->getError();
    if (!result) {
      result = ERR_INVALID_FILE_FORMAT;
    }

    // release all
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return nullptr;
  }

  // Read frames
  LOGV(" | read frames");
  png_uint_32 x_offset = 0;
  png_uint_32 y_offset = 0;
  png_uint_32 frame_width = width;
  png_uint_32 frame_height = height;
  uint16_t delay_num = 1;
  uint16_t delay_den = 100;
  uint8_t dispose_op = 0;
  uint8_t blend_op = 0;
  uint32_t first = (png_get_first_frame_is_hidden(png_ptr, info_ptr) != 0) ? 1 : 0;
  for (uint32_t i = 0; i < frames; ++i) {
    // Read fcTL
    LOGV(" | read fcTL (%d)", i);
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
        static_cast<size_t>(std::lround(static_cast<float>(delay_num) / delay_den * 1000.F));
    // Check unsigned integer wrapping by `height * width`.
    if (height > SIZE_MAX / width) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        result = ERR_INVALID_FILE_FORMAT;
        return nullptr;
    }
    size_t pixelCount = height * width; // The frame expects the pixels to be a uint32_t array, so we don't need to multiply by 4
    std::unique_ptr<ApngFrame> frame(new ApngFrame(pixelCount, duration));
    if (i == first) {
      blend_op = PNG_BLEND_OP_SOURCE;
      if (dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
        dispose_op = PNG_DISPOSE_OP_BACKGROUND;
      }
    }

    // Read fdAT or IDAT
    png_read_image(png_ptr, rows_buffer.get());

    // Composite this frame into the running canvas (shared with the streaming decoder)
    composeFrame(rows_frame.get(),
                 rows_buffer.get(),
                 p_frame.get(),
                 p_previous_frame.get(),
                 frame->getRawPixels(),
                 width,
                 height,
                 x_offset,
                 y_offset,
                 frame_width,
                 frame_height,
                 dispose_op,
                 blend_op,
                 size);
    png->setFrame(i, std::move(frame));
  }

  // Finish read
  LOGV(" | finish decode");
  png_read_end(png_ptr, info_ptr);
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  result = SUCCESS;
  return std::move(png);
}

bool ApngDecoder::isApng(std::unique_ptr<StreamSource> source) {
  // Checks PNG signature
  int result = source->checkPngSignature();
  if (result != SUCCESS) {
    return false;
  }

  // Checks APNG acTL chunk
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!png_ptr || !info_ptr) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return false;
  }

  // Point to handle error (Read header and acTL)
  if (setjmp(png_jmpbuf(png_ptr)) != 0) { // NOLINT(cert-err52-cpp)
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return false;
  }

  // Read header
  source->init(png_ptr);
  png_set_sig_bytes(png_ptr, 8);
  png_read_info(png_ptr, info_ptr);

  // Read acTL
  png_uint_32 frames = 0;
  png_uint_32 plays = 0;
  bool has_acTL = png_get_acTL(png_ptr, info_ptr, &frames, &plays) != 0;
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  return has_acTL;
}
}
