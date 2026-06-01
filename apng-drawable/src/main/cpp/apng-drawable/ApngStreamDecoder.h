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
#include <memory>
#include <vector>
#include "png.h"
#include "MemoryStreamSource.h"

namespace apng_drawable {

/**
 * On-demand (streaming) APNG decoder.
 *
 * Unlike {@link ApngDecoder}, which composes every frame up front into
 * `ApngImage` (memory ∝ N × w × h × 4), this decoder keeps only a single running
 * composition canvas plus the buffered encoded bytes, and composes one frame at a
 * time on request. Resident memory is therefore ~constant in the frame count.
 *
 * Decoding is sequential and forward-only (APNG frames are deltas): `seekTo`
 * steps forward one frame at a time, and rewinds (recreating the libpng session
 * over the buffered bytes) for loop-wrap and backward seeks. All frame durations,
 * the frame count and the loop count are produced up front by a lightweight
 * metadata pre-scan, because `ApngDrawable` maps elapsed time → frame index.
 *
 * Instances are not internally synchronized; callers must serialize access (the
 * JNI layer guards each decoder with its own mutex).
 */
class ApngStreamDecoder {
 public:
  /**
   * Buffers `encoded` (taking ownership), runs the metadata pre-scan and opens a
   * playback session positioned before frame 0. Returns nullptr and sets
   * `result` to an ERR_* code on failure.
   */
  static std::unique_ptr<ApngStreamDecoder> create(std::vector<uint8_t> encoded, int32_t &result);

  ApngStreamDecoder() = default;
  ~ApngStreamDecoder();

  uint32_t getWidth() const { return mWidth; }
  uint32_t getHeight() const { return mHeight; }
  uint32_t getFrameCount() const { return mFrameCount; }
  uint32_t getLoopCount() const { return mLoopCount; }
  const std::vector<uint32_t> &getDurations() const { return mDurations; }

  /** The buffered encoded APNG bytes, used to clone the decoder (`copyStream`). */
  const std::vector<uint8_t> &getEncoded() const { return mEncoded; }

  uint32_t getFrameByteCount() const { return sizeof(uint32_t) * mWidth * mHeight; }

  /**
   * The streaming resident footprint: the running canvas (3 × canvas), the
   * displayed-frame buffer and the buffered encoded bytes. Independent of the
   * frame count (contrast `ApngImage::getAllFrameByteCount`, which is N × frame).
   */
  uint64_t getAllFrameByteCount() const;

  /**
   * Composes the canvas so it holds frame `target`. Forward seeks step frame by
   * frame; backward seeks (and loop-wrap) rewind first. Returns SUCCESS or an
   * ERR_* code. After SUCCESS the displayed pixels are available via `blitInto`.
   */
  int32_t seekTo(uint32_t target);

  /** Copies the currently composed (premultiplied) frame into `dst`. */
  void blitInto(uint32_t *dst);

 private:
  std::vector<uint8_t> mEncoded;
  std::unique_ptr<MemoryStreamSource> mSource;
  png_structp mPngPtr = nullptr;
  png_infop mInfoPtr = nullptr;

  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  uint32_t mFrameCount = 0;
  uint32_t mLoopCount = 0;
  uint32_t mFirst = 0;          // index of the first non-hidden frame (0 or 1)
  size_t mRowBytes = 0;
  size_t mCanvasSize = 0;       // mHeight * mRowBytes

  std::unique_ptr<uint8_t[]> mPFrame;           // running canvas (straight alpha)
  std::unique_ptr<uint8_t[]> mPPreviousFrame;   // DISPOSE_OP_PREVIOUS snapshot
  std::unique_ptr<uint8_t[]> mPBuffer;          // freshly read frame pixels
  std::unique_ptr<png_bytep[]> mRowsFrame;      // row pointers into mPFrame
  std::unique_ptr<png_bytep[]> mRowsBuffer;     // row pointers into mPBuffer
  std::unique_ptr<uint32_t[]> mDisplayPixels;   // premultiplied output for mCurrentIndex

  std::vector<uint32_t> mDurations;
  int64_t mCurrentIndex = -1;   // frame the canvas currently holds (-1 = none)

  int32_t preScan();            // metadata pre-scan + canvas allocation
  int32_t openSession();        // (re)create the libpng session before frame 0
  void closeSession();
  int32_t composeNext();        // advance the canvas by one frame

  // Do not copy this object.
  ApngStreamDecoder(const ApngStreamDecoder &ref) = delete;
  ApngStreamDecoder &operator=(const ApngStreamDecoder &ref) = delete;
};
}
