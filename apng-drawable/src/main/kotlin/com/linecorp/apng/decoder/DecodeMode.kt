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

package com.linecorp.apng.decoder

/**
 * How an APNG is decoded into frames.
 */
enum class DecodeMode {
    /**
     * Decode and compose every frame up front, keeping them all in native memory.
     *
     * Drawing and seeking are O(1) (a memory copy of a pre-composed frame), at the
     * cost of resident memory proportional to the frame count
     * (frames × width × height × 4 bytes). This is the default and is the better
     * choice for small or short APNGs.
     */
    EAGER,

    /**
     * Decode and compose one frame at a time, on demand, keeping only a single
     * composition canvas plus the buffered encoded bytes.
     *
     * Resident memory is roughly constant in the frame count, which suits large or
     * many-frame APNGs. The trade-off is that forward playback decodes one frame per
     * displayed frame, and `seekToFrame` / backward seeks are O(target) because APNG
     * frames are deltas and must be replayed from the start.
     */
    ON_DEMAND
}
