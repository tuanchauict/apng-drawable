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
#include "png.h"

namespace apng_drawable {

const uint8_t ALPHA_TRANSPARENT = 0U;
const uint8_t ALPHA_OPAQUE = 0xFFU;
const size_t CHANNEL_4_BYTE_SIZE = sizeof(uint8_t) * 4;

/**
 * Copies the composited canvas (`source`, straight alpha) into `destination`,
 * pre-multiplying alpha and handling Android's ABGR layout. `destination` may be
 * null, in which case the call is a no-op.
 *
 * This produces the displayed pixels for one frame; the canvas itself stays in
 * straight alpha so subsequent frames can blend onto it correctly.
 */
void saveFrame(uint32_t *destination,
               uint32_t **const source,
               uint32_t width,
               uint32_t height);

/**
 * Blends `source` over `destination` (`PNG_BLEND_OP_OVER`) within the sub-rectangle
 * described by the offsets/size, both buffers being straight-alpha RGBA rows.
 */
void blendOver(uint8_t **destination,
               uint8_t **source,
               png_uint_32 x_offset,
               png_uint_32 y_offset,
               png_uint_32 width,
               png_uint_32 height);

/**
 * Copies `source` into `destination` (`PNG_BLEND_OP_SOURCE`) within the
 * sub-rectangle described by the offsets/size.
 */
void blendSource(uint8_t **destination,
                 uint8_t **source,
                 png_uint_32 x_offset,
                 png_uint_32 y_offset,
                 png_uint_32 width,
                 png_uint_32 height);

/**
 * Composites one APNG frame, identically for the eager and streaming decoders.
 *
 * Preconditions:
 *  - `rows_buffer` points at the freshly read frame pixels (output of
 *    `png_read_image`).
 *  - `rows_frame` points at the running canvas `p_frame` (straight alpha), which
 *    holds the previous composited frame.
 *  - `dispose_op`/`blend_op` are the fcTL values, already adjusted for the first
 *    (or hidden-first) frame by the caller.
 *
 * Effects, matching the original eager loop body:
 *  1. if dispose is PREVIOUS, snapshots the canvas into `p_previous_frame`;
 *  2. blends the frame into the canvas (OVER or SOURCE);
 *  3. writes the displayed pixels (premultiplied) into `out_pixels` (may be null);
 *  4. applies the dispose op to the canvas so the next frame blends correctly.
 *
 * `size` is the canvas byte size (`height * row_bytes`).
 */
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
                  size_t size);
}
