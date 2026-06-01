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

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import coil3.ImageLoader
import coil3.compose.AsyncImage
import coil3.request.ImageRequest
import com.linecorp.apng.decoder.DecodeMode

/** The APNG assets bundled with this sample. */
private val ASSETS = listOf(
    "large_anim.png" to "Large (120 frames)",
    "test.png" to "Small",
)

/**
 * A Compose + Coil sample for [com.linecorp.apng.ApngDrawable].
 *
 * It loads an APNG through [ApngCoilDecoder] and renders it with Coil's
 * [AsyncImage]. The toggle flips between [DecodeMode.EAGER] (all frames composed up
 * front) and [DecodeMode.ON_DEMAND] (streaming, roughly constant memory); a fresh
 * [ImageLoader] per mode forces a re-decode so the difference is visible.
 */
@Composable
fun ComposeSampleScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current

    var onDemand by remember { mutableStateOf(false) }
    var asset by remember { mutableStateOf(ASSETS.first().first) }
    val decodeMode = if (onDemand) DecodeMode.ON_DEMAND else DecodeMode.EAGER

    // One loader per decode mode: a different decoder factory means a different
    // cache key, so toggling the mode re-decodes the same asset.
    val imageLoader = remember(decodeMode) {
        ImageLoader.Builder(context)
            .components { add(ApngCoilDecoder.Factory(decodeMode)) }
            .build()
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Top,
    ) {
        Text(
            text = "ApngDrawable + Coil + Compose",
            style = MaterialTheme.typography.titleLarge,
        )

        Spacer(Modifier.height(16.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "On-demand decoding",
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    text = "Off = EAGER (all frames up front)\n" +
                        "On = ON_DEMAND (streaming, ~constant memory)",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            Switch(checked = onDemand, onCheckedChange = { onDemand = it })
        }

        Spacer(Modifier.height(12.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ASSETS.forEach { (name, label) ->
                val selected = asset == name
                if (selected) {
                    Button(
                        onClick = { asset = name },
                        modifier = Modifier.weight(1f),
                    ) { Text(label, maxLines = 1) }
                } else {
                    OutlinedButton(
                        onClick = { asset = name },
                        modifier = Modifier.weight(1f),
                    ) { Text(label, maxLines = 1) }
                }
            }
        }

        Spacer(Modifier.height(12.dp))

        Text(
            text = "mode: $decodeMode\nasset: $asset",
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = FontFamily.Monospace,
        )

        Spacer(Modifier.height(16.dp))

        AsyncImage(
            model = ImageRequest.Builder(context)
                .data("file:///android_asset/$asset")
                .build(),
            imageLoader = imageLoader,
            contentDescription = "Animated APNG ($asset)",
            modifier = Modifier.size(300.dp),
        )
    }
}
