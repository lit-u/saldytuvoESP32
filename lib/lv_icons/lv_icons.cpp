#include "lv_icons.h"
#include "family_profiles.h"

// Ta pati tvarka/logika kaip main.cpp ADMIN_PERSON_ICONS[] (admin puslapio
// ikonos) — laikoma ATSKIRAI cia, kad lib/ui_screens/ galetu naudoti be
// priklausomybes nuo main.cpp.
static const char *PERSON_ICONS[PERSON_COUNT] = {
    "",                    // PERSON_UNKNOWN — nenaudojama
    LV_ICON_UNICORN,       // GRANDDAUGHTER_1 (Saulytė)
    LV_ICON_TURTLE,        // GRANDDAUGHTER_2 (Upytė)
    LV_ICON_MEDICAL,       // SON (Saulius)
    LV_ICON_HEART,         // WIFE (Monika)
    LV_ICON_BOW,           // SELF (Senelis)
};

const char *LvIcons_GetPersonIcon(RecognizedPerson person) {
    if (person < 0 || person >= PERSON_COUNT) return "";
    return PERSON_ICONS[person];
}
