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

import android.os.Debug
import android.os.SystemClock
import android.system.Os
import android.system.OsConstants
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import java.io.File

private const val BYTES_PER_MB = 1024.0 * 1024.0
private const val SAMPLE_INTERVAL_MS = 500L

/**
 * A live native-heap / Java-heap / CPU readout, sampled every 500ms.
 *
 * The composed APNG frames live in the native heap, so [Debug.getNativeHeapAllocatedSize]
 * is the number that separates EAGER (all frames resident) from ON_DEMAND (roughly a
 * single canvas). CPU is read from `/proc/self/stat` over the sample interval and
 * normalized so 100% == one fully-busy core.
 */
@Composable
fun ResourceStatsPanel(modifier: Modifier = Modifier) {
    val clockTicksPerSecond = remember { Os.sysconf(OsConstants._SC_CLK_TCK) }
    val cpuCount = remember { Runtime.getRuntime().availableProcessors() }
    var text by remember { mutableStateOf("sampling…") }

    LaunchedEffectStats { lastTicks, lastWallMs ->
        val nativeMb = Debug.getNativeHeapAllocatedSize() / BYTES_PER_MB
        val runtime = Runtime.getRuntime()
        val javaMb = (runtime.totalMemory() - runtime.freeMemory()) / BYTES_PER_MB

        val nowTicks = readSelfCpuTicks(lastTicks)
        val nowWallMs = SystemClock.elapsedRealtime()
        val wallMs = (nowWallMs - lastWallMs).coerceAtLeast(1L)
        val cpuMs = (nowTicks - lastTicks) * 1000.0 / clockTicksPerSecond
        val cpuPercent = (cpuMs / wallMs) * 100.0

        text = (
            "Native heap : %6.1f MB   ← APNG frames live here\n" +
                "Java heap   : %6.1f MB\n" +
                "CPU         : %5.0f %%   (100%% = 1 of %d cores)"
            ).format(nativeMb, javaMb, cpuPercent, cpuCount)

        nowTicks to nowWallMs
    }

    Text(
        text = text,
        fontFamily = FontFamily.Monospace,
        style = MaterialTheme.typography.bodySmall,
        modifier = modifier
            .background(Color(0x11000000))
            .padding(8.dp),
    )
}

/**
 * Runs [sample] every [SAMPLE_INTERVAL_MS], threading the previous CPU-tick and
 * wall-clock readings into the next call so each sample is a delta over the interval.
 * [sample] returns the new (ticks, wallMs) baseline.
 */
@Composable
private fun LaunchedEffectStats(sample: (lastTicks: Long, lastWallMs: Long) -> Pair<Long, Long>) {
    androidx.compose.runtime.LaunchedEffect(Unit) {
        var lastTicks = readSelfCpuTicks(0L)
        var lastWallMs = SystemClock.elapsedRealtime()
        while (true) {
            delay(SAMPLE_INTERVAL_MS)
            val (ticks, wallMs) = sample(lastTicks, lastWallMs)
            lastTicks = ticks
            lastWallMs = wallMs
        }
    }
}

/** utime + stime of this process, in clock ticks, from /proc/self/stat. */
private fun readSelfCpuTicks(fallback: Long): Long = try {
    val stat = File("/proc/self/stat").readText()
    // The comm field (in parentheses) may contain spaces, so split after ')'.
    val fields = stat.substring(stat.lastIndexOf(')') + 2).split(' ')
    // After comm+state, utime is field 14 and stime field 15 (1-based);
    // here that is index 11 and 12 of the post-')' slice.
    fields[11].toLong() + fields[12].toLong()
} catch (e: Exception) {
    fallback
}
