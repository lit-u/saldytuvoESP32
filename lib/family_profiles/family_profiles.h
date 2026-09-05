/*
 * Seimos nariu profiliai — statiniai duomenys, naudojami veido atpazinimo
 * (face_recognition) rezultatui susieti su UI tema, sveikinimu ir turiniu
 * (ui_screens).
 *
 * TODO: kai atsiras realus veido atpazinimas, kiekvienam PersonProfile
 * gali prireikti prideti embedding/reference nuotrauka SD korteleje.
 */
#pragma once
#include <Arduino.h>
#include <lvgl.h>

enum RecognizedPerson {
    PERSON_UNKNOWN = 0,
    PERSON_GRANDDAUGHTER_1,
    PERSON_GRANDDAUGHTER_2,
    PERSON_SON,
    PERSON_WIFE,
    PERSON_SELF,
    PERSON_COUNT   // visada paskutinis — masyvo dydziui
};

enum PersonCategory {
    CATEGORY_CHILD,
    CATEGORY_ADULT,
};

struct PersonProfile {
    RecognizedPerson id;
    // PRIVALO TIKSLIAI atitikti telefono atpazinimo serverio grazinama "name"
    // lauka (zr. face_recognition.cpp mapNameToPerson() — string palyginimas).
    // NEKEISTI be atitinkamo phone-side embeddings/enrollment pervadinimo!
    // Del sios priezasties PERSON_SELF ciа lieka "Seimininkas" (taip uzregistruota
    // telefone), NORS ekrane rodomas tikras vardas "OldBoy" (zr. publicName).
    const char *displayName;
    // Rodoma EKRANE (vartotojo pastaba 2026-09-05: "tik vardai, ne sunus/zmona
    // ir pan."). NEnaudojama atpazinimo palyginimui — gali laisvai keistis.
    const char *publicName;
    // Kreipinio (vokatyvo) forma — vartotojo pastaba 2026-09-05: "Labas,
    // Sauliau" ir "Labas, Seneli", NE "Labas, Saulius"/"Labas, Senelis"
    // (lietuviu k. kreipiantis vardas keiciasi). Naudojama TIK sveikinimo
    // sakiniuose (zr. ui_screens.cpp); publicName lieka nominatyvas mygtuku
    // etiketems/kitur, kur nera sakinio.
    const char *vocativeName;
    PersonCategory category;
    lv_color_t themeBg;       // fono spalva jo ekranui
    lv_color_t themeAccent;   // akcentine spalva
    const char *greetingAudioFile;   // TODO: SD kelias — v2, kartu su admin panele
};

// Grazina profilio nuoroda pagal ID. PERSON_UNKNOWN grazina saugu "tuscia" profili.
const PersonProfile &FamilyProfiles_Get(RecognizedPerson id);
