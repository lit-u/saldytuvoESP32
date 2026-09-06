"""
2026-09-06: `.pio/libdeps/` yra gitignore'intas (PlatformIO pats atsisiunčia
priklausomybes iš naujo per kiekvieną "pio run" švarioje aplinkoje) — bet
`lib/lv_tjpgd/lv_tjpgd.c`/`.h` (LVGL vendored TJpgDec adapteris) TURI
papildomą funkciją `lv_tjpgd_decode_thumbnail()`, kurios be šito automatinio
"patch" žingsnio NEBŪTŲ jokioje šviežioje aplinkoje (nauja mašina, `.pio`
ištrinta rankiniu būdu ir pan.) — ir foto rodymas SCANNING ekrane vėl
nustotų veikti be jokio matomo pakeitimo šaltinio kode.

Šis skriptas paleidžiamas PRIEŠ kiekvieną build (`extra_scripts = pre:...`,
žr. platformio.ini) ir tiesiog nukopijuoja `patches/lv_tjpgd/*.{c,h}` (kurie
YRA git'e) VIRŠ atitinkamų failų `.pio/libdeps/.../lvgl/src/libs/tjpgd/` —
jei PlatformIO dar nespėjo atsisiųsti LVGL (pirmas build švarioje mašinoje),
šis žingsnis tyliai NIEKO nedaro (katalogo dar nėra) ir PlatformIO pats
atsisiunčia priklausomybę PRIEŠ kompiliavimą — antras `pio run` paleidimas
(arba paprasčiausiai šis skriptas, jei PlatformIO iškviečia jį po
priklausomybių atsisiuntimo per tą patį build'ą) pritaikys pataisymą.
"""
import os
import shutil

Import("env")  # noqa: F821 (PlatformIO SCons globalas)

PROJECT_DIR = env.get("PROJECT_DIR")  # noqa: F821
PATCH_SRC_DIR = os.path.join(PROJECT_DIR, "patches", "lv_tjpgd")
TARGET_DIR = os.path.join(
    PROJECT_DIR, ".pio", "libdeps", "esp32-s3-cam", "lvgl", "src", "libs", "tjpgd"
)


def apply_patch():
    if not os.path.isdir(TARGET_DIR):
        print("[lv_tjpgd patch] .pio/libdeps dar neegzistuoja (pirmas build) — praleidziama, "
              "PlatformIO pats atsisius LVGL PRIES kompiliavima.")
        return
    for filename in ("lv_tjpgd.c", "lv_tjpgd.h"):
        src = os.path.join(PATCH_SRC_DIR, filename)
        dst = os.path.join(TARGET_DIR, filename)
        shutil.copyfile(src, dst)
        print(f"[lv_tjpgd patch] Pritaikyta: {dst}")


apply_patch()
