/*
 * Veido atpazinimo sasaja — REALIZUOTA per laptopo serveri (client-server,
 * zr. README "Serveris-pagrindu atpazinimas"). ESP32 tik nufotografuoja ir
 * POST'ina JPEG kadra i SECRET_SERVER_URL (include/secrets.h), laptopas
 * (Flask + DeepFace) atlieka aptikima+atpazinima ir grazina JSON.
 *
 * SVARBU: tai sąmoningas nukrypimas nuo pradinio "standalone, be išorinio
 * serverio" reikalavimo — priimta po nesekmingo on-device bandymo (zr.
 * eloquent-facelib-experiment saka, 1/110 ~0.9% sekmes rodiklis). Laptopas
 * TURI buti ijungtas ir pasiekiamas per WiFi, kad atpazinimas veiktu.
 */
#pragma once
#include "family_profiles.h"

void FaceRecognition_Init();

// Paima kadra, POST'ina i serveri, isparsuoja JSON atsakyma. Grazina
// PERSON_UNKNOWN, jei: veidas nerastas/neatpazintas serverio puseje, ARBA
// serveris nepasiekiamas/timeout (laptopas isjungtas ar ne tame tinkle) —
// abiem atvejais NEPAKIMBA, kad likusi sistema (deep sleep ir t.t.) veiktu.
// SVARBU: BLOKUOJANTI (kelios sekundes) — LVGL/animacijos SUSTOJA per si
// laika. Naujam kodui naudoti ASINCHRONINE versija zemiau.
RecognizedPerson FaceRecognition_Identify();

// ASINCHRONINE versija (2026-09-04, vartotojo pastaba: "uzsaldyta" SCANNING
// animacija per laukima) — HTTP kvietimas vyksta ATSKIRAME FreeRTOS task'e,
// tad main loop() gali toliau kviesti lv_timer_handler()/animuoti akis VISA
// laukimo laika. Kviesti IdentifyAsync() KARTA (grazina is karto), tada
// tikrinti IsBusy() kiekviename loop() cikle; kai IsBusy()==false, rezultatas
// pasiekiamas per GetResult().
void FaceRecognition_IdentifyAsync();
bool FaceRecognition_IsBusy();
RecognizedPerson FaceRecognition_GetResult();

// TESTAVIMUI/FALLBACK: rankiniu budu "priverstinai" nustato atpazinta asmeni,
// aplenkiant realu serverio kvietima. Naudinga UI/state machine derinimui
// arba kaip atsarginis variantas, jei serveris laikinai nepasiekiamas.
void FaceRecognition_DebugForce(RecognizedPerson person);
void FaceRecognition_DebugClear();
