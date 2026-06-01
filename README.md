# ApngDrawable

[![Maven Central](https://img.shields.io/maven-central/v/com.linecorp/apng)](https://search.maven.org/artifact/com.linecorp/apng)

ApngDrawable is fast and light weight Animated Portable Network Graphics(APNG) image decoder library for Android platform.
ApngDrawable is written in Kotlin and C++.

## How to use

Use Gradle to build the library. Download it from [Maven Central](https://search.maven.org/artifact/com.linecorp/apng) and add configurations in the `build.gradle` file as follows.

```build.gradle
// In your top-level project's `build.gradle`
allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

// In your app project's `build.gradle`
dependencies {
  implementation 'com.linecorp:apng:x.y.z'
}
```

If using Kotlin Gradle DSL, add configurations in the `build.gradle.kts` file as follows.

```build.gradle.kts
// In your top-level project's `build.gradle.kts`
allprojects {
    repositories {
        mavenCentral()
    }
}

// In your app project's `build.gradle.kts`
dependencies {
  implementation("com.linecorp:apng:x.y.z")
}
```

## Getting started

You can decode from a lot of source types, e.g. File, InputStream and Resources.

```kotlin
// Decode from File
val drawable1 = ApngDrawable.decode(File("path/to/file"))

// Decode from InputStream
val drawable2 = File("path/to/file").inputStream().use {
    ApngDrawable.decode(it)
}

// Decode from Resources
val drawable3 = ApngDrawable.decode(context.resources, R.raw.apng_image)
```

You can find a more advanced way of using the library from the [example](https://github.com/line/apng-drawable/tree/master/sample-app).

## Decode modes

Every `decode` overload accepts an optional `decodeMode` that controls how frames
are produced in the native layer:

| Mode | Memory | Drawing / seeking | Best for |
| --- | --- | --- | --- |
| `DecodeMode.EAGER` (default) | All frames composed up front: `frames × width × height × 4` bytes | O(1) — a copy of a pre-composed frame | Small or short APNGs |
| `DecodeMode.ON_DEMAND` | Roughly constant in the frame count (a single composition canvas plus the buffered encoded bytes) | Forward playback decodes one frame per displayed frame; backward seeks are O(target) because APNG frames are deltas replayed from the start | Large or many-frame APNGs |

`EAGER` is the default and keeps the original behavior. Opt into streaming for
large images:

```kotlin
import com.linecorp.apng.decoder.DecodeMode

// Decode a large/many-frame APNG with roughly constant memory.
val drawable = ApngDrawable.decode(
    context.resources,
    R.raw.large_apng_image,
    decodeMode = DecodeMode.ON_DEMAND
)
```

The sample app exposes an **On-demand** toggle and an on-screen memory/CPU panel
so you can compare the two modes side by side.

## How to build

Note: This operation is necessary when building from code. It's not necessary if you are reading using `implementation` as shown in "[How to use]".

The patched `libpng` sources aren't included in the repository.
You need to download `libpng` and apply APNG patch first.

```sh
$ cat libpng_version | xargs ./download_libpng_and_apply_apng_patch.sh
$ ./gradlew :sample-app:assembleDebug
```


## How to contribute to ApngDrawable

See [CONTRIBUTING.md](CONTRIBUTING.md)

If you believe you have discovered a vulnerability or have an issue related to security, please contact the maintainer directly or send us a email to dl_oss_dev@linecorp.com before sending a pull request.

## License

```
Copyright 2018 LINE Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
