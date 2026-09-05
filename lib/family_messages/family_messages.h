/*
 * Seimos zinuciu "pastdezute" — NVS-persistuojama busena (Preferences).
 *
 * 2026-09-05 (vartotojo pastaba: "galime padaryti slapta skyriu, kuri matys
 * tiktai tas, kuri atpazins... reikia lenteleje dar vieno stulpelio") —
 * DVI atskiros zinutes kiekvienam zmogui:
 *   - PUBLIC: rodoma VISADA, net kai zmogus save pasirenka is "Kas tu?"
 *     meniu (UI_ShowPublicGreeting) — bet kas gali pamatyti.
 *   - PRIVATE: rodoma TIK kai kamera TIKRAI atpazista ta zmogu
 *     (UI_ShowAdultGreeting/UI_ShowChildGreeting) — niekada per viesa
 *     pasirinkima, nes tada nera jokio patvirtinimo, kad tai TIKRAI tas
 *     zmogus prie ekrano.
 *
 * Naudojimas:
 *   - Admin panele (main.cpp POST /admin/message, "kind" parametras)
 *     kvies FamilyMessages_Set(target, kind, text).
 *   - Sveikinimo ekranai (ui_screens) kvies FamilyMessages_Get(person, kind).
 */
#pragma once
#include "family_profiles.h"

#define FAMILY_MESSAGE_MAX_LEN 128

enum class MessageKind { PUBLIC, PRIVATE };

struct FamilyMessage {
    bool hasMessage;
    char text[FAMILY_MESSAGE_MAX_LEN];
    uint32_t setAtMillis;
};

void FamilyMessages_Init();
void FamilyMessages_Set(RecognizedPerson target, MessageKind kind, const char *text);
const FamilyMessage &FamilyMessages_Get(RecognizedPerson target, MessageKind kind);
void FamilyMessages_Clear(RecognizedPerson target, MessageKind kind);
