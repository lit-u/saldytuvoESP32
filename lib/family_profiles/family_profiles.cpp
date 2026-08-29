#include "family_profiles.h"

// TODO: vardus/spalvas laisvai keisk pagal seimos pageidavimus.
static const PersonProfile PROFILES[PERSON_COUNT] = {
    /* PERSON_UNKNOWN */
    {
        PERSON_UNKNOWN, "Svecias", CATEGORY_ADULT,
        lv_color_hex(0x202020), lv_color_hex(0x808080),
        nullptr,
    },
    /* PERSON_GRANDDAUGHTER_1 */
    {
        PERSON_GRANDDAUGHTER_1, "Emilija", CATEGORY_CHILD,
        lv_color_hex(0xFFD93D), lv_color_hex(0xFF6B6B),
        "/greetings/granddaughter1.wav",
    },
    /* PERSON_GRANDDAUGHTER_2 */
    {
        PERSON_GRANDDAUGHTER_2, "Kotryna", CATEGORY_CHILD,
        lv_color_hex(0x6BCB77), lv_color_hex(0x4D96FF),
        "/greetings/granddaughter2.wav",
    },
    /* PERSON_SON */
    {
        PERSON_SON, "Sunus", CATEGORY_ADULT,
        lv_color_hex(0x2C3E50), lv_color_hex(0xE67E22),
        "/greetings/son.wav",
    },
    /* PERSON_WIFE */
    {
        PERSON_WIFE, "Zmona", CATEGORY_ADULT,
        lv_color_hex(0x34495E), lv_color_hex(0xE74C3C),
        "/greetings/wife.wav",
    },
    /* PERSON_SELF */
    {
        PERSON_SELF, "Seimininkas", CATEGORY_ADULT,
        lv_color_hex(0x2C3E50), lv_color_hex(0x27AE60),
        "/greetings/self.wav",
    },
};

const PersonProfile &FamilyProfiles_Get(RecognizedPerson id) {
    if (id < 0 || id >= PERSON_COUNT) id = PERSON_UNKNOWN;
    return PROFILES[id];
}
