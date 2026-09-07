#include "app_state_machine.h"
#include "face_recognition.h"
#include "ui_screens.h"
#include "lcd_st7796.h"
#include "eye_renderer.h"
#include "audio_output.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>

// 2026-09-06: JPEG->maza RGB888 dekodavimas (lv_tjpgd.c naujas viesas
// funkcija) — zr. ui_screens.h UI_ScanningShowPhoto() komentara del
// priezasties, kodel LVGL incremental TJpgDec+scale kelias nenaudojamas.
extern "C" bool lv_tjpgd_decode_thumbnail(const uint8_t *jpegData, size_t jpegLen,
                                           uint16_t targetW, uint16_t targetH,
                                           uint8_t *outBuf, size_t outBufSize);
static uint8_t *s_scanThumbBuf = nullptr;
static const uint16_t SCAN_THUMB_W = 240;
static const uint16_t SCAN_THUMB_H = 180;

static AppState s_state = APP_STATE_STANDBY;
static uint32_t s_lastMotionMs = 0;
static uint32_t s_scanStartMs = 0;
static uint32_t s_recognizeStartMs = 0;   // kada prasidejo TIKRAS HTTP laukimas (po WAKE+blykstes)
static uint32_t s_lastStatusUpdateMs = 0; // sekundziu skaitliuko atnaujinimo laikas
static uint32_t s_pickingStartMs = 0;     // kada parodytas "Kas tu?" mygtuku ekranas
static bool s_captureStarted = false;

static void onWakeSequenceDone();
static void onPersonPicked(RecognizedPerson person);
static void onMenuPressed();

// Kiek laiko laukti mygtuko paspaudimo PICKING ekrane (vartotojo pastaba
// 2026-09-05: "Kas tu?" mygtukai po nesekmingo atpazinimo), pries pasiduodant
// ir grystant miegoti — "kiekvienam veiksmui uzrasas" principas galioja ir
// cia (zr. AppStateMachine_Update APP_STATE_PICKING atveji).
// 2026-09-07 (vartotojo pastaba: "Meniu langas turi fiksuotą automatinį
// išsijungimą. Todėl perklausai kelias žinutes ir jis išsijungia. Padaryk
// automatinį kuo didesnį (gal 2 min)") — 20s -> 2min, nes Meniu ekranas
// dabar taip pat naudojamas keliu balso zinuciu isklausymui (zr.
// onPersonBadgeTapped()), kas gali uztrukti ilgiau nei paprastas "Kas tu?"
// pasirinkimas.
static const uint32_t PICKING_TIMEOUT_MS = 120000;

// Kiek laiko be judesio grizti i STANDBY is GREETING (=RECOGNIZED) busenos.
// TODO: pagal README "atviri klausimai" — sitas skaicius v1 kontekste
// reiskia "kiek laiko rodyti ekrana po pasisveikinimo pries miegant".
// Koreguoti kai turesim realu energijos biudzeta is fizinio testo.
static const uint32_t SCREEN_AWAKE_TIMEOUT_MS = 15000;
// Nuotraukos rodymui (APP_STATE_SHOWING_PHOTO) — zr. komentara ties sios
// busenos update() sakiu del priezasties, kodel ilgesnis nei GREETING.
static const uint32_t PHOTO_AWAKE_TIMEOUT_MS = 60000;
// Saugumo riba SCANNING busenai (2026-09-04, po perejimo i asinchronini
// atpazinima) — TIK apsauga, jei FreeRTOS task'as kazkodel niekada
// nebaigtu (paciam HTTP kvietimui jau yra savas 40s timeout'as viduje,
// zr. face_recognition.cpp).
//
// KLAIDA rasta 2026-09-05 (vartotojo bandymas: 40s "Atpazistama...", tada
// "Per ilgai uztruko"): SI riba buvo skaiciuojama nuo s_scanStartMs (WAKE
// SEKOS pradzios), NE nuo s_recognizeStartMs (tikro HTTP kvietimo pradzios,
// kuris prasideda ~2.9s (WAKE ~2.3s + blykste ~0.6s) VELIAU). Todel realus
// buferis virs vidinio 40s HTTP timeout'o buvo TIK ~2s — pakankamai maza,
// kad saugumo riba galejo suveikti PRIES pacio HTTPClient timeout'a spejant
// grazinti svaru "nezinomas" rezultata (kas duotu "Nepazinau", ne "Per ilgai
// uztruko"), IR PALIKTI FreeRTOS task'a "pakibusi" fone (s_asyncBusy lieka
// true, kol jis pats galiausiai baigiasi) — sekantis mygtuko paspaudimas per
// ta langa GALEJO tyliai nieko nedaryti. FIX: matuoti NUO s_recognizeStartMs
// IR palikti tikra 10s buferi virs 40s.
static const uint32_t FACE_SCAN_SAFETY_TIMEOUT_MS = 50000;

static void enterStandby() {
    EyeRenderer_StopSequence();
    LCD_Backlight_Set(0);
    UI_ShowStandby();
    s_state = APP_STATE_STANDBY;
}

// 2026-09-05: vartotojo pastaba — "kiekvienam veiksmui uzrasas, nes
// animacijos nepakanka". Prieš isjungiant ekrana (STANDBY) po NEsekmingo
// atpazinimo (ar saugumo timeout'o), PARODOMA aiski priezastis + "einu
// miegoti" prieš pat uzgestant — vaikas neturi likti spelioti, kodel dezute
// tiesiog uzgeso. Trumpi delay() cia priimtini (sio ekrano PASKUTINIS
// momentas pries STANDBY, ne daugiasekundis HTTP laukimas).
static void enterStandbyWithMessage(const char *reasonText) {
    UI_SetScanningStatusText(reasonText);
    lv_timer_handler();
    delay(1200);
    EyeRenderer_SetState(EYE_STATE_SLEEP);
    UI_SetScanningStatusText("Einu miegoti...");
    lv_timer_handler();
    delay(900);
    enterStandby();
}

// Linksmas spejimas prieš "Kas tu?" mygtukus (vartotojo pastaba 2026-09-05:
// "turi linksmai speleoti uzrasais") — trumpa, statine seka, tas pats
// trumpu-delay() pattern'as kaip enterStandbyWithMessage() (paskutinis
// SCANNING ekrano momentas pries kita ekrana, ne daugiasekundis laukimas).
static const char *GUESS_TEXTS[] = {
    "Hmm... gal tu Saulius?",
    "O gal Monika?",
    "Nejau Senelis?",
    "Gal Saulytė ar Upytė?",
};
static const size_t GUESS_TEXTS_COUNT = sizeof(GUESS_TEXTS) / sizeof(GUESS_TEXTS[0]);

static void enterPicking() {
    for (size_t i = 0; i < GUESS_TEXTS_COUNT; i++) {
        UI_SetScanningStatusText(GUESS_TEXTS[i]);
        lv_timer_handler();
        delay(800);
        if (i == 1) EyeRenderer_Blink();  // truputis "gyvumo" per spejima
    }
    UI_SetScanningStatusText("Nepažinau! Paspausk save:");
    lv_timer_handler();
    delay(900);

    UI_ShowNamePicker(onPersonPicked);
    s_pickingStartMs = millis();
    s_state = APP_STATE_PICKING;
}

// Kviecianas TIESIOGIAI is LVGL mygtuko paspaudimo ivykio (zr. ui_screens.cpp
// nameButtonEventCb) — VIESAS (ne privatus) profilis, zr. UI_ShowPublicGreeting
// komentara. Pakartotinai naudoja GREETING busenos auto-miego timeout'a
// (SCREEN_AWAKE_TIMEOUT_MS), tad papildomo kodo tam nereikia.
static void onPersonPicked(RecognizedPerson person) {
    if (s_state != APP_STATE_PICKING) return;  // apsauga nuo pavelavusio ivykio
    const PersonProfile &profile = FamilyProfiles_Get(person);
    UI_ShowPublicGreeting(profile);
    s_lastMotionMs = millis();
    s_state = APP_STATE_GREETING;
}

// Universalus "Meniu" mygtukas (vartotojo pastaba 2026-09-05: "jau iskart
// kai pasileidzia ir bet ka daro, visada apacioje kaireje turi buti aktyvi
// nuoroda Meniu, kuri iskart soka i 5 pasirinkimus") — VEIKIA BET KURIOJE
// busenoje (SCANNING/GREETING/PICKING), nes tiesiogiai perjungia s_state,
// nelaukdama jokio proceso pabaigos (ta pati logika kaip PWR mygtuko
// "isjungejo" — foninis atpazinimo task'as, jei vyksta, tiesiog ignoruojamas).
static void onMenuPressed() {
    EyeRenderer_StopSequence();
    LCD_Backlight_Set(100);
    UI_ShowNamePicker(onPersonPicked);
    s_pickingStartMs = millis();
    s_state = APP_STATE_PICKING;
}

// 2026-09-07 (vartotojo pastaba: "vaikams be adminkes galimybe irasyti
// trumpa teksta... Meniu skyriuje - visiems skirta"; VELIAU: "tegu būna
// neištrinta, nes gali norėti daug žmonių išklausyti. Išsitrina tada, kai
// tas žmogus parašo kitą žinutę") — "Kas tu?" ekrane paspaudus "Palikti
// žinutę" + varda, IRASOMA balso zinute VISIEMS. VIENAS FIKSUOTAS failas
// KIEKVIENAM zmogui ("/inbox/<personId>.wav") — nauja sio ZMOGAUS zinute
// PERRASO sena JO PATIES (tai priimtina — vartotojo sprendimas), bet
// NIEKADA neliecia KITU zmoniu failu (skirtingi personId => skirtingi
// failai). Audio_RecordToFile() yra BLOKUOJANTIS ir kviecamas TIESIOGIAI is
// LVGL mygtuko paspaudimo ivykio — TAS PATS saugus pattern'as, kaip jau
// naudojamas soundButtonEventCb() (ui_screens.cpp) su Audio_PlayFile().
// 2026-09-07 (vartotojo pastaba: "Pailginkime žinutę iki 10 sek"): 6s->10s.
static const uint32_t MESSAGE_RECORD_MS = 10000;

static void inboxPathFor(RecognizedPerson person, char *outPath, size_t outSize) {
    snprintf(outPath, outSize, "/inbox/%d.wav", (int)person);
}

// 2026-09-07 (vartotojo pastaba: "Per įrašymą tegu eina atbulinis
// cauntdown sek") — Audio_RecordToFile() kviecia SITA KARTA per sekunde;
// atnaujina overlay teksta su likusiu sekundziu skaicium. Paprastas laisvo
// tipo funkcijos rodykle (audio_output.h) — jokios busenos NEIŠSAUGOME
// cia, viskas paskaiciuojama is (elapsedS,totalS) parametru.
static void onRecordTick(uint32_t elapsedS, uint32_t totalS) {
    uint32_t remaining = (totalS > elapsedS) ? (totalS - elapsedS) : 0;
    char buf[24];
    snprintf(buf, sizeof(buf), "🔴 %lus", (unsigned long)remaining);
    UI_ShowMessageRecordingOverlay(buf);
}

static void onMessageSenderPicked(RecognizedPerson person) {
    const PersonProfile &profile = FamilyProfiles_Get(person);
    char recMsg[64];
    snprintf(recMsg, sizeof(recMsg), "Įrašoma... (%s)", profile.vocativeName);
    UI_ShowMessageRecordingOverlay(recMsg);
    delay(600);  // trumpa pauze, kad "Įrašoma... (Vardas)" spetu buti perskaitytas pries prasidedant skaitliukui

    if (!LittleFS.exists("/inbox")) LittleFS.mkdir("/inbox");
    char path[32];
    inboxPathFor(person, path, sizeof(path));
    Audio_RecordToFile(path, MESSAGE_RECORD_MS, nullptr, onRecordTick);  // PERRASO sena SIO ZMOGAUS zinute, jei buvo

    UI_ShowMessageRecordingOverlay("Įrašyta! Ačiū :)");
    delay(1200);
    UI_HideMessageRecordingOverlay();
    UI_MarkPersonMessageSent(person);  // nauja zinute — visada raudonas taskas, net jei sena buvo jau isklausyta
    UI_RefreshInboxIndicator();
    // 2026-09-07 (vartotojo pastaba: "Po žinutės įrašymo išsijungia, tegu
    // eina į meniu") — anksciau CIA buvo enterStandby(); dabar VIETOJ TO
    // grystama i Meniu (ta pati logika kaip GREETING timeout, zr.
    // AppStateMachine_Update() komentara).
    onMenuPressed();
}

// 2026-09-07 (vartotojo pastaba: "dabar negali pasirinkti kieno žinutę
// klausysi... vardo ženkliukas veda į asmeninį, todėl šalia bus kitas") —
// paspaudus KONKRETAUS zmogaus zenkla Meniu ekrane, groja TO zmogaus
// zinute, parodo "Nuo: Vardas" (vartotojo pastaba: "kai groja, rašyti nuo
// ko"). Failas NETRINAMAS (vartotojo pastaba: "gali norėti daug žmonių
// išklausyti") — tik pazymima kaip isklausyta (zalias taskas).
static void onPersonBadgeTapped(RecognizedPerson person) {
    char path[32];
    inboxPathFor(person, path, sizeof(path));
    if (!LittleFS.exists(path)) return;  // apsauga — badge neturetu buti matomas be zinutes

    const PersonProfile &profile = FamilyProfiles_Get(person);
    char msg[48];
    snprintf(msg, sizeof(msg), "Nuo: %s", profile.publicName);
    UI_ShowMessageRecordingOverlay(msg);

    Audio_PlayFile(path);

    UI_HideMessageRecordingOverlay();
    UI_MarkPersonMessageHeard(person);
    UI_RefreshInboxIndicator();
}

// 2026-09-04 (pokalbis su ChatGPT): vietoj to, kad kiekviena efekta
// programuotume atskirai ("dar pridekim mirktelejima..."), scenarijus dabar
// yra timeline/sequencer (zr. eye_renderer.h) — sita funkcija tik PALEIDZIA
// choreografuota "pabudimo" seka, o tikras fotografavimas prasideda TIK kai
// ji baigiasi (onWakeSequenceDone). Pakeicia buvusi tuscia SCAN_WARMUP_MS
// laukima + statini SCAN_BLINK_INTERVAL_MS mirksejima.
static void enterScanning() {
    LCD_Backlight_Set(100);
    UI_ShowScanning();
    // "Kiekvienam veiksmui turi buti uzrasas" (vartotojo pastaba 2026-09-05)
    // — sitas konkretus tekstas atitinka WAKE seka (akys apsidairo), toliau
    // keiciamas onWakeSequenceDone() kiekvienai sekanciai fazei.
    UI_SetScanningStatusText("Sveiki! Ruošiuosi...");
    // BUTINA: priverstinis piesimo ciklas, kad SCANNING ekranas (akys+
    // tekstas) fiziskai pasirodytu PRIES paleidziant atpazinimo uzklausa
    // (vartotojo pastaba 2026-09-04: "nebuvo SCANNING akiu, tik
    // pasveikinimas po pauzes").
    lv_timer_handler();
    s_scanStartMs = millis();
    s_captureStarted = false;
    s_state = APP_STATE_SCANNING;
    // KLAIDA rasta 2026-09-07 (po ilgos "blykste nesuveikia" diagnostikos —
    // I2C visada sekmingas, laikas TIKSLIAI atitinka CAMERA_FLASH_MS, bet
    // fiziskai blykstes NIEKADA nesimato): onWakeSequenceDone() anksciau buvo
    // kvieciamas TIESIOGIAI is eye_renderer.cpp sequencer'io LVGL timer
    // callback'o vidaus (zr. playCurrentStep() "if (cb) cb();"). O
    // onWakeSequenceDone() savo ruoztu kviecia lv_timer_handler() KELIS
    // KARTUS (UI_ShowCameraFlashOn() + laukimo ciklas) — REKURSYVUS
    // lv_timer_handler() kvietimas IS LVGL timer callback'o vidaus yra
    // nesaugus/neapibreztas elgesys (LVGL nera re-entrant), galintis
    // sugadinti/praleisti bet kokio TA PACIA akimirka sukurto objekto
    // (baltos blykstes overlay) piesima. FIX: NEBEPERDUODAME callback'o
    // tiesiogiai — laukiam sekos pabaigos per EyeRenderer_IsSequencePlaying()
    // pooling'a is AppStateMachine_Update() (main loop(), NE is LVGL timer
    // vidaus) — zr. APP_STATE_SCANNING atveji zemiau.
    EyeRenderer_PlayWakeSequence(nullptr);
}

// Kviecianas is eye_renderer sequencer'io, kai WAKE seka baigiasi (~2.3s).
// Cia tinkamas momentas pradeti tikra fotografavima — vartotojas per sita
// laika turejo laiko atsistoti/pasiruosti (buvusio SCAN_WARMUP_MS tikslas).
// Kiek laiko laikyti LCD "blykste" (baltas ekranas) apsvietimui — tiek, kad
// tikrai apimtu FaceRecognition_IdentifyAsync() task'o paleidima IR jo
// pirma esp_camera_fb_get() kvietima (paprastai keli ms po task'o starto).
// 2026-09-05: 350ms nepakako kontraviesos atveju (zr. main.cpp AE_LEVEL
// pastaba) — prailginta, daugiau laiko sensoriui pilnai pritaikyti AE prie
// naujos (sviesesnes) LCD apsvietimo situacijos PRIES kadro paemima.
// 2026-09-06 (vartotojo pastaba: "pirma-antra karta buvo sviesiau, bet
// toliau vel tamsu") — sensoriaus TESTINE automatine ekspozicija (AEC/AGC)
// dreifuoja link tamsaus tarp skanavimu (kamera mato daugiausia juoda
// ekrano fona), tad kuo ilgiau prietaisas budi, tuo TOLIAU nuo sviesios
// busenos ji nudreifuoja, ir 600ms nebeuztenka pilnai atsigauti. Bandymas:
// gerokai prailginta (1500ms), kad AEC turetu daugiau laiko konverguoti
// AUKSTYN, nepriklausomai nuo to, kaip toli ji nudreifavo.
static const uint32_t CAMERA_FLASH_MS = 1500;

static void onWakeSequenceDone() {
    // Apsauga: jei per ta laika jau grizom i STANDBY (pvz. rankiniu budu ar
    // kitu mechanizmu), neverta pradeti fotografavimo.
    if (s_state != APP_STATE_SCANNING) return;
    // "Blykste" tamsiam kambariui (vartotojo pastaba 2026-09-04: "tamsu
    // kambaryje, neatpazista") — plokstej NERA atskiro kameros LED (zr.
    // io_extension.h), tad LCD ekranas (arti veido) panaudojamas kaip
    // apsvietimo saltinis. Sviesa uzdegama PRIES paleidziant async task'a IR
    // laikoma per visa fotografavimo langa (trumpas delay() cia priimtinas —
    // tai NE daugiasekundis HTTP laukimas, o vienkartinis ~0.35s "blyksnis",
    // per kuri niekas kitas is esmes neturetu animuotis).
    // KLAIDA rasta 2026-09-06 (vartotojo pastaba: "labai tamsi nuotrauka,
    // o apsvietimas pusiau geras", net PADIDINUS gain ceiling 16X->64X BE
    // JOKIO pokycio): delay(CAMERA_FLASH_MS) buvo CIA, PO
    // FaceRecognition_IdentifyAsync() — bet tas async task'as (kitame core,
    // face_recognition.cpp) paima kadra (esp_camera_fb_get()) BEVEIK IS
    // KARTO po starto (per kelis ms), NE po delay(). Taigi delay() TIK
    // ilgino, kiek laiko blykste MATOSI ekrane PO kadro paemimo — sensoriaus
    // automatinei ekspozicijai (AEC) NEBUVO duota jokio laiko prisitaikyti
    // prie naujai apsviesto (LCD blykste) vaizdo PRIES fotografuojant. FIX:
    // delay PERKELTAS PRIES FaceRecognition_IdentifyAsync() — dabar sensoris
    // tikrai turi laiko AEC konvergencijai naujoje sviesoje PRIES kadro
    // paemima. Trumpas papildomas buferis PO async starto uztikrina, kad
    // blykste dar dega, kol kitas core realiai pradeda fotografuoti.
    UI_SetScanningStatusText("Fotografuojama...");
    UI_ShowCameraFlashOn();
    delay(CAMERA_FLASH_MS);
    FaceRecognition_IdentifyAsync();
    // KLAIDA rasta 2026-09-07 (serial log diagnostika: kadro LUMA nuosekliai
    // krito 134->87->36->32 per sekancius scan'us, NORS backlight PWM I2C
    // rasymas VISADA sekmingas su ta pacia reiksme) — aklas delay(50) CIA
    // NEGARANTUODAVO, kad esp_camera_fb_get() (async task'e, kitame core)
    // realiai jau bus ivykes PRIES gesinant blykste. SVGA JPEG fiksavimas
    // gali uztrukti ilgiau nei 50ms, tad blykste galejo issijungti PRIES ar
    // VIDURYJE realios sensoriaus ekspozicijos. FIX: laukti (su saugikliu),
    // kol FaceRecognition_IsFrameCaptured() patvirtins, kad fb_get() jau
    // ivyko, TIK TADA gesinti — zr. face_recognition.h komentara.
    // KLAIDA rasta 2026-09-07 (vartotojo pastaba: "ekranas nieko nerodo" po
    // PWR/blykstes — TIK SCANNING/blykstes sekos metu, STANDBY akys rodomos
    // gerai) — sitas laukimo ciklas anksciau kviete TIK delay(5), NIEKADA
    // lv_timer_handler(). Kol laukimas buvo trumpas (~50ms, senas kodas),
    // niekas nepastebejo — bet PRIDEJUS apsilimo kadrus (face_recognition.cpp,
    // 3x papildomas esp_camera_fb_get()), sis laukimas gali uztrukti gerokai
    // ilgiau (kelias JPEG SVGA kadru fiksavimo trukmes), o SPI ekrano
    // atnaujinimui/LVGL vidinei busenai reikia periodinio lv_timer_handler()
    // "pumpavimo" — ilgas blokavimas be jo galejo palikti ekrana tuscia/pilka.
    uint32_t flashWaitStartMs = millis();
    while (!FaceRecognition_IsFrameCaptured() && millis() - flashWaitStartMs < 3000) {
        lv_timer_handler();
        delay(5);
    }
    UI_ShowCameraFlashOff();
    s_recognizeStartMs = millis();
    s_lastStatusUpdateMs = s_recognizeStartMs;
    UI_SetScanningStatusText("Atpažįstama... (0s)");

    // 2026-09-06 (vartotojo pastaba: "noriu, kad kai vyksta atpažinimas,
    // žmogus jau matytų savo foto, kurią bandoma atpažinti") — kadras
    // paprastai jau nufotografuotas siuo momentu (FaceRecognition_
    // IdentifyAsync() task'as paima ji per keliolika ms nuo starto, o mes
    // ka tik palaukeme CAMERA_FLASH_MS=600ms). Jei kazkodel dar neparuostas
    // (labai letas task startas), NEBLOKUOJAME — tiesiog paliekame senaji
    // aki animacija kaip atsargini variantą.
    //
    // JPEG (800x600 SVGA) dekoduojamas PATYS i maza RGB888 miniaturia CIA
    // (main loop/core 1, 20KB stack — saugu), NE per LVGL/TJpgDec incremental
    // kelia (žr. ui_screens.h/lv_tjpgd.c komentarus del ilgos diagnostikos su
    // ChatGPT, kuri rado realu LVGL+scale+TJpgDec nesuderinamuma dideliems
    // vaizdams).
    const uint8_t *frameData = nullptr;
    size_t frameLen = 0;
    uint16_t frameW = 0, frameH = 0;
    bool photoShown = false;
    if (FaceRecognition_GetLastFrame(&frameData, &frameLen, &frameW, &frameH)) {
        if (!s_scanThumbBuf) {
            s_scanThumbBuf = (uint8_t *)heap_caps_malloc((size_t)SCAN_THUMB_W * SCAN_THUMB_H * 3, MALLOC_CAP_SPIRAM);
        }
        if (s_scanThumbBuf &&
            lv_tjpgd_decode_thumbnail(frameData, frameLen, SCAN_THUMB_W, SCAN_THUMB_H,
                                       s_scanThumbBuf, (size_t)SCAN_THUMB_W * SCAN_THUMB_H * 3)) {
            // 2026-09-07 diagnostika (ChatGPT konsultacija del "labai tamsi
            // nuotrauka") — objektyvus dekoduoto kadro rysklumo (luma) matas,
            // nepriklausomas nuo zmogaus akies/ekrano — leidzia atskirti, ar
            // KAMERA realiai gauna skirtinga apsvietima tarp scan ciklu, ar
            // problema tik LCD/backlight puseje (zr. io_extension.cpp PWM logus).
            {
                uint32_t sumLuma = 0;
                const uint32_t pixCount = (uint32_t)SCAN_THUMB_W * SCAN_THUMB_H;
                for (uint32_t i = 0; i < pixCount; i++) {
                    const uint8_t *px = s_scanThumbBuf + i * 3;
                    sumLuma += (uint32_t)(px[0] + px[1] + px[2]) / 3;
                }
                Serial.printf("[AppState] Kadro VIDUTINE LUMA=%lu (0-255, is %lu pikseliu) millis=%lu\n",
                              (unsigned long)(sumLuma / pixCount), (unsigned long)pixCount, millis());
            }
            Serial.printf("[AppState] Miniatiura dekoduota (%ux%u is %ux%u) — rodoma nuotrauka.\n",
                          SCAN_THUMB_W, SCAN_THUMB_H, frameW, frameH);
            UI_ScanningShowPhoto(s_scanThumbBuf, SCAN_THUMB_W, SCAN_THUMB_H);
            photoShown = true;
        } else {
            Serial.println("[AppState] Miniaturos dekodavimas NEPAVYKO — atsarginis variantas (akys).");
        }
    } else {
        Serial.println("[AppState] Kadras UI rodymui DAR NEGATAS — atsarginis variantas (akys).");
    }
    if (!photoShown) {
        EyeRenderer_PlayRecognizingLoop();
    }
    s_captureStarted = true;
}

// Cia istatomas veido atpazinimo rezultatas — sitas switch() yra vieta,
// kur kiekvienam seimos nariui priskiriamas jo ekranas (README: "RECOGNIZED").
// PASTABA: si funkcija kviesiama TIK kai person != PERSON_UNKNOWN (zr.
// AppStateMachine_Update SCANNING atveji) — v1 sutarta, kad neatpazinus per
// timeout NEBERA jokio "svecio" ekrano, tiesiog griztama i STANDBY.
static void enterGreeting(RecognizedPerson person) {
    const PersonProfile &profile = FamilyProfiles_Get(person);

    switch (person) {
        case PERSON_GRANDDAUGHTER_1:
        case PERSON_GRANDDAUGHTER_2:
            UI_ShowChildGreeting(profile);
            break;

        case PERSON_SON:
        case PERSON_WIFE:
        case PERSON_SELF:
        default:
            UI_ShowAdultGreeting(profile);
            break;
    }

    s_state = APP_STATE_GREETING;
}

void AppStateMachine_Init() {
    UI_Screens_Init(onMenuPressed, onMessageSenderPicked, onPersonBadgeTapped);
    FaceRecognition_Init();
    enterStandby();
}

void AppStateMachine_Update(bool motionDetected) {
    if (motionDetected) {
        s_lastMotionMs = millis();
    }

    // 2026-09-05 (vartotojo pastaba): PWR mygtukas turi veikti KAIP
    // ISJUNGEJAS bet kuriuo metu, jei kazkas vyksta — ne tik pazadinti is
    // STANDBY. Paspaudus SCANNING/PICKING/GREETING metu, TIESIOGIAI
    // grystama i STANDBY, nelaukiant jokio proceso (atpazinimo, mygtuko
    // pasirinkimo ir t.t.) pabaigos. Foninis FreeRTOS atpazinimo task'as
    // (jei tuo metu vyksta) liks veikti ir baigsis pats — jo rezultatas
    // tiesiog bus ignoruojamas (ta pati logika kaip saugumo timeout'e).
    if (motionDetected && s_state != APP_STATE_STANDBY) {
        enterStandby();
        return;
    }

    switch (s_state) {
        case APP_STATE_STANDBY:
            if (motionDetected) {
                enterScanning();
            }
            break;

        case APP_STATE_SCANNING: {
            if (!s_captureStarted) {
                // Sekos pabaigos pooling'as (zr. enterScanning() komentara) —
                // SAUGUS, nes cia esame main loop() kontekste, NE LVGL timer
                // callback'o viduje.
                if (!EyeRenderer_IsSequencePlaying()) {
                    onWakeSequenceDone();
                }
                // Dar vyksta choreografuota WAKE seka (eye_renderer
                // sequencer'is) — jos pabaigoje onWakeSequenceDone() pati
                // pradeda fotografavima (FaceRecognition_IdentifyAsync).
                // lv_timer_handler() (main.cpp loop()) varo animacija toliau.
                break;
            }

            if (FaceRecognition_IsBusy()) {
                // Dar laukiama HTTP atsakymo (task'as fone) — RECOGNIZING
                // seka (eye_renderer sequencer'is) toliau "gyvena" akimis.
                // Sekundziu skaitliukas (vartotojo pastaba 2026-09-05: "turi
                // eiti sekundes, nes lauki lauki ir nezinai, kas vyksta") —
                // atnaujinamas kas ~1s, kad butu aisku, jog sistema NE
                // uzstrigusi, o tiesiog dar laukia atsakymo.
                if (millis() - s_lastStatusUpdateMs >= 1000) {
                    s_lastStatusUpdateMs = millis();
                    uint32_t elapsedS = (millis() - s_recognizeStartMs) / 1000;
                    char buf[32];  // UTF-8 "ž"/"į" uzima po 2 baitus — daugiau vietos nei grynas ASCII
                    snprintf(buf, sizeof(buf), "Atpažįstama... (%lus)", (unsigned long)elapsedS);
                    UI_SetScanningStatusText(buf);
                }
                // Saugumo riba — TIK jei task'as niekada nebaigtu. Matuojama
                // NUO s_recognizeStartMs (tikro HTTP kvietimo pradzios), NE
                // nuo s_scanStartMs — zr. FACE_SCAN_SAFETY_TIMEOUT_MS pastaba.
                if (millis() - s_recognizeStartMs > FACE_SCAN_SAFETY_TIMEOUT_MS) {
                    enterStandbyWithMessage("Per ilgai užtruko...");
                }
                break;
            }

            EyeRenderer_StopSequence();
            RecognizedPerson person = FaceRecognition_GetResult();
            if (person != PERSON_UNKNOWN) {
                enterGreeting(person);
                break;
            }
            // 2026-09-05 (vartotojo pastaba): neatpazinus — NE tiesiai
            // miegoti, o linksmas spejimas + "Kas tu?" mygtukai (zr.
            // enterPicking()), kad bet kas galetu pats pasirinkti savo varda.
            enterPicking();
            break;
        }

        case APP_STATE_PICKING: {
            uint32_t elapsed = millis() - s_pickingStartMs;
            // Niekas nepaspaude per PICKING_TIMEOUT_MS — pasiduodam, bet
            // (ta pati "kiekvienam veiksmui uzrasas" taisykle) aiskiai
            // parodome, kad grystama miegoti, ne tiesiog uzgesus tyliai.
            if (elapsed > PICKING_TIMEOUT_MS) {
                UI_SetPickingWarnActive(false);
                enterStandbyWithMessage("Niekas nepasirinko...");
                break;
            }
            // 2026-09-07 (vartotojo pastaba: "Kai lieka 10 sek iki
            // išsijungimo, tegu ima vis dažniau mirksėti... prieš 10 sek [1
            // blyksnis]... dukart... prieš 6 sekundes... triskart prieš 3
            // sek") — vis dažnesnis zalio indikatoriaus blyksejimas, kad
            // vartotojas MATYTU laika baigiantis, ne tiesiog netiketai
            // uzgestu ekranas bekalbant/beklausant zinutes.
            uint32_t remaining = PICKING_TIMEOUT_MS - elapsed;
            bool warnOn =
                (remaining <= 10000 && remaining > 9500) ||
                (remaining <= 6000 && remaining > 5500) ||
                (remaining <= 5000 && remaining > 4500) ||
                (remaining <= 3000 && remaining > 2500) ||
                (remaining <= 2000 && remaining > 1500) ||
                (remaining <= 1000 && remaining > 500);
            UI_SetPickingWarnActive(warnOn);
            break;
        }

        case APP_STATE_GREETING:
            // 2026-09-07 (vartotojo pastaba: "asmeniniame profilyje taip pat
            // greit automatiškai išsijungia... tegu ne išsijungia, o nueina
            // į Meniu") — anksciau cia buvo enterStandby() (visiskas
            // uzmigimas); dabar VIETOJ TO grystama i Meniu ("Kas tu?"), kad
            // vartotojas galetu iskart pasirinkti/perziureti kita zmogu (ar
            // paklausyti kito zinutes) BE reikalo is naujo zadinti irengini.
            // Galioja IR tikram atpazinimui (Adult/Child), IR pasirinktam is
            // saraso (Public) — abu naudoja TA PATI APP_STATE_GREETING.
            if (!motionDetected && (millis() - s_lastMotionMs > SCREEN_AWAKE_TIMEOUT_MS)) {
                onMenuPressed();
            }
            break;

        case APP_STATE_SHOWING_PHOTO:
            // 2026-09-06: IS PRADZIU naudojo ta pati SCREEN_AWAKE_TIMEOUT_MS
            // (15s) kaip GREETING — testuojant nuotolinio P10 nuotraukos
            // kelio (kartais 60-90s uzsitesiantis ESP<->telefonas HTTP
            // round-trip per lete WiFi jungti), vartotojas nespedavo net
            // pribegti pazuret i ekrana, kol jis JAU buvo isjunges — atrode
            // kaip "nuotrauka neveikia", nors is tikruju viskas nusisiunte
            // ir parodyta korektiskai. FIX: ilgesnis, atskiras timeout SIAI
            // busenai (nuotrauka NE toks daznas veiksmas kaip GREETING, tad
            // energijos kaina priimtina).
            if (!motionDetected && (millis() - s_lastMotionMs > PHOTO_AWAKE_TIMEOUT_MS)) {
                enterStandby();
            }
            break;
    }
}

AppState AppStateMachine_GetState() {
    return s_state;
}

// 2026-09-06 (vartotojo pastaba: "gali trukdyti musu saldytuvo esp-32
// programa. Isijungia, o veliau uzgesta. Gal reikia ta nuotraukos rodyma
// integruoti i saldytuvo esp?") — zr. app_state_machine.h pastaba del
// pirmo (nepavykusio) bandymo.
void AppStateMachine_ShowPhoto(const uint8_t *jpegData, size_t jpegLen, uint16_t width, uint16_t height) {
    EyeRenderer_StopSequence();
    LCD_Backlight_Set(100);
    UI_ShowPhoto(jpegData, jpegLen, width, height);
    s_lastMotionMs = millis();  // pakartotinai naudoja SCREEN_AWAKE_TIMEOUT_MS
    s_state = APP_STATE_SHOWING_PHOTO;
}
