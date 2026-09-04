#include "face_recognition.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_camera.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "secrets.h"

static bool s_debugActive = false;
static RecognizedPerson s_debugForced = PERSON_UNKNOWN;

// Vardas (serverio JSON "name") -> RecognizedPerson, remiantis TIK esamais
// family_profiles.cpp displayName ("Svecias" praleidziamas — PERSON_UNKNOWN
// yra numatytoji reiksme, ne registruojamas asmuo).
static RecognizedPerson mapNameToPerson(const String &name) {
    if (name.length() == 0) return PERSON_UNKNOWN;
    for (int i = 1; i < PERSON_COUNT; i++) {
        const PersonProfile &p = FamilyProfiles_Get((RecognizedPerson)i);
        if (name.equals(p.displayName)) return (RecognizedPerson)i;
    }
    return PERSON_UNKNOWN;
}

void FaceRecognition_Init() {
    Serial.printf("[FaceRecognition] Paruosta. Serveris: %s\n", SECRET_SERVER_URL);
}

RecognizedPerson FaceRecognition_Identify() {
    if (s_debugActive) return s_debugForced;

    if (WiFi.status() != WL_CONNECTED) {
        // Nepakibti — laptopas/tinklas laikinai nepasiekiamas, likusi
        // sistema (deep sleep ir t.t.) turi veikti toliau.
        return PERSON_UNKNOWN;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr) return PERSON_UNKNOWN;

    HTTPClient http;
    // Diagnostika 2026-09-03: A/B testas parode, kad telefono ATPAZINIMAS
    // pats greitas (~2s per localhost), bet failo PERDAVIMAS i telefona per
    // apkrauta WiFi hotspot (telefonas VIENU METU AP + atpazinimo serveris)
    // gali uztrukti 30+ s. 40s (buvo 15s) — saugumo atsarga, kol matuojame
    // SVGA (buvo SXGA) itaka realiam laikui.
    http.setTimeout(40000);
    http.begin(SECRET_SERVER_URL);
    http.addHeader("Content-Type", "image/jpeg");

    uint32_t requestStartMs = millis();
    int httpCode = http.POST(fb->buf, fb->len);
    uint32_t elapsedMs = millis() - requestStartMs;
    Serial.printf("[FaceRecognition] Uzklausa uztruko %.1fs (kadras %u baitu)\n",
                  elapsedMs / 1000.0f, fb->len);
    esp_camera_fb_return(fb);

    RecognizedPerson result = PERSON_UNKNOWN;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[FaceRecognition] JSON parse klaida: %s\n", err.c_str());
        } else {
            const char *name = doc["name"] | "";
            result = mapNameToPerson(String(name));
            Serial.printf("[FaceRecognition] Serveris: name=\"%s\" -> %s\n", name,
                          result == PERSON_UNKNOWN
                              ? "PERSON_UNKNOWN"
                              : FamilyProfiles_Get(result).displayName);
            // Diagnostika: kai "unknown", telefono serveris grazina PRIEZASTI
            // (no_face_detected/too_different) + artimiausia atstuma — svarbu
            // atskirti "kamera nerado veido" nuo "rado, bet per skirtingas".
            if (result == PERSON_UNKNOWN && doc["reason"].is<const char *>()) {
                Serial.printf("[FaceRecognition] Priezastis: %s", doc["reason"].as<const char *>());
                if (doc["closest_distance"].is<float>()) {
                    Serial.printf(" (artimiausias: %s, atstumas=%.4f)",
                                  doc["closest_name"] | "?",
                                  doc["closest_distance"].as<float>());
                }
                Serial.println();
            }
        }
    } else {
        Serial.printf("[FaceRecognition] HTTP klaida: %d (%s)\n", httpCode,
                      http.errorToString(httpCode).c_str());
    }

    http.end();
    return result;
}

// --- Asinchronine versija (2026-09-04) --------------------------------------
// FaceRecognition_Identify() vykdomas ATSKIRAME FreeRTOS task'e, kad main
// loop() galetu toliau kviesti lv_timer_handler() (taigi ir animuoti akis)
// per visa keliu sekundziu HTTP laukima — anksciau visa UI "uzsaldavo".
static volatile bool s_asyncBusy = false;
static volatile RecognizedPerson s_asyncResult = PERSON_UNKNOWN;
static TaskHandle_t s_asyncTaskHandle = nullptr;

static void faceRecognitionTask(void *param) {
    (void)param;
    RecognizedPerson result = FaceRecognition_Identify();
    s_asyncResult = result;
    s_asyncBusy = false;
    s_asyncTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void FaceRecognition_IdentifyAsync() {
    if (s_asyncBusy) return;  // jau vyksta — nepradeti antro lygiagreciai
    s_asyncBusy = true;
    s_asyncResult = PERSON_UNKNOWN;
    // Stack 8KB — HTTPClient+ArduinoJson+TLS stack naudojimui pakankamai;
    // core 0 (WiFi/protokolu core), kad neblokuotu Arduino loop() core (1).
    xTaskCreatePinnedToCore(faceRecognitionTask, "faceRecog", 8192, nullptr,
                             1, &s_asyncTaskHandle, 0);
}

bool FaceRecognition_IsBusy() {
    return s_asyncBusy;
}

RecognizedPerson FaceRecognition_GetResult() {
    return s_asyncResult;
}

void FaceRecognition_DebugForce(RecognizedPerson person) {
    s_debugActive = true;
    s_debugForced = person;
}

void FaceRecognition_DebugClear() {
    s_debugActive = false;
    s_debugForced = PERSON_UNKNOWN;
}
