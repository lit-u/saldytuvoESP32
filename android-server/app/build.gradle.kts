plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "lt.saldytuvas.recognizer"
    compileSdk = 34

    defaultConfig {
        applicationId = "lt.saldytuvas.recognizer"
        // Huawei P10 (VTR-L29) = Android 9 / API 28. minSdk placiau del bendro
        // suderinamumo, bet realus target irenginys yra API 28. compileSdk=34
        // reikalauja androidx.activity/androidx.core naujesnes versijos —
        // tai NEKEICIA, kokiuose irenginiuose app veikia (tai lemia minSdk).
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "0.1"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    // MobileFaceNet .tflite failas assets/ nepakuojamas/nekompresuojamas.
    androidResources {
        noCompress += "tflite"
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")

    // Veido APTIKIMAS (ne atpazinimas — zr. README, tikrintas faktas).
    implementation("com.google.mlkit:face-detection:16.1.6")

    // Veido ATPAZINIMAS (embedding) — MobileFaceNet per TFLite.
    implementation("org.tensorflow:tensorflow-lite:2.14.0")
    implementation("org.tensorflow:tensorflow-lite-support:0.4.4")

    // Embedded HTTP serveris (ESP32 POST'ina JPEG i sita, telefone veikiantis).
    implementation("org.nanohttpd:nanohttpd:2.3.1")

    // JSON atsakymui/embeddings saugojimui.
    implementation("org.json:json:20231013")
}
