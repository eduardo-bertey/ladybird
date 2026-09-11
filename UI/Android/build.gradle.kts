plugins {
    id("com.android.application") version "8.11.0"
    id("org.jetbrains.kotlin.android") version "2.1.20"
}

// ladybird: con -PusePrebuiltNative=true se saltea CMake por completo y se
// empaquetan las .so prebuilt de src/main/jniLibs/<abi>/ (bajadas del artifact
// ladybird-android-sos). Solo arm64-v8a por ahora; sin el flag todo igual que
// el oficial (CMake compila x86_64 + arm64-v8a).
val usePrebuiltNative = (project.findProperty("usePrebuiltNative") ?: "false") == "true"

android {
    namespace = "org.serenityos.ladybird"
    compileSdk = 35
    // FIXME: Replace the NDK version to a stable one (this is r29 beta 2)
    ndkVersion = "29.0.13599879"

    defaultConfig {
        applicationId = "org.serenityos.ladybird"
        minSdk = 30
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        if (!usePrebuiltNative) {
            externalNativeBuild {
                cmake {
                    cppFlags += "-std=c++23"
                    arguments += listOf(
                        "-DANDROID_STL=c++_shared",
                        "-DLADYBIRD_VCPKG_TYPE=release",
                        "-DVCPKG_TARGET_ANDROID=ON",
                        "-DENABLE_CRANELIFT_JIT=OFF"
                    )
                    System.getenv("LADYBIRD_HOST_LAYOUT_GENERATOR")?.let {
                        arguments += "-DLADYBIRD_HOST_LAYOUT_GENERATOR=$it"
                    }
                }
            }
        }
        ndk {
            // Specifies the ABI configurations of your native
            // libraries Gradle should build and package with your app.
            abiFilters += if (usePrebuiltNative) listOf("arm64-v8a") else listOf("x86_64", "arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions {
        jvmTarget = "11"
    }
    externalNativeBuild {
        if (!usePrebuiltNative) {
            cmake {
                path = file("../../CMakeLists.txt")
                version = "3.23.0+"
            }
        }
    }

    buildFeatures {
        viewBinding = true
        prefab = true
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test.ext:junit-ktx:1.1.5")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
}
