/*
 * Pritaikyti LVGL sriftai su lietuviskomis raidemis (a c e e i s u u z ir
 * didziosiomis) — vartotojo pastaba 2026-09-05: "Lietuviskai visur istaisyk".
 *
 * PRIEZASTIS #1 (kodel apskritai reikia sito): LVGL numatytieji
 * lv_font_montserrat_* sriftai (zr. lv_conf.h) apima tik Basic Latin +
 * Latin-1 Supplement — Lietuviskos raides (Ą Č Ę Ė Į Š Ų Ū Ž ir mazosios)
 * yra Unicode "Latin Extended-A" bloke, kurio numatytieji sriftai NETURI.
 *
 * PRIEZASTIS #2 (kodel PIRMAS bandymas 2026-09-05 NEVEIKE — "visai
 * nesimato raidziu"): sis projektas turi `LV_USE_FONT_COMPRESSED 0`
 * lv_conf.h faile — TAI VISISKAI ISJUNGIA suglaudintu (RLE) sriftu
 * dekodavima LVGL viduje. `lv_font_conv` PAGAL NUTYLEJIMA generuoja
 * suglaudinta srifto duomenu formata — tokio srifto naudoti sitame
 * projekte NEIMANOMA (LVGL negali ju dekoduoti, rezultatas — nematomos
 * raides). PATVIRTINTA per LVGL GitHub issue #8480 (lygiai tas pats
 * simptomas: "veikia kompiliuojant, bet ekranas tuscias").
 * FIX: generuoti su `--no-compress --no-prefilter` (zr. komanda apacioje).
 *
 * SUGENERUOTA su lv_font_conv (npx lv_font_conv), saltinis: Windows
 * C:/Windows/Fonts/segoeui.ttf (Segoe UI — Montserrat TTF sio kompiuterio
 * nebuvo, tad vizualiai NEidentiskas likusiam projekto tekstui, bet
 * funkcionaliai teisingas). Kiekvienas dydis turi --lv-fallback i
 * atitinkama lv_font_montserrat_N — bet koks simbolis, kurio siame sriftie
 * NERA (pvz. LV_SYMBOL_LEFT rodykle), automatiskai paimamas is numatytojo.
 *
 * Dydziai sumazinti (vartotojo pastaba 2026-09-05: "visur vos vos
 * sumazink") — 20->18, 22->20, 24->22, 32->28.
 *
 * Regeneruoti (jei reikia papildomu raidziu ar dydziu):
 *   npx lv_font_conv --font "C:/Windows/Fonts/segoeui.ttf" --size N --bpp 4 \
 *     --format lvgl --no-compress --no-prefilter \
 *     -r "0x20-0x7E,0x104,0x105,0x10C,0x10D,0x118,0x119,0x116,0x117,0x12E,\
 *     0x12F,0x160,0x161,0x172,0x173,0x16A,0x16B,0x17D,0x17E" \
 *     --lv-font-name lv_font_lt_N --lv-fallback lv_font_montserrat_N \
 *     -o lv_font_lt_N.c
 */
#pragma once
#include <lvgl.h>

extern const lv_font_t lv_font_lt_18;
extern const lv_font_t lv_font_lt_20;
extern const lv_font_t lv_font_lt_22;
extern const lv_font_t lv_font_lt_28;
