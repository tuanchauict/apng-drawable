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

package com.linecorp.apngsample

import android.annotation.SuppressLint
import android.content.ContentValues
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.Drawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Debug
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.MediaStore
import android.system.Os
import android.system.OsConstants
import android.util.Log
import android.widget.ImageView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.vectordrawable.graphics.drawable.Animatable2Compat
import com.linecorp.apng.ApngDrawable
import com.linecorp.apng.RepeatAnimationCallback
import com.linecorp.apng.decoder.ApngException
import com.linecorp.apng.decoder.DecodeMode
import com.linecorp.apngsample.databinding.ActivityMainBinding
import com.linecorp.lich.lifecycle.AutoResetLifecycleScope
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private val lifecycleScope: CoroutineScope = AutoResetLifecycleScope(this)

    private var drawable: ApngDrawable? = null

    // Remember the last load so toggling the decode mode can re-run it.
    private var lastLoadName: String? = null
    private var lastLoadWidth: Int? = null
    private var lastLoadHeight: Int? = null

    // --- Resource stats sampling ---
    private val statsHandler = Handler(Looper.getMainLooper())
    private val clockTicksPerSecond = Os.sysconf(OsConstants._SC_CLK_TCK)
    private val cpuCount = Runtime.getRuntime().availableProcessors()
    private var lastCpuTicks = 0L
    private var lastCpuWallMs = 0L
    private val statsSampler = object : Runnable {
        override fun run() {
            updateStats()
            statsHandler.postDelayed(this, STATS_INTERVAL_MS)
        }
    }

    @SuppressLint("SetTextI18n")
    private val animationCallback = object : AnimationCallbacks() {
        override fun onAnimationStart(drawable: Drawable) {
            Log.d("apng", "Animation start")
            binding.textCallback.text = "Animation started"
        }

        override fun onAnimationRepeat(drawable: ApngDrawable, nextLoopIndex: Int) {
            val loopCount = drawable.loopCount
            Log.d("apng", "Animation repeat loopCount: $loopCount, nextLoopIndex: $nextLoopIndex")
            binding.textCallback.text = "Animation repeat " +
                "loopCount: $loopCount, " +
                "nextLoopIndex: $nextLoopIndex"
        }

        override fun onAnimationEnd(drawable: Drawable) {
            Log.d("apng", "Animation end")
            binding.textCallback.text = "Animation ended"
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.buttonLoadImage1.setOnClickListener { startLoad("test.png") }
        binding.buttonLoadImage15x.setOnClickListener { startLoad("test.png", 500, 500) }
        binding.buttonLoadImage110x.setOnClickListener { startLoad("test.png", 1000, 1000) }
        binding.buttonLoadLarge.setOnClickListener { startLoad("large_anim.png") }
        binding.buttonLoadImage2NormalPng.setOnClickListener { startLoad("normal_png.png") }
        binding.buttonLoadImage2Jpeg.setOnClickListener { startLoad("jpeg.jpg") }
        binding.buttonMutate.setOnClickListener { mutate() }
        binding.buttonCopy.setOnClickListener { duplicate() }

        binding.buttonStart.setOnClickListener { startAnimation() }
        binding.buttonStop.setOnClickListener { stopAnimation() }
        binding.buttonGc.setOnClickListener { runGc() }
        binding.buttonSeekStart.setOnClickListener { seekTo(0L) }
        binding.buttonSeekEnd.setOnClickListener { seekTo(10000000L) }
        binding.buttonSaveCurrentFrame.setOnClickListener { exportCurrentFrame() }
        binding.buttonRemove.setOnClickListener { removeView() }

        // Flipping the decode mode reloads the current image so the stats below
        // reflect the new mode. The old drawable is recycled and GC is forced
        // first, otherwise its native frames linger and skew the comparison.
        binding.switchOnDemand.setOnCheckedChangeListener { _, _ ->
            val name = lastLoadName ?: return@setOnCheckedChangeListener
            releaseCurrentDrawable()
            System.gc()
            startLoad(name, lastLoadWidth, lastLoadHeight)
        }
    }

    private fun releaseCurrentDrawable() {
        binding.imageView.setImageDrawable(null)
        drawable?.clearAnimationCallbacks()
        drawable?.recycle()
        drawable = null
    }

    override fun onResume() {
        super.onResume()
        lastCpuTicks = readSelfCpuTicks()
        lastCpuWallMs = SystemClock.elapsedRealtime()
        statsHandler.post(statsSampler)
    }

    override fun onPause() {
        super.onPause()
        statsHandler.removeCallbacks(statsSampler)
    }

    @SuppressLint("SetTextI18n")
    private fun updateStats() {
        val nativeMb = Debug.getNativeHeapAllocatedSize() / BYTES_PER_MB
        val runtime = Runtime.getRuntime()
        val javaMb = (runtime.totalMemory() - runtime.freeMemory()) / BYTES_PER_MB

        // App CPU usage since the previous sample: ticks of CPU time consumed
        // divided by wall-clock time, normalized so 100% == one fully-busy core.
        val nowTicks = readSelfCpuTicks()
        val nowWallMs = SystemClock.elapsedRealtime()
        val wallMs = (nowWallMs - lastCpuWallMs).coerceAtLeast(1L)
        val cpuMs = (nowTicks - lastCpuTicks) * 1000.0 / clockTicksPerSecond
        val cpuPercent = (cpuMs / wallMs) * 100.0
        lastCpuTicks = nowTicks
        lastCpuWallMs = nowWallMs

        val d = drawable
        val frameInfo = if (d != null) {
            "frame ${d.currentFrameIndex + 1} / ${d.frameCount}"
        } else {
            "no image loaded"
        }
        val mode = if (binding.switchOnDemand.isChecked) "ON_DEMAND" else "EAGER"

        binding.textStats.text = (
            "MODE: %s        %s\n" +
            "Native heap : %6.1f MB   ← APNG frames live here\n" +
            "Java heap   : %6.1f MB\n" +
            "CPU         : %5.0f %%   (100%% = 1 of %d cores)"
            ).format(mode, frameInfo, nativeMb, javaMb, cpuPercent, cpuCount)
    }

    /** utime + stime of this process, in clock ticks, from /proc/self/stat. */
    private fun readSelfCpuTicks(): Long = try {
        val stat = java.io.File("/proc/self/stat").readText()
        // The comm field (in parentheses) may contain spaces, so split after ')'.
        val fields = stat.substring(stat.lastIndexOf(')') + 2).split(' ')
        // After comm+state, utime is field 14 and stime field 15 (1-based);
        // here that is index 11 and 12 of the post-')' slice.
        fields[11].toLong() + fields[12].toLong()
    } catch (e: Exception) {
        lastCpuTicks
    }

    @SuppressLint("SetTextI18n")
    private fun startLoad(name: String, width: Int? = null, height: Int? = null) {
        lastLoadName = name
        lastLoadWidth = width
        lastLoadHeight = height
        // Free the previous image's native frames so the stats reflect only this
        // load rather than the previous one lingering until GC.
        releaseCurrentDrawable()
        binding.textCallback.text = null
        val isApng = assets.open(name).buffered().use {
            ApngDrawable.isApng(it)
        }
        val decodeMode = if (binding.switchOnDemand.isChecked) {
            DecodeMode.ON_DEMAND
        } else {
            DecodeMode.EAGER
        }
        binding.textStatus.text = "isApng: $isApng"
        if (isApng) {
            try {
                drawable = ApngDrawable.decode(assets, name, width, height, decodeMode)
            } catch (e: ApngException) {
                binding.textCallback.text = "Failed to decode: ${e.errorCode}"
                return
            }
            drawable?.loopCount = 5
            drawable?.setTargetDensity(resources.displayMetrics)
            drawable?.registerAnimationCallback(animationCallback)
            drawable?.registerRepeatAnimationCallback(animationCallback)
            binding.imageView.setImageDrawable(drawable)
            binding.imageView.scaleType = ImageView.ScaleType.CENTER
        }
        Log.d("apng", "size: ${drawable?.allocationByteCount} byte")
    }

    private fun mutate() {
        drawable?.mutate()
    }

    private fun duplicate() {
        drawable = drawable?.constantState?.newDrawable() as? ApngDrawable ?: return
        drawable?.loopCount = 5
        drawable?.registerAnimationCallback(animationCallback)
        drawable?.registerRepeatAnimationCallback(animationCallback)
        drawable?.setTargetDensity(resources.displayMetrics)

        (binding.imageView.drawable as? ApngDrawable)?.recycle()
        binding.imageView.setImageDrawable(drawable)
    }

    private fun startAnimation() {
        drawable?.start()
    }

    private fun stopAnimation() {
        drawable?.stop()
    }

    private fun runGc() {
        System.gc()
    }

    private fun seekTo(time: Long) {
        drawable?.seekTo(time)
    }

    private fun exportCurrentFrame() {
        val drawableSnapshot = drawable ?: return
        lifecycleScope.launch {
            val bitmap = exportAsBitmap(drawableSnapshot)
            val savedUri = saveAsPng(bitmap)
            val toastMessage = if (savedUri != null) {
                "Saved to $savedUri"
            } else {
                "Failed to save image"
            }
            Toast.makeText(this@MainActivity, toastMessage, Toast.LENGTH_SHORT).show()
        }
    }

    private suspend fun exportAsBitmap(drawable: Drawable): Bitmap =
        withContext(Dispatchers.Default) {
            val bitmap = Bitmap.createBitmap(
                drawable.intrinsicWidth,
                drawable.intrinsicHeight,
                Bitmap.Config.ARGB_8888
            )
            val canvas = Canvas(bitmap)
            drawable.draw(canvas)
            return@withContext bitmap
        }

    private suspend fun saveAsPng(bitmap: Bitmap): Uri? = withContext(Dispatchers.IO) {
        val resolver = applicationContext.contentResolver
        MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        val collectionUri = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        } else {
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        }
        val contentValues = ContentValues().apply {
            put(
                MediaStore.Audio.Media.DISPLAY_NAME,
                "apng-frame-export-${System.currentTimeMillis()}.png"
            )
        }
        val uri = resolver.insert(collectionUri, contentValues) ?: return@withContext null
        resolver.openOutputStream(uri)?.use {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
        }
        return@withContext uri
    }

    // Tests whether removing the view will release the memory
    private fun removeView() {
        binding.imageView.setImageDrawable(null)
        drawable?.clearAnimationCallbacks()
        drawable?.recycle()
        drawable = null
    }

    private abstract class AnimationCallbacks
        : Animatable2Compat.AnimationCallback(), RepeatAnimationCallback

    private companion object {
        private const val STATS_INTERVAL_MS = 500L
        private const val BYTES_PER_MB = 1024.0 * 1024.0
    }
}
