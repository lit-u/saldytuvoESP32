#include "family_profiles.h"

// 2026-09-05: vardai (publicName) patvirtinti su vartotoju — TIK tikri
// pirmi vardai, ne rysio pavadinimai (ne "Sunus"/"Zmona" ir pan.).
// displayName lieka NEPALIESTAS ten, kur jau realiai uzregistruotas
// telefono atpazinimo serveryje (zr. struct komentara) — kol kas TIK
// PERSON_SELF/"Seimininkas" turi tikru enrollment'a, likusiems keturiems
// displayName jau iskart sutampa su publicName (nera ko islaikyti).
static const PersonProfile PROFILES[PERSON_COUNT] = {
    /* PERSON_UNKNOWN */
    {
        PERSON_UNKNOWN, "Svecias", "Svecias", "Sveci", CATEGORY_ADULT,
        lv_color_hex(0x202020), lv_color_hex(0x808080),
        nullptr,
    },
    /* PERSON_GRANDDAUGHTER_1 — vokatyvas sutampa su nominatyvu. */
    {
        PERSON_GRANDDAUGHTER_1, "Saulytė", "Saulytė", "Saulytė", CATEGORY_CHILD,
        lv_color_hex(0xFFD93D), lv_color_hex(0xFF6B6B),
        "/greetings/granddaughter1.wav",
    },
    /* PERSON_GRANDDAUGHTER_2 — ta pati pastaba kaip virs. */
    {
        PERSON_GRANDDAUGHTER_2, "Upytė", "Upytė", "Upytė", CATEGORY_CHILD,
        lv_color_hex(0x6BCB77), lv_color_hex(0x4D96FF),
        "/greetings/granddaughter2.wav",
    },
    /* PERSON_SON — vokatyvas "Sauliau" (vartotojo pastaba 2026-09-05). */
    {
        PERSON_SON, "Saulius", "Saulius", "Sauliau", CATEGORY_ADULT,
        lv_color_hex(0x2C3E50), lv_color_hex(0xE67E22),
        "/greetings/son.wav",
    },
    /* PERSON_WIFE — vokatyvas sutampa su nominatyvu ("Monika" nesikeicia). */
    {
        PERSON_WIFE, "Monika", "Monika", "Monika", CATEGORY_ADULT,
        lv_color_hex(0x34495E), lv_color_hex(0xE74C3C),
        "/greetings/wife.wav",
    },
    /* PERSON_SELF */
    {
        // displayName="Seimininkas" TYCIA nekeiciamas — TIKSLIAI sutampa su
        // telefono embeddings.json enrollment'u (zr. face_recognition.cpp).
        // publicName="Senelis" (buvo "OldBoy"), vocativeName="Seneli"
        // (vartotojo pastaba 2026-09-05: "Labas, Seneli", ne "Labas, Senelis").
        PERSON_SELF, "Seimininkas", "Senelis", "Seneli", CATEGORY_ADULT,
        lv_color_hex(0x2C3E50), lv_color_hex(0x27AE60),
        "/greetings/self.wav",
    },
};

const PersonProfile &FamilyProfiles_Get(RecognizedPerson id) {
    if (id < 0 || id >= PERSON_COUNT) id = PERSON_UNKNOWN;
    return PROFILES[id];
}
