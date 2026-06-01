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

package com.linecorp.apngsample.compose

import coil3.ImageLoader
import coil3.asImage
import coil3.decode.DecodeResult
import coil3.decode.Decoder
import coil3.decode.ImageSource
import coil3.fetch.SourceFetchResult
import coil3.request.Options
import com.linecorp.apng.ApngDrawable
import com.linecorp.apng.decoder.DecodeMode
import okio.BufferedSource
import okio.ByteString.Companion.encodeUtf8
import okio.ByteString.Companion.toByteString

/**
 * A Coil [Decoder] that renders animated PNGs with LINE's [ApngDrawable].
 *
 * Coil's built-in image decoder only draws the first frame of an APNG, so this
 * hands the encoded bytes to [ApngDrawable] and exposes the resulting (animatable)
 * drawable to Coil as a [coil3.Image]. The [decodeMode] selects between the eager
 * decoder (default) and the streaming [DecodeMode.ON_DEMAND] decoder.
 */
class ApngCoilDecoder(
    private val source: ImageSource,
    private val decodeMode: DecodeMode,
) : Decoder {
    override suspend fun decode(): DecodeResult {
        // ApngDrawable.decode reads the stream fully; ON_DEMAND keeps the encoded
        // bytes buffered natively so it can re-read them on every loop.
        val drawable = source.source().inputStream().buffered().use { input ->
            ApngDrawable.decode(input, decodeMode = decodeMode)
        }
        return DecodeResult(
            image = drawable.asImage(),
            isSampled = false,
        )
    }

    /**
     * Builds an [ApngCoilDecoder] for animated PNGs only, decoding with the given
     * [decodeMode]. A new factory (and a new [ImageLoader]) per mode keeps the two
     * modes on separate cache keys so flipping the toggle re-decodes.
     */
    class Factory(
        private val decodeMode: DecodeMode,
    ) : Decoder.Factory {
        override fun create(
            result: SourceFetchResult,
            options: Options,
            imageLoader: ImageLoader,
        ): Decoder? {
            if (!isApng(result.source.source())) return null
            return ApngCoilDecoder(result.source, decodeMode)
        }

        /**
         * Returns true only for *animated* PNGs: a PNG signature followed by an
         * `acTL` (animation control) chunk. In a valid APNG `acTL` precedes the first
         * `IDAT`, so the search is bounded to the pre-`IDAT` header region instead of
         * scanning the (possibly large) whole stream — important since plain PNGs
         * would otherwise be read to the end just to conclude "not animated". All
         * reads go through `peek()`, so the decoder can still read the full stream.
         */
        private fun isApng(source: BufferedSource): Boolean {
            if (!source.rangeEquals(0L, PNG_SIGNATURE)) return false
            val idatIndex = source.peek().indexOf(IDAT_CHUNK)
            if (idatIndex == -1L) return false
            val header = source.peek().readByteString(idatIndex)
            return header.indexOf(ACTL_CHUNK) != -1
        }

        private companion object {
            val PNG_SIGNATURE = byteArrayOf(
                0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            ).toByteString()
            val ACTL_CHUNK = "acTL".encodeUtf8()
            val IDAT_CHUNK = "IDAT".encodeUtf8()
        }
    }
}
