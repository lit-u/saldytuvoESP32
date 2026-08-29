/*
 * Veido atpazinimo sasaja — SIANDIEN tik stub'as (visada grazina
 * PERSON_UNKNOWN arba testavimo reiksme), kad state machine ir UI galetu
 * buti kuriami ir testuojami NELAUKIANT realaus OV5640 atpazinimo modelio.
 *
 * TODO (ateities zingsnis): realizuoti su esp-who / ESP-DL biblioteka
 * (Espressif oficialus veido aptikimo+atpazinimo sprendimas ESP32-S3
 * su PSRAM). Tada:
 *   1. FaceRecognition_Init() ikrauna/inicijuoja modeli is SD/flash.
 *   2. FaceRecognition_Identify() paima kadra is esp_camera_fb_get(),
 *      paleidzia aptikima+atpazinima, grazina atitinkamo PersonProfile ID
 *      arba PERSON_UNKNOWN, jei asmuo neatpazintas/nera duomenu baze.
 */
#pragma once
#include "family_profiles.h"

void FaceRecognition_Init();

// Grazina atpazinta asmeni. Kol modelis neipildytas, visada PERSON_UNKNOWN.
RecognizedPerson FaceRecognition_Identify();

// TESTAVIMUI: leidzia rankiniu budu "priverstinai" nustatyti atpazinta
// asmeni (pvz. is Serial konsoles arba busimo web serverio /debug endpoint),
// kol realaus atpazinimo dar nera. Naudinga UI/state machine derinimui.
void FaceRecognition_DebugForce(RecognizedPerson person);
