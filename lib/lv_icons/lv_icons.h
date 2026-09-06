/*
 * Šeimos narių emoji ikonos LVGL ekranams — 2026-09-06, vartotojo pastaba:
 * "tas pačias ikonas įmesk į esp meniu ir asmeninius puslapius" (tos
 * pačios ikonos, kaip jau pridėtos admin puslapyje: unicorn/vėžlys/
 * stetoskopas/širdis/lankas, žr. main.cpp ADMIN_PERSON_ICONS[]).
 *
 * KLIŪTIS: LVGL numatytieji `lv_font_montserrat_*` (ir mūsų lietuviški
 * `lv_font_lt_*`, žr. lib/lv_fonts_lt/) NETURI emoji glifų — tikri emoji
 * (🦄🐢🩺 ir t.t.) yra arba SPALVOTI bitmap fontai (Segoe UI Emoji —
 * ŠIAME kompiuteryje NEĮDIEGTAS, ir LVGL apskritai nemoka piešti spalvotų
 * bitmap glifų be papildomos `LV_USE_IMGFONT` sąrankos), arba vektoriniai
 * kontūrai. Google „Noto Emoji" (monochrome, google/fonts repo,
 * ofl/notoemoji/NotoEmoji[wght].ttf) yra VIENSPALVIS KONTŪRINIS variantas
 * su beveik visais emoji kodų taškais — TINKA lv_font_conv, kaip ir bet
 * koks kitas TTF šriftas (ta pati technika kaip lietuviškiems raidėms,
 * žr. lib/lv_fonts_lt/lv_fonts_lt.h).
 *
 * PRIVALOMI --no-compress --no-prefilter (LV_USE_FONT_COMPRESSED 0
 * lv_conf.h faile — žr. lv_fonts_lt.h pilną paaiškinimą, tas pats
 * gotcha pasikartotų čia be šių vėliavėlių).
 *
 * Regeneruoti (jei reikia papildomu ikonu ar dydziu):
 *   1) Atsisiusti https://raw.githubusercontent.com/google/fonts/main/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf
 *   2) npx lv_font_conv --font NotoEmoji.ttf --size N --bpp 4 \
 *        --format lvgl --no-compress --no-prefilter \
 *        -r "0x1F984,0x1F422,0x2695,0x2764,0x1F3F9" \
 *        --lv-font-name lv_icons_N --lv-fallback lv_font_lt_N -o lv_icons_N.c
 *
 * 2026-09-06 (vartotojo pastaba: "pakeisk vien tik stetoskopą zmogaus
 * galva su stetoskopu, ar yra toks") — TIKSLAUS "zmogaus galva su
 * stetoskopu" (t.y. "🧑‍⚕️" health worker) Unikode NERA kaip VIENAS kodo
 * taskas — tai ZWJ (Zero-Width-Joiner) sujungta sekele (asmuo + ZWJ +
 * medicinos simbolis + variacijos selektorius), o `lv_font_conv` -r
 * flag'as veikia PER KODO TASKA, NE per GSUB ligaturu sudetingumu —
 * negali sudeti keliu kodo tasku i viena glifa. FIX: pakeista i ⚕
 * (U+2695, "Medical Symbol" — Asklepijo lazda), universaliai atpazistamas
 * medicinos/daktaro simbolis, aiskesnis uz izoliuota stetoskopo forma
 * mazame dydyje.
 */
#pragma once
#include <lvgl.h>
#include "family_profiles.h"

extern const lv_font_t lv_icons_22;  // dydis atitinka lv_font_lt_22 (pvz. "Kas tu?" mygtukai)
extern const lv_font_t lv_icons_28;  // dydis atitinka lv_font_lt_28 (pvz. sveikinimo uzrasas)

// UTF-8 emoji simboliai — TA PATI tvarka kaip main.cpp ADMIN_PERSON_ICONS[]
// (GRANDDAUGHTER_1, GRANDDAUGHTER_2, SON, WIFE, SELF). Nera tikslaus
// "ragatkos" emoji Unikode — Seneliui naudojamas artimiausias (lankas+strele).
#define LV_ICON_UNICORN     "🦄"  // Saulytė
#define LV_ICON_TURTLE      "🐢"  // Upytė
#define LV_ICON_MEDICAL     "⚕"  // Saulius (medicinos simbolis, ne stetoskopas)
#define LV_ICON_HEART       "❤"  // Monika
#define LV_ICON_BOW         "🏹"  // Senelis

// Grazina UTF-8 ikonos eilute konkreciam zmogui (arba "", jei PERSON_UNKNOWN
// ar neteisingas indeksas) — naudoti kartu su lv_icons_22/28 sriftu.
const char *LvIcons_GetPersonIcon(RecognizedPerson person);
