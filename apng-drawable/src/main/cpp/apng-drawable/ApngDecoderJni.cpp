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

#include <jni.h>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <android/bitmap.h>
#include "Log.h"
#include "ApngDecoder.h"
#include "ApngStreamDecoder.h"
#include "Error.h"

#include "StreamSource.h"
#ifdef BUILD_DEBUG
#include<chrono>
#endif

namespace apng_drawable {

static std::unordered_map<int32_t, std::shared_ptr<ApngImage>> gImageMap;

/**
 * A streaming decoder plus its own mutex. `gLock` only guards the map itself;
 * decode/draw work on a decoder is serialized by the per-entry mutex, so a slow
 * decode on one image never blocks lookups or other images.
 */
struct StreamEntry {
  std::shared_ptr<ApngStreamDecoder> decoder;
  std::shared_ptr<std::mutex> mutex;
};

static std::unordered_map<int32_t, StreamEntry> gStreamMap;
static std::mutex gLock;
static uint32_t gIdCounter;

static jclass gResult_class;
static jfieldID gResult_heightFieldID;
static jfieldID gResult_widthFieldID;
static jfieldID gResult_frameCountFieldID;
static jfieldID gResult_repeatCountFieldID;
static jfieldID gResult_frameDurationsFieldID;
static jfieldID gResult_allFrameByteCountFieldID;

void copyFrameDurations(JNIEnv *env,
                        const std::shared_ptr<ApngImage> &image,
                        jintArray &frame_durations_ptr);

static bool readStreamFully(JNIEnv *env, jobject inputStream, std::vector<uint8_t> &out);
static void copyStreamDurations(JNIEnv *env,
                                const std::shared_ptr<ApngStreamDecoder> &decoder,
                                jintArray frame_durations_ptr);
static void setStreamResultFields(JNIEnv *env,
                                  jobject result,
                                  const std::shared_ptr<ApngStreamDecoder> &decoder);

extern "C" {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  gIdCounter = 0;

  jclass result_class = env->FindClass("com/linecorp/apng/decoder/Apng$DecodeResult");
  gResult_class = reinterpret_cast<jclass>(env->NewGlobalRef(result_class));
  env->DeleteLocalRef(result_class);
  gResult_heightFieldID = env->GetFieldID(gResult_class, "height", "I");
  gResult_widthFieldID = env->GetFieldID(gResult_class, "width", "I");
  gResult_frameCountFieldID = env->GetFieldID(gResult_class, "frameCount", "I");
  gResult_repeatCountFieldID = env->GetFieldID(gResult_class, "loopCount", "I");
  gResult_frameDurationsFieldID = env->GetFieldID(gResult_class, "frameDurations", "[I");
  gResult_allFrameByteCountFieldID = env->GetFieldID(gResult_class, "allFrameByteCount", "J");
  StreamSource::registerJavaClass(env);
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
  JNIEnv *env;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return;
  }
  gResult_heightFieldID = nullptr;
  gResult_widthFieldID = nullptr;
  gResult_frameCountFieldID = nullptr;
  gResult_repeatCountFieldID = nullptr;
  gResult_frameDurationsFieldID = nullptr;
  gResult_allFrameByteCountFieldID = nullptr;
  env->DeleteGlobalRef(gResult_class);
  gResult_class = nullptr;
  StreamSource::unregisterJavaClass(env);

  gImageMap.clear();
  gStreamMap.clear();
}

JNIEXPORT jint JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_decode(
    JNIEnv *env,
    jclass thiz,
    jobject inputStream,
    jobject result
) {
  LOGV("decode start");
#ifdef BUILD_DEBUG
  std::chrono::system_clock::time_point start;
  std::chrono::system_clock::time_point end;
  start = std::chrono::system_clock::now();
#endif
  int32_t resultCode;

  // decode
  std::unique_ptr<StreamSource> source(new StreamSource(env, inputStream));
  std::shared_ptr<ApngImage> image = std::move(ApngDecoder::decode(std::move(source), resultCode));

  LOGV(" | decode result: %d", resultCode);

  if (resultCode != SUCCESS) {
    return resultCode;
  }

#ifdef BUILD_DEBUG
  end = std::chrono::system_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  LOGV(" | time: %lld ns (%lld ms)",
       elapsed,
       elapsed / 1000000);
#endif

  LOGV(" | set to field");

  env->SetIntField(result, gResult_widthFieldID, image->getWidth());
  env->SetIntField(result, gResult_heightFieldID, image->getHeight());
  env->SetIntField(result, gResult_frameCountFieldID, image->getFrameCount());
  env->SetIntField(result, gResult_repeatCountFieldID, image->getRepeatCount());
  env->SetLongField(result, gResult_allFrameByteCountFieldID, image->getAllFrameByteCount());
  {
    jintArray frame_durations = env->NewIntArray(image->getFrameCount());
    if (!frame_durations) {
      return ERR_OUT_OF_MEMORY;
    }
    copyFrameDurations(env, image, frame_durations);
    env->SetObjectField(result, gResult_frameDurationsFieldID, frame_durations);
    env->DeleteLocalRef(frame_durations);
  }
  {
    std::lock_guard<std::mutex> lock(gLock);
    gIdCounter++;
    gImageMap.emplace(gIdCounter, std::move(image));
    resultCode = gIdCounter;
    LOGV(" | total images: %ld", gImageMap.size());
  }
  LOGV("decode end");
  return resultCode;
}

JNIEXPORT jboolean JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_isApng(
    JNIEnv *env,
    jclass thiz,
    jobject inputStream
) {
  // check
  std::unique_ptr<StreamSource> source(new StreamSource(env, inputStream));
  bool result = ApngDecoder::isApng(std::move(source));
  return static_cast<jboolean>(result);
}

JNIEXPORT void JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_draw(
    JNIEnv *env,
    jclass thiz,
    jint id,
    jint index,
    jobject bitmap
) {
  void *data;

  if (id < 0) {
    return;
  }
  if (index < 0) {
    return;
  }

  int32_t result;
  if ((result = AndroidBitmap_lockPixels(env, bitmap, &data)) < 0) {
    LOGE("Error in AndroidBitmap_lockPixels. errorCode: %d", result);
    return;
  }

  AndroidBitmapInfo info;
  if ((result = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
    LOGE("Error in AndroidBitmap_getInfo. errorCode: %d", result);
    return;
  }

  std::shared_ptr<ApngImage> image = nullptr;
  {
    std::lock_guard<std::mutex> lock(gLock);
    auto const &it = gImageMap.find(id);
    if (it != gImageMap.end()) {
      image = gImageMap[id];
    }
  }

  if (!image) {
    AndroidBitmap_unlockPixels(env, bitmap);
    return;
  }

  std::shared_ptr<ApngFrame> frame = image->getFrame(static_cast<const uint32_t>(index));
  if (!frame) {
    AndroidBitmap_unlockPixels(env, bitmap);
    return;
  }
  memcpy(data, frame->getRawPixels(), image->getFrameByteCount());
  AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT jint JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_recycle(
    JNIEnv *env,
    jclass thiz,
    jint id
) {
  LOGV("recycle start. id : %d", id);
  if (id < 0) {
    return ERR_NOT_EXIST_IMAGE;
  }
  std::lock_guard<std::mutex> lock(gLock);
  auto const &it = gImageMap.find(id);
  if (it == gImageMap.end()) {
    return ERR_NOT_EXIST_IMAGE;
  }
  gImageMap.erase(it);
  LOGV(" | removed from map. remaining: %ld", gImageMap.size());
  LOGV("recycle end");
  return SUCCESS;
}

JNIEXPORT jint JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_copy(
    JNIEnv *env,
    jclass thiz,
    jint id,
    jobject result
) {
  LOGV("copy start. id : %d", id);
  if (id < 0) {
    return ERR_NOT_EXIST_IMAGE;
  }
  std::lock_guard<std::mutex> lock(gLock);
  auto const &it = gImageMap.find(id);
  if (it == gImageMap.end()) {
    return ERR_NOT_EXIST_IMAGE;
  }

  auto copyPtr = it->second;

  env->SetIntField(result, gResult_widthFieldID, copyPtr->getWidth());
  env->SetIntField(result, gResult_heightFieldID, copyPtr->getHeight());
  env->SetIntField(result, gResult_frameCountFieldID, copyPtr->getFrameCount());
  env->SetIntField(result, gResult_repeatCountFieldID, copyPtr->getRepeatCount());
  env->SetLongField(result, gResult_allFrameByteCountFieldID, copyPtr->getAllFrameByteCount());

  {
    uint32_t frame_count = copyPtr->getFrameCount();
    jintArray frame_durations = env->NewIntArray(frame_count);
    if (!frame_durations) {
      return ERR_OUT_OF_MEMORY;
    }
    copyFrameDurations(env, copyPtr, frame_durations);
    env->SetObjectField(result, gResult_frameDurationsFieldID, frame_durations);
    env->DeleteLocalRef(frame_durations);
  }

  int32_t resultId = ++gIdCounter;
  gImageMap.emplace(resultId, std::move(copyPtr));
  LOGV(" | total images: %ld", gImageMap.size());
  LOGV("copy end");
  return resultId;
}

JNIEXPORT jint JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_decodeStream(
    JNIEnv *env,
    jclass thiz,
    jobject inputStream,
    jobject result
) {
  LOGV("decodeStream start");
  std::vector<uint8_t> encoded;
  if (!readStreamFully(env, inputStream, encoded)) {
    return ERR_STREAM_READ_FAIL;
  }

  int32_t resultCode;
  std::shared_ptr<ApngStreamDecoder> decoder =
      ApngStreamDecoder::create(std::move(encoded), resultCode);
  LOGV(" | decodeStream result: %d", resultCode);
  if (resultCode != SUCCESS) {
    return resultCode;
  }

  setStreamResultFields(env, result, decoder);
  {
    jintArray frame_durations = env->NewIntArray(decoder->getFrameCount());
    if (!frame_durations) {
      return ERR_OUT_OF_MEMORY;
    }
    copyStreamDurations(env, decoder, frame_durations);
    env->SetObjectField(result, gResult_frameDurationsFieldID, frame_durations);
    env->DeleteLocalRef(frame_durations);
  }

  {
    std::lock_guard<std::mutex> lock(gLock);
    gIdCounter++;
    StreamEntry entry{std::move(decoder), std::make_shared<std::mutex>()};
    gStreamMap.emplace(gIdCounter, std::move(entry));
    resultCode = gIdCounter;
    LOGV(" | total streams: %ld", gStreamMap.size());
  }
  LOGV("decodeStream end");
  return resultCode;
}

JNIEXPORT void JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_drawStream(
    JNIEnv *env,
    jclass thiz,
    jint id,
    jint index,
    jobject bitmap
) {
  if (id < 0 || index < 0) {
    return;
  }

  std::shared_ptr<ApngStreamDecoder> decoder;
  std::shared_ptr<std::mutex> entryMutex;
  {
    std::lock_guard<std::mutex> lock(gLock);
    auto const &it = gStreamMap.find(id);
    if (it != gStreamMap.end()) {
      decoder = it->second.decoder;
      entryMutex = it->second.mutex;
    }
  }
  if (!decoder) {
    return;
  }

  void *data;
  int32_t result;
  if ((result = AndroidBitmap_lockPixels(env, bitmap, &data)) < 0) {
    LOGE("Error in AndroidBitmap_lockPixels. errorCode: %d", result);
    return;
  }

  {
    // Serialize seek/compose on this decoder; decode may run off the UI thread.
    std::lock_guard<std::mutex> decodeLock(*entryMutex);
    if (decoder->seekTo(static_cast<uint32_t>(index)) == SUCCESS) {
      decoder->blitInto(reinterpret_cast<uint32_t *>(data));
    }
  }

  AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT jint JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_recycleStream(
    JNIEnv *env,
    jclass thiz,
    jint id
) {
  LOGV("recycleStream start. id : %d", id);
  if (id < 0) {
    return ERR_NOT_EXIST_IMAGE;
  }
  std::lock_guard<std::mutex> lock(gLock);
  auto const &it = gStreamMap.find(id);
  if (it == gStreamMap.end()) {
    return ERR_NOT_EXIST_IMAGE;
  }
  gStreamMap.erase(it);
  LOGV(" | removed from stream map. remaining: %ld", gStreamMap.size());
  LOGV("recycleStream end");
  return SUCCESS;
}

JNIEXPORT jint JNICALL
Java_com_linecorp_apng_decoder_ApngDecoderJni_copyStream(
    JNIEnv *env,
    jclass thiz,
    jint id,
    jobject result
) {
  LOGV("copyStream start. id : %d", id);
  if (id < 0) {
    return ERR_NOT_EXIST_IMAGE;
  }

  std::shared_ptr<ApngStreamDecoder> src;
  std::shared_ptr<std::mutex> srcMutex;
  {
    std::lock_guard<std::mutex> lock(gLock);
    auto const &it = gStreamMap.find(id);
    if (it == gStreamMap.end()) {
      return ERR_NOT_EXIST_IMAGE;
    }
    src = it->second.decoder;
    srcMutex = it->second.mutex;
  }

  // Clone = re-buffer the bytes + a fresh session. Memory stays ~constant (no
  // N-frame copy), but create() runs the metadata pre-scan, which decodes every
  // frame once, so this is O(N) in CPU/time despite the small footprint.
  std::vector<uint8_t> bytesCopy;
  {
    std::lock_guard<std::mutex> decodeLock(*srcMutex);
    bytesCopy = src->getEncoded();
  }

  int32_t resultCode;
  std::shared_ptr<ApngStreamDecoder> copy =
      ApngStreamDecoder::create(std::move(bytesCopy), resultCode);
  if (resultCode != SUCCESS) {
    return resultCode;
  }

  setStreamResultFields(env, result, copy);
  {
    jintArray frame_durations = env->NewIntArray(copy->getFrameCount());
    if (!frame_durations) {
      return ERR_OUT_OF_MEMORY;
    }
    copyStreamDurations(env, copy, frame_durations);
    env->SetObjectField(result, gResult_frameDurationsFieldID, frame_durations);
    env->DeleteLocalRef(frame_durations);
  }

  int32_t resultId;
  {
    std::lock_guard<std::mutex> lock(gLock);
    resultId = ++gIdCounter;
    StreamEntry entry{std::move(copy), std::make_shared<std::mutex>()};
    gStreamMap.emplace(resultId, std::move(entry));
    LOGV(" | total streams: %ld", gStreamMap.size());
  }
  LOGV("copyStream end");
  return resultId;
}

#pragma clang diagnostic pop
}

void copyFrameDurations(JNIEnv *env,
                        const std::shared_ptr<ApngImage> &image,
                        jintArray &frame_durations_ptr) {
  uint32_t frame_count = image->getFrameCount();
  jint *frame_durations_array = env->GetIntArrayElements(frame_durations_ptr, nullptr);
  for (uint32_t i = 0; i < frame_count; ++i) {
    std::shared_ptr<ApngFrame> frame = image->getFrame(i);
    if (!frame) {
      break;
    }
    frame_durations_array[i] = frame->getDuration();
  }
  env->ReleaseIntArrayElements(frame_durations_ptr, frame_durations_array, 0);
}

static bool readStreamFully(JNIEnv *env, jobject inputStream, std::vector<uint8_t> &out) {
  jclass is_class = env->FindClass("java/io/InputStream");
  if (!is_class) {
    return false;
  }
  jmethodID readMethod = env->GetMethodID(is_class, "read", "([BII)I");
  if (!readMethod) {
    env->DeleteLocalRef(is_class);
    return false;
  }
  const jsize CHUNK = 64 * 1024;
  jbyteArray buffer = env->NewByteArray(CHUNK);
  if (!buffer) {
    env->DeleteLocalRef(is_class);
    return false;
  }

  bool ok = true;
  while (true) {
    jint read = env->CallIntMethod(inputStream, readMethod, buffer, 0, CHUNK);
    if (env->ExceptionOccurred()) {
      env->ExceptionClear();
      ok = false;
      break;
    }
    if (read < 0) {
      break; // EOF
    }
    if (read == 0) {
      continue;
    }
    size_t old_size = out.size();
    out.resize(old_size + static_cast<size_t>(read));
    env->GetByteArrayRegion(buffer, 0, read, reinterpret_cast<jbyte *>(out.data() + old_size));
  }

  env->DeleteLocalRef(buffer);
  env->DeleteLocalRef(is_class);
  return ok;
}

static void copyStreamDurations(JNIEnv *env,
                                const std::shared_ptr<ApngStreamDecoder> &decoder,
                                jintArray frame_durations_ptr) {
  const std::vector<uint32_t> &durations = decoder->getDurations();
  jint *frame_durations_array = env->GetIntArrayElements(frame_durations_ptr, nullptr);
  for (size_t i = 0; i < durations.size(); ++i) {
    frame_durations_array[i] = static_cast<jint>(durations[i]);
  }
  env->ReleaseIntArrayElements(frame_durations_ptr, frame_durations_array, 0);
}

static void setStreamResultFields(JNIEnv *env,
                                  jobject result,
                                  const std::shared_ptr<ApngStreamDecoder> &decoder) {
  env->SetIntField(result, gResult_widthFieldID, decoder->getWidth());
  env->SetIntField(result, gResult_heightFieldID, decoder->getHeight());
  env->SetIntField(result, gResult_frameCountFieldID, decoder->getFrameCount());
  env->SetIntField(result, gResult_repeatCountFieldID, decoder->getLoopCount());
  env->SetLongField(result,
                    gResult_allFrameByteCountFieldID,
                    static_cast<jlong>(decoder->getAllFrameByteCount()));
}

}
