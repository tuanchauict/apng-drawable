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

#include "ApngCompose.h"
#include <cstring>
#include "ApngDecoder.h"

namespace apng_drawable {

void saveFrame(uint32_t *destination,
               uint32_t **source,
               uint32_t const width,
               uint32_t const height) {
  if (!destination) {
    return;
  }
  uint_fast8_t alpha;
  uint32_t src_color;
  // default
  for (uint32_t j = 0; j < height; ++j, destination += width) {
    memcpy(destination, source[j], width * CHANNEL_4_BYTE_SIZE);
    for (uint32_t i = 0; i < width; ++i) {
      // pre multiply color
      src_color = destination[i];
      alpha = static_cast<uint_fast8_t>((src_color >> 24U) & 0xFFU);
      if (alpha == ALPHA_TRANSPARENT) {
        // transparent
        destination[i] = 0;
        continue;
      }
      if (alpha == ALPHA_OPAQUE) {
        // opaque
        continue;
      }
      // translucent
      destination[i] = abgr(
          alpha,
          div255Round(src_color >> 16U & 0xFFU, alpha),
          div255Round(src_color >> 8U & 0xFFU, alpha),
          div255Round(src_color & 0xFFU, alpha));
    }
  }
}

void blendOver(uint8_t **destination,
               uint8_t **const source,
               const png_uint_32 x_offset,
               const png_uint_32 y_offset,
               const png_uint_32 width,
               const png_uint_32 height) {
  for (uint32_t j = 0; j < height; ++j) {
    uint8_t *sp = source[j];
    uint8_t *dp = destination[j + y_offset] + x_offset * CHANNEL_4_BYTE_SIZE;
    uint8_t sourceAlpha;
    for (uint32_t i = 0; i < width; ++i, sp += CHANNEL_4_BYTE_SIZE, dp += CHANNEL_4_BYTE_SIZE) {
      sourceAlpha = sp[3];
      if (sourceAlpha == ALPHA_OPAQUE) {
        memcpy(dp, sp, CHANNEL_4_BYTE_SIZE);
      } else if (sourceAlpha != ALPHA_TRANSPARENT) {
        if (dp[3] != ALPHA_TRANSPARENT) {
          int32_t u = sourceAlpha * ALPHA_OPAQUE;
          int32_t v = (ALPHA_OPAQUE - sourceAlpha) * dp[3];
          int32_t al =
              ALPHA_OPAQUE * ALPHA_OPAQUE - (ALPHA_OPAQUE - sourceAlpha) * (ALPHA_OPAQUE - dp[3]);
          dp[0] = static_cast<uint8_t>((sp[0] * u + dp[0] * v) / al);
          dp[1] = static_cast<uint8_t>((sp[1] * u + dp[1] * v) / al);
          dp[2] = static_cast<uint8_t>((sp[2] * u + dp[2] * v) / al);
          dp[3] = static_cast<uint8_t>(al / ALPHA_OPAQUE);
        } else {
          memcpy(dp, sp, CHANNEL_4_BYTE_SIZE);
        }
      }
    }
  }
}

void blendSource(uint8_t **destination,
                 uint8_t **const source,
                 png_uint_32 x_offset,
                 png_uint_32 y_offset,
                 png_uint_32 width,
                 png_uint_32 height) {
  for (uint32_t j = 0; j < height; j++) {
    memcpy(destination[j + y_offset] + x_offset * CHANNEL_4_BYTE_SIZE,
           source[j],
           width * CHANNEL_4_BYTE_SIZE);
  }
}

void composeFrame(png_bytep *rows_frame,
                  png_bytep *rows_buffer,
                  uint8_t *p_frame,
                  uint8_t *p_previous_frame,
                  uint32_t *out_pixels,
                  uint32_t width,
                  uint32_t height,
                  png_uint_32 x_offset,
                  png_uint_32 y_offset,
                  png_uint_32 frame_width,
                  png_uint_32 frame_height,
                  uint8_t dispose_op,
                  uint8_t blend_op,
                  size_t size) {
  // Process dispose operation
  if (dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
    memcpy(p_previous_frame, p_frame, size);
  }

  // Process blend operation
  if (blend_op == PNG_BLEND_OP_OVER) {
    blendOver(rows_frame, rows_buffer, x_offset, y_offset, frame_width, frame_height);
  } else { // PNG_BLEND_OP_SOURCE
    blendSource(rows_frame, rows_buffer, x_offset, y_offset, frame_width, frame_height);
  }

  // Save frame
  saveFrame(out_pixels, reinterpret_cast<uint32_t **>(rows_frame), width, height);

  // Process dispose operation after decode frame
  if (dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
    memcpy(p_frame, p_previous_frame, size);
  } else if (dispose_op == PNG_DISPOSE_OP_BACKGROUND) {
    for (uint32_t j = 0; j < frame_height; j++) {
      memset(rows_frame[j + y_offset] + x_offset * CHANNEL_4_BYTE_SIZE,
             0,
             frame_width * CHANNEL_4_BYTE_SIZE);
    }
  }
}
}
