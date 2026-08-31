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
RecognizedPerson FaceRecognition_Identify();

// TESTAVIMUI/FALLBACK: rankiniu budu "priverstinai" nustato atpazinta asmeni,
// aplenkiant realu serverio kvietima. Naudinga UI/state machine derinimui
// arba kaip atsarginis variantas, jei serveris laikinai nepasiekiamas.
void FaceRecognition_DebugForce(RecognizedPerson person);
void FaceRecognition_DebugClear();
