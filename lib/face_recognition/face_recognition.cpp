#include "face_recognition.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "secrets.h"

static bool s_debugActive = false;
static RecognizedPerson s_debugForced = PERSON_UNKNOWN;

// 2026-09-06: paskutinio nufotografuoto (atpazinimui siunciamo) kadro RAM
// kopija — zr. FaceRecognition_GetLastFrame() komentara header'yje.
static uint8_t *s_lastFrameBuf = nullptr;
static size_t s_lastFrameLen = 0;
static uint16_t s_lastFrameW = 0;
static uint16_t s_lastFrameH = 0;
static volatile bool s_lastFrameReady = false;
// 2026-09-07 diagnostika (žr. header'io FaceRecognition_IsFrameCaptured()
// komentarą) — atskiras nuo s_lastFrameReady, nes TAS liktų "true" nuo
// PRAEITO bandymo kol sis dar nespejo pats prasidėti (trumpas race'as
// tarp task'o starto ir pirmo esp_camera_fb_get() rezultato).
static volatile bool s_frameCaptured = false;

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

    // KLAIDA rasta 2026-09-07 (serial log diagnostika: kadro LUMA nuosekliai
    // krito per sekancius scan'us KIEKVIENAME firmware bandyme — 134->87->36->32,
    // veliau po flash-off sinchronizacijos fix'o VIS TIEK 52->19->21 — abu
    // kartus greitai pasiekiant "grinda"/plato). Backlight PWM I2C rasymas
    // VISADA sekmingas su ta pacia reiksme, blykstes-off sinchronizacija jau
    // sutvarkyta (zr. FaceRecognition_IsFrameCaptured()) — vadinasi, LIKUSI
    // priezastis yra PACIO SENSORIAUS AEC/AGC (automatines ekspozicijos)
    // BUSENA, kuri paveldima is PRAEITO (tamsaus, be blykstes) kadro ir
    // NESPEJA pilnai atsigauti per viena esp_camera_fb_get() kvietima naujoje
    // (skaisciai apsvieстoje) scenoje. FIX: keli "apsilimo" kadrai (paimami IR
    // ISKART ISMETAMI) PRIES realu (siunciama) kadra — standartine kameru
    // technika, duodanti AEC/AGC algoritmui kelis kadrus laiko konverguoti i
    // NAUJA (blykstes apsviesta) scena, ne i senaji (tamsu) buferio turini.
    for (int warmup = 0; warmup < 3; warmup++) {
        camera_fb_t *warmupFb = esp_camera_fb_get();
        if (warmupFb) esp_camera_fb_return(warmupFb);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    // BUTINA CIA (ne veliau): pats esp_camera_fb_get() kvietimas yra momentas,
    // kai sensorius fiziskai fiksuoja REALU (siunciama) kadra — LCD "blykste"
    // nebereikalinga laikyti dega po sito (zr. FaceRecognition_IsFrameCaptured()
    // header'yje) — apsilimo kadrai AUKSCIAU jau ivyko SU blykste dar dega.
    s_frameCaptured = true;
    if (fb == nullptr) return PERSON_UNKNOWN;

    // Kopija RAM (PSRAM) buferyje UI puse PRIES siunciant — kad vartotojas
    // matytu TIKSLIAI ta pati kadra, kuris iskart po sito siunciamas
    // atpazinimo serveriui (zr. FaceRecognition_GetLastFrame()).
    s_lastFrameReady = false;
    uint8_t *frameCopy = (uint8_t *)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
    if (frameCopy) {
        memcpy(frameCopy, fb->buf, fb->len);
        if (s_lastFrameBuf) heap_caps_free(s_lastFrameBuf);
        s_lastFrameBuf = frameCopy;
        s_lastFrameLen = fb->len;
        s_lastFrameW = fb->width;
        s_lastFrameH = fb->height;
        s_lastFrameReady = true;
        Serial.printf("[FaceRecognition] Kadras nukopijuotas UI rodymui: %ux%u, %u baitu\n",
                      fb->width, fb->height, (unsigned)fb->len);
    } else {
        Serial.println("[FaceRecognition] DIAG: nepavyko isskirti PSRAM buferio UI kadro kopijai.");
    }

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
    // Nustatoma CIA (kviecianciame/main thread'e), PRIES sukuriant task'a —
    // jokio race'o su task'o vidumi (zr. FaceRecognition_IsFrameCaptured()).
    s_frameCaptured = false;
    // Stack 8KB — HTTPClient+ArduinoJson+TLS stack naudojimui pakankamai;
    // core 0 (WiFi/protokolu core), kad neblokuotu Arduino loop() core (1).
    xTaskCreatePinnedToCore(faceRecognitionTask, "faceRecog", 8192, nullptr,
                             1, &s_asyncTaskHandle, 0);
}

bool FaceRecognition_IsBusy() {
    return s_asyncBusy;
}

bool FaceRecognition_IsFrameCaptured() {
    return s_frameCaptured;
}

RecognizedPerson FaceRecognition_GetResult() {
    return s_asyncResult;
}

bool FaceRecognition_GetLastFrame(const uint8_t **data, size_t *len, uint16_t *w, uint16_t *h) {
    if (!s_lastFrameReady || !s_lastFrameBuf) return false;
    *data = s_lastFrameBuf;
    *len = s_lastFrameLen;
    *w = s_lastFrameW;
    *h = s_lastFrameH;
    return true;
}

void FaceRecognition_DebugForce(RecognizedPerson person) {
    s_debugActive = true;
    s_debugForced = person;
}

void FaceRecognition_DebugClear() {
    s_debugActive = false;
    s_debugForced = PERSON_UNKNOWN;
}
