plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.srceng.launcher"
    compileSdk = 34

    // 签名：debug 与 release 统一使用仓库自带 debug.keystore（标准密码：androiddebugkey / android）
    // 目的：CI 产出的 release APK 也带签名，可直接安装；避免 unsigned release APK 被当成"没签名"装不了
    signingConfigs {
        create("shared") {
            val store = rootProject.file("app/debug.keystore")
            storeFile = store
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
            // v1 (JAR signing) + v2 (APK Signature Scheme v2) 全开：
            //  Android 7.0+ 优先 v2（更快更安全），老版本/老工具回落到 v1
            enableV1Signing = true
            enableV2Signing = true
            enableV3Signing = false
            enableV4Signing = false
        }
        getByName("debug") {
            val store = rootProject.file("app/debug.keystore")
            if (store.exists()) {
                storeFile = store
                storePassword = "android"
                keyAlias = "androiddebugkey"
                keyPassword = "android"
            }
            enableV1Signing = true
            enableV2Signing = true
            enableV3Signing = false
            enableV4Signing = false
        }
    }

    defaultConfig {
        applicationId = "com.srceng.launcher"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            // 只保留 CI 产出的 ABI，避免 APK 里 x86_64 空 ABI 导致运行时找不到 so
            abiFilters += listOf("armeabi-v7a", "arm64-v8a")
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        vectorDrawables {
            useSupportLibrary = true
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // release 也强制用 shared 签名，保证 CI 产出的 release APK 始终是 signed 的，能直接 adb install
            // v1+v2 在 signingConfigs.shared 里已开启
            signingConfig = signingConfigs.getByName("shared")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            // debug 用和 release 一样的 key（若仓库提供了 keystore），以便跨构建稳定签名
            // v1+v2 在 signingConfigs.debug 里已开启
            signingConfig = signingConfigs.findByName("debug").takeIf { it?.storeFile?.exists() == true }
                ?: signingConfigs.getByName("shared")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.8"
    }
    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
        // 与 AndroidManifest 中 extractNativeLibs=true 配套，允许系统把 so 解压到 /data/app/<pkg>/lib/
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    // Core Android
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
    implementation("androidx.activity:activity-compose:1.8.2")

    // Jetpack Compose
    implementation(platform("androidx.compose:compose-bom:2024.02.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.navigation:navigation-compose:2.7.7")

    // DataStore (Preferences)
    implementation("androidx.datastore:datastore-preferences:1.0.0")

    // Coroutines
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")

    // JSON
    implementation("com.squareup.moshi:moshi-kotlin:1.15.0")

    // Testing
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
    androidTestImplementation(platform("androidx.compose:compose-bom:2024.02.00"))
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
