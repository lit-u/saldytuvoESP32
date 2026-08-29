/*
 * LVGL ekranu kurimas/rodymas kiekvienai state machine busenai.
 * Sitas modulis zino TIK kaip nupiesti ekrana pagal PersonProfile —
 * NEZINO nieko apie radara/kamera/busenu perjungimo logika (tai —
 * app_state_machine atsakomybe).
 */
#pragma once
#include <lvgl.h>
#include "family_profiles.h"

void UI_Screens_Init();   // sukuria visus ekranus (kviesti karta, setup())

void UI_ShowStandby();                              // 1. Budejimo rezimas
void UI_ShowScanning();                              // "aptiktas judesys, atpazistama..."
void UI_ShowChildGreeting(const PersonProfile &p);   // 4a. vaikiskas ekranas + checklist
void UI_ShowAdultGreeting(const PersonProfile &p);   // 4b. suaugusiojo ekranas
