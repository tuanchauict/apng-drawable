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

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.os.Trace
import android.util.Log
import androidx.annotation.IntRange
import com.linecorp.apng.BuildConfig
import java.io.InputStream

/**
 * A class which holds APNG image information and bridge to the native(jni) layer.
 * When finished to use this class, you must explicitly call [recycle].
 */
internal class Apng(
    private val id: Int,
    /**
     * The width of the image
     */
    val width: Int,
    /**
     * The height of the image
     */
    val height: Int,
    /**
     * The number of frames included in this APNG image.
     */
    @IntRange(from = 1, to = Int.MAX_VALUE.toLong())
    val frameCount: Int,

    val frameDurations: IntArray,
    /**
     * The number of times to loop this APNG image. The value must be a signed value.
     * `0` indicates infinite looping.
     */
    @IntRange(from = 0, to = Int.MAX_VALUE.toLong())
    val loopCount: Int,
    /**
     * The size of memory required for this image in the native layer.
     */
    @IntRange(from = 0, to = Int.MAX_VALUE.toLong())
    val allFrameByteCount: Long,
    /**
     * How frames are decoded in the native layer. Determines which set of JNI
     * entry points ([ApngDecoderJni.draw] / [ApngDecoderJni.drawStream], etc.) this
     * instance routes to.
     */
    val decodeMode: DecodeMode = DecodeMode.EAGER
) {
    /**
     * Scratch frame buffer for [DecodeMode.EAGER], where [drawWithIndex] composes the
     * current frame into it and blits to the canvas. [DecodeMode.ON_DEMAND] renders
     * through its own ping-pong buffers in [com.linecorp.apng.ApngDrawable], so this
     * bitmap (a full extra frame of resident memory) is never allocated there.
     */
    private val bitmap: Bitmap? = if (decodeMode == DecodeMode.EAGER) {
        Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
    } else {
        null
    }

    val byteCount: Int
        get() = bitmap?.allocationByteCount ?: (width * height * BYTES_PER_PIXEL)

    init {
        // ON_DEMAND composes frame 0 lazily through its own buffers on the first draw.
        val target = bitmap
        if (target != null) {
            Trace.beginSection("Apng#draw")
            drawFrame(0, target)
            Trace.endSection()
        }
    }

    /**
     * The duration to animate one loop of APNG animation.
     */
    @IntRange(from = 0, to = Int.MAX_VALUE.toLong())
    val duration: Int = frameDurations.sum()

    val isRecycled: Boolean
        get() = bitmap?.isRecycled ?: false

    val config: Bitmap.Config
        get() = bitmap?.config ?: Bitmap.Config.ARGB_8888

    fun recycle() {
        when (decodeMode) {
            DecodeMode.EAGER -> ApngDecoderJni.recycle(id)
            DecodeMode.ON_DEMAND -> ApngDecoderJni.recycleStream(id)
        }
    }

    fun copy(): Apng = copy(this)

    /**
     * Composes [frameIndex] into [target] using the JNI entry point that matches
     * this instance's [decodeMode].
     */
    fun drawFrame(frameIndex: Int, target: Bitmap) {
        when (decodeMode) {
            DecodeMode.EAGER -> ApngDecoderJni.draw(id, frameIndex, target)
            DecodeMode.ON_DEMAND -> ApngDecoderJni.drawStream(id, frameIndex, target)
        }
    }

    @Suppress("unused")
    fun finalize() {
        if (BuildConfig.DEBUG) {
            Log.d("apng-drawable", "finalized: $id")
        }
        recycle()
    }

    /**
     * Draws specified frame to the [canvas].
     */
    fun drawWithIndex(frameIndex: Int, canvas: Canvas, src: Rect?, dst: Rect, paint: Paint) {
        // Only EAGER draws through this scratch bitmap; ON_DEMAND blits via its own
        // buffers in ApngDrawable and never calls here.
        val target = bitmap ?: return
        Trace.beginSection("Apng#draw")
        drawFrame(frameIndex, target)
        Trace.endSection()
        canvas.drawBitmap(target, src, dst, paint)
    }

    /**
     * A model class which contains the results of decoding in the native layer.
     *
     * Note:
     * If you edit this class, you should update `ApngDecoderJni.cpp` too.
     * Also, this class shouldn't be obfuscated, including class name and class member's name.
     * This class is accessed from the native layer and the fields are accessed by name like
     * reflection.
     */
    class DecodeResult {
        var width: Int = 0
        var height: Int = 0
        var frameCount: Int = 0
        var loopCount: Int = 0
        var frameDurations: IntArray = intArrayOf()
        var allFrameByteCount: Long = 0
    }

    companion object {

        /** Bytes per pixel of an ARGB_8888 frame, used to size [byteCount] without a bitmap. */
        private const val BYTES_PER_PIXEL = 4

        @Throws(ApngException::class)
        fun decode(stream: InputStream, decodeMode: DecodeMode = DecodeMode.EAGER): Apng {
            val result = DecodeResult()
            Trace.beginSection("Apng#decode")
            val id = try {
                when (decodeMode) {
                    DecodeMode.EAGER -> ApngDecoderJni.decode(stream, result)
                    DecodeMode.ON_DEMAND -> ApngDecoderJni.decodeStream(stream, result)
                }
            } catch (e: Throwable) {
                throw ApngException(e)
            } finally {
                Trace.endSection()
            }
            throwIfError(id)
            try {
                return Apng(
                    id,
                    result.width,
                    result.height,
                    result.frameCount,
                    result.frameDurations,
                    result.loopCount,
                    result.allFrameByteCount,
                    decodeMode
                )
            } catch (e: Throwable) {
                throw ApngException(e)
            }
        }

        @Throws(ApngException::class)
        fun isApng(stream: InputStream): Boolean {
            return try {
                ApngDecoderJni.isApng(stream)
            } catch (e: Throwable) {
                throw ApngException(e)
            }
        }

        @Throws(ApngException::class)
        fun copy(apng: Apng): Apng {
            val result = DecodeResult()
            Trace.beginSection("Apng#copy")
            val id = try {
                when (apng.decodeMode) {
                    DecodeMode.EAGER -> ApngDecoderJni.copy(apng.id, result)
                    DecodeMode.ON_DEMAND -> ApngDecoderJni.copyStream(apng.id, result)
                }
            } catch (e: Throwable) {
                throw ApngException(e)
            } finally {
                Trace.endSection()
            }
            throwIfError(id)
            try {
                return Apng(
                    id,
                    result.width,
                    result.height,
                    result.frameCount,
                    result.frameDurations,
                    result.loopCount,
                    result.allFrameByteCount,
                    apng.decodeMode
                )
            } catch (e: Throwable) {
                throw ApngException(e)
            }
        }

        @Throws(ApngException::class)
        private fun throwIfError(resultCode: Int) {
            if (resultCode >= 0) {
                return
            }
            throw ApngException(ApngException.ErrorCode.fromErrorCode(resultCode))
        }
    }
}
