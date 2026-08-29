/*
 * Seimos zinuciu "pastdezute" — paprasta atmintyje laikoma busena.
 *
 * Numatyta naudoti taip:
 *   - Busimas web serveris (ESPAsyncWebServer POST /api/message) kvies
 *     FamilyMessages_Set(target, text) kai sunus/zmona parasys zinute
 *     is telefono.
 *   - Suaugusiuju sveikinimo ekranas (ui_screens) kvies
 *     FamilyMessages_Get(person) ir, jei yra zinute, parodys ja ekrane.
 *   - Kai zinute parodyta (arba po tam tikro laiko), galima iskviesti
 *     FamilyMessages_Clear(person).
 *
 * TODO: siuo metu zinutes NEISSAUGOMOS i SD/flash — power-loss metu
 * dings. Jei reikes islaikyti tarp perkrovimu, perkelti i SD faila
 * arba NVS (Preferences biblioteka).
 */
#pragma once
#include "family_profiles.h"

#define FAMILY_MESSAGE_MAX_LEN 128

struct FamilyMessage {
    bool hasMessage;
    char text[FAMILY_MESSAGE_MAX_LEN];
    uint32_t setAtMillis;
};

void FamilyMessages_Init();
void FamilyMessages_Set(RecognizedPerson target, const char *text);
const FamilyMessage &FamilyMessages_Get(RecognizedPerson target);
void FamilyMessages_Clear(RecognizedPerson target);
