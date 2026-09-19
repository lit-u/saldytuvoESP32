# P10 serveris — SOURCE OF TRUTH

Atnaujinta 2026-09-19. Šis dokumentas aprašo faktinę P10 serverio posistemio būseną ir turi pirmenybę prieš pasenusius P10 būsenos teiginius README sesijų istorijoje. Istoriniai testai nėra pažadas, kad paslauga visada pasiekiama; diagnostikos komandos pateiktos žemiau.

**P10 24/7 server boot chain — VERIFIED. Antras kontroliuojamas Android reboot PASS:** Kotlin AI serveris, SSH ir HA atsistatė automatiškai, programų ir servisų rankiniu būdu nepaleidžiant. Kotlin BOOT_COMPLETED veikimas patvirtintas paslaugos paleidimo laiku ir veikiančiu :5000; tiesioginio receiver iškvietimo log įrašo neišliko.

## Architektūra ir versijos

```text
Huawei P10 VTR-L29 / Android 9 / EMUI 9.1
├── Kotlin AI server :5000
│   └── NanoHTTPD → ML Kit face detection → TFLite / MobileFaceNet
├── Tailscale
└── Termux 0.118.3
    ├── wake lock
    ├── runit (termux-services)
    │   ├── SSH :8022
    │   ├── Mosquitto MQTT :1883 (native Termux, su autentifikacija)
    │   └── Ubuntu/proot
    │       └── Home Assistant Core :8123
    └── Termux:Boot 0.8.1 → boot script → wake lock + runit

ESP32-S3 192.168.43.250 → HTTP JPEG → P10 192.168.43.51:5000/recognize
                      ← JSON name/distance ← atpažinimas telefone
```

| Komponentas | Faktinė būsena |
|---|---|
| Telefonas | Huawei P10 VTR-L29, Kirin 960, ARM64, 4 GB RAM klasė |
| Android / kernel | Android 9 / EMUI 9.1; kernel 4.9.148 |
| Android matoma RAM | ~3,63 GiB |
| P10 Wi-Fi IP | `192.168.43.51`; jungiasi prie Huawei P20 Pro hotspot, pats nėra šio tinklo AP |
| Kotlin | `lt.saldytuvas.recognizer`, NanoHTTPD `:5000`, ML Kit ir TFLite/MobileFaceNet |
| Termux | 0.118.3, F-Droid; ne Play Store |
| Termux:Boot | 0.8.1, F-Droid; parašas suderinamas su įdiegtu Termux; pirmą kartą paleistas |
| Priežiūra | termux-services 0.13-1, runit 2.1.2-4; ne systemd |
| SSH | OpenSSH, `:8022`, atskiras ED25519 raktas; password ir keyboard-interactive autentifikacija išjungtos |
| MQTT | Native Termux Mosquitto 2.1.2 (paketas 2.1.2-2), runit; anonymous uždraustas; [docs/MQTT.md](MQTT.md) |
| proot-distro | 5.8.0 |
| Ubuntu | 24.04.5 LTS ARM64 per proot; root vartotojas svečio aplinkoje nėra Android root |
| Sisteminis Python | `/usr/bin/python3` → Python 3.12.3; nepakeistas |
| HA Python | atskiras ARM64 Python 3.14.7; atskiras venv |
| Home Assistant | Core 2026.9.2, HTTP `0.0.0.0:8123` |
| Tailscale | Įdiegtas Android sluoksnyje, anksčiau konfigūruotas `192.168.43.0/24` subnet router. Dabartinis VPN pasiekiamumas šiame etape NOT TESTED; ankstesnėje diagnostikoje P10 buvo offline. LAN testas nepatvirtina Tailscale veikimo. |

Linux GUI, X11, KDE/XFCE, Wine ir GPU acceleration nenaudojami. MQTT brokeris įdiegtas; HA MQTT integracija dar neprijungta, nes HA pradinis onboarding neužbaigtas. HACS ir Node-RED neįdiegti. HA Core venv/proot diegimas yra šio projekto prižiūrimas sprendimas, ne oficialiai palaikomas HA OS/Container diegimo būdas. HA neperima Kotlin recognition funkcijos.

## Paleidimas ir proceso atsistatymas

Android boot → Termux:Boot → `00-p10-services` → `termux-wake-lock` → `start-services.sh` → runit → sshd ir Ubuntu/proot/HA.

2026-09-19 pridėtas native Termux Mosquitto runit servisas. Ankstesni VERIFIED reboot testai apima Kotlin/SSH/HA; naujo MQTT proceso SIGKILL recovery PASS, jo paleidimas po tikro telefono reboot dar netestuotas (šiame etape reboot nedarytas). Esamas boot skriptas nepakeistas.

Boot skripto faktinis turinys:

```sh
#!/data/data/com.termux/files/usr/bin/sh
export PATH=/data/data/com.termux/files/usr/bin:$PATH
termux-wake-lock
. /data/data/com.termux/files/usr/etc/profile.d/start-services.sh
```

HA runit `run` per `exec` paleidžia:

```sh
proot-distro login --no-link2symlink ubuntu -- /bin/bash /root/homeassistant/start.sh
```

Esamas `start.sh` aktyvina venv per PATH, nustato atskirą uv cache, riboja uv lygiagretumą (download 2, install/build 1), turi `UV_NO_BUILD=1`, įrašo HA PID ir per `exec` paleidžia:

```sh
/root/homeassistant/venv/bin/hass --config /root/homeassistant/config --log-file /root/homeassistant/home-assistant.log
```

Runit paleidžia procesą iš naujo tiek po HA restart exit code 100, tiek po netikėtos baigties. `finish` laukia 5 s po bent 60 s veikusio proceso; trumpai iš eilės krentančiam procesui laukimas didėja 10/20/40/60 s. Tai riboja restart ciklą. Pakartotinis boot skripto paleidimas tikrintas: papildomų SSH/HA kopijų nesukuria.

Runit neišgyvens viso Termux Android proceso/UID sustabdymo; tai nėra Android apribojimų apėjimas. Šis P10 neturi PIN ar slaptažodžio: vartotojo patvirtinimu, po antro reboot telefono nelietė, o visi trys serveriai atsistatė. Pirmo atrakinimo hipotezė ADB problemai šiame teste netiko; žr. EMUI diagnostiką žemiau. Bandymas neapima kitokios, PIN apsaugotos telefono konfigūracijos.

### Kotlin boot pakeitimas

- [AndroidManifest.xml](../android-server/app/src/main/AndroidManifest.xml): `RECEIVE_BOOT_COMPLETED` ir neeksportuotas `BootCompletedReceiver` su `BOOT_COMPLETED` intent filter.
- [BootCompletedReceiver.kt](../android-server/app/src/main/java/lt/saldytuvas/recognizer/BootCompletedReceiver.kt): tikrina intent action, per `ContextCompat.startForegroundService` paleidžia esamą `RecognitionForegroundService` (Android 9 suderinamas kelias).
- Paslauga iškart kviečia `startForeground`; esamas `server == null` saugiklis neleidžia pakartotinai sukurti NanoHTTPD serverio. Recognition, ML Kit, TFLite, API, `:5000` ir paslaugos architektūra nepakeisti.
- Pirmo reboot metu Kotlin nepasileido: tuo metu receiver dar nebuvo. Vėlesnis APK atnaujinimas įdiegė receiver; antras kontroliuojamas reboot patvirtino automatinį paleidimą — **PASS**.
- Po APK atnaujinimo, dar prieš antrą reboot, Kotlin buvo paleistas per esamą programos mygtuką regresijos testams. Per patį antrą reboot testą Kotlin ir Termux programos bei servisai **rankiniu būdu nepaleisti**.
- Tiesioginio receiver iškvietimo įrašo prieinamuose loguose neišliko. Automatinį paleidimą patvirtina `RecognitionForegroundService` sukūrimo laikas boot pradžioje, `isForeground=true` ir veikiantis :5000 be rankinio programos paleidimo.

### EMUI

Vartotojas patvirtino tiek Termux, tiek „Saldytuvo atpazinimas“ programoms: **Manage manually → Auto-launch ON, Secondary launch ON, Run in background ON**. Android battery optimization išimtys patikrintos abiem programoms; Kotlin `RUN_IN_BACKGROUND` default allow. Termux wake lock realiai stebėtas per `dumpsys power`. EMUI apsaugos neapeinamos, Android root nenaudojamas.

### ADB po reboot — patvirtintas praktinis sprendimas šiame P10

Po antro reboot serveriai jau veikė, bet `adbd=stopped`. Windows matė USB įrenginį be klaidų (error code 0), tas pats laidas visą laiką buvo prijungtas. Laptopo `adb kill-server` / `start-server` nepadėjo. Aktyvi `sys.usb.config` buvo `hisuite,mtp,mass_storage`, nors išsaugota `persist.sys.usb.config` turėjo ir `adb`.

Vartotojas patikrino: **USB debugging = ON**, **Allow ADB debugging in charge only mode = OFF**. Įjungus tik antrą nustatymą, neperjungiant USB režimo, nejudinant laido ir nedarant papildomo reboot, `adbd` iškart tapo `running`, aktyvi konfigūracija vėl įtraukė `adb`, ADB atsistatė. **Allow ADB debugging in charge only mode dabar ON.** Tai patvirtintas praktinis sprendimas šiame telefone; jis nebuvo reikalingas jau savarankiškai atsistačiusiems Kotlin, SSH ir HA servisams.

## Tikrieji failų keliai

Boot, runit ir HA skriptų bei logų keliai pakartotinai patikrinti per SSH 2026-09-19. Termux `$HOME` yra `/data/data/com.termux/files/home`, `$PREFIX` yra `/data/data/com.termux/files/usr`.

| Paskirtis | Kelias |
|---|---|
| Boot skriptas | `$HOME/.termux/boot/00-p10-services` |
| Runit paleidimas | `$PREFIX/etc/profile.d/start-services.sh` |
| SSH runit | `$PREFIX/var/service/sshd/run` |
| HA runit | `$PREFIX/var/service/homeassistant/run` |
| HA restart laukimas | `$PREFIX/var/service/homeassistant/finish` |
| HA log tarnyba | `$PREFIX/var/service/homeassistant/log/run` → `$PREFIX/share/termux-services/svlogger` |
| HA runit log | `$PREFIX/var/log/sv/homeassistant/current` |
| SSH runit log | `$PREFIX/var/log/sv/sshd/current` |
| HA svečio paleidimas | `/root/homeassistant/start.sh` Ubuntu viduje |
| Tas pats failas iš Termux | `$PREFIX/var/lib/proot-distro/containers/ubuntu/rootfs/root/homeassistant/start.sh` |
| HA duomenų katalogas Ubuntu | `/root/homeassistant/` |
| Atskiras Python / venv Ubuntu | `/root/homeassistant/python/`, `/root/homeassistant/venv/` |
| HA konfigūracija Ubuntu | `/root/homeassistant/config/configuration.yaml` |
| HA log Ubuntu | `/root/homeassistant/home-assistant.log` |
| HA log iš Termux | `$PREFIX/var/lib/proot-distro/containers/ubuntu/rootfs/root/homeassistant/home-assistant.log` |
| HA PID Ubuntu | `/root/homeassistant/hass.pid` (PID prieš signalą būtina sutikrinti su procesu) |

Runit log rotacija sukonfigūruota ~1 MiB failui ir 3 archyvams (`s1048576`, `n3`). HA savas logas yra atskiras. Seni rankinio paleidimo `~/ha-proot.pid` ir `~/ha-proot.log` nėra dabartinės runit būsenos šaltinis.

Kotlin privatūs `files/embeddings.json`, `files/audio/`, `files/photo_latest.jpg`, `files/gallery/` lieka programos Android duomenų kataloge. Jie nėra Git turinys.

## Patvirtintų testų registras

### Termux / HA

| Testas | Rezultatas |
|---|---|
| HA full initialization, P10 ir laptopo HTTP | PASS |
| HA restart exit code 100 → automatic restart | PASS |
| HA SIGKILL → runit automatic recovery | PASS |
| Tikras Android reboot → Termux:Boot → wake lock → runit → SSH → HA | PASS; Termux rankiniu būdu neatidarytas |
| Wake lock po ~9 h 42 min uptime | PASS, `termux:service-wakelock` laikomas ~9 h 41 min |
| SSH ED25519 prisijungimas | PASS; prisijungimas be rakto ankstesniame teste atmestas |

Po pirmo reboot runit HA paleidimą užfiksavo 2026-09-19 08:32:50 EEST; pilna inicializacija baigta 08:33:09 EEST. HA žurnalas naudojo UTC (05:33:09), todėl lyginant laikus reikia atsižvelgti į laiko zoną. Stebėti PID: runsvdir 7729, sshd 7737, proot 7738, HA 7765. PID nėra pastovūs identifikatoriai.

### Kotlin atnaujinimas prieš antrą reboot

| Testas | Rezultatas |
|---|---|
| APK `assembleDebug` (Gradle 8.4 / JDK 17) | PASS |
| `adb install -r`, tas pats pasirašymo sertifikatas | PASS; uninstall nenaudotas |
| Duomenys prieš / iškart po atnaujinimo | PASS; visų 13 failų SHA256 sutapo |
| Modeliai ir native bibliotekos APK | PASS; 23 assets/lib įrašai baitais nepakitę |
| Foreground service | PASS, `isForeground=true` |
| `GET /health` | PASS, HTTP 200, `status=ok` |
| Trys nuoseklūs `POST /recognize` | PASS: 0.854 / 1.003 / 1.085 s; visi „Seimininkas“, distance 1.2753057479858398 |
| HA HTTP / SSH po Kotlin atnaujinimo | PASS; esami procesai liko veikti |
| Kotlin autostart po tikro reboot | **PASS**, patvirtinta vėlesniu antru reboot; žr. žemiau |
| Papildoma lint patikra | NOT COMPLETED; nutrauktas >10 min užtrukęs įrankio priklausomybių atsisiuntimas. APK build ir funkciniai testai PASS. |

Originalus 10 recognition užklausų baseline prieš Linux/HA: min 1.159 s, median 1.378 s, average 1.430 s, max 1.920 s, HTTP klaidų 0/10. Atskirame palyginime su HA 2.110 s, be HA 2.092 s. Tai atskirų bandymų matavimai skirtingomis sąlygomis, ne formalus našumo pagerėjimo įrodymas.

### Antras kontroliuojamas Android reboot — PASS

Atliktas vienas šio bandymo reboot. Prieš jį `boot_id` buvo `48937aa8-f4ff-459f-a5b0-ee6ad3aebdbd`, uptime ~10 h 32 min. Po jo naujas `boot_id` — `75e83df4-a615-4d6d-995f-5e9dd60a8a2a`; jis nepasikeitė ir vėliau atkūrus ADB. Programos ir servisai po reboot rankiniu būdu nepaleisti.

| Patikra | Galutinis rezultatas |
|---|---|
| Kotlin foreground service automatinis startas | PASS, PID 7602, `isForeground=true`; sukurtas boot pradžioje |
| Kotlin `/health` :5000 | PASS, HTTP 200 |
| Trys recognition testai po reboot | PASS: 2.770 / 1.971 / 2.060 s; visi „Seimininkas“, distance 1.2753057479858398 |
| Termux:Boot → runit | PASS |
| Wake lock | PASS, `termux:service-wakelock` aktyvus ~4 h 21 min galutinės patikros metu |
| SSH :8022 su ED25519 raktu | PASS, PID 7577 |
| HA :8123 ir pilna inicializacija | PASS, HTTP 200; runit proot PID 7578 |
| ESP32 `/admin` iš P10 | PASS, HTTP 200 |
| ESP32 `/admin` iš laptopo per Wi-Fi | PASS, HTTP 200 |
| Android available RAM | 1,866,140 KiB (~1.78 GiB), galutinės patikros metu |

HA paleidimas užfiksuotas 2026-09-19 19:06:07 EEST, pilna inicializacija 19:06:27 EEST (log'e 16:06:27 UTC, bootstrap 6.40 s). Galutinėje ADB patikroje uptime ~4 h 22 min, Kotlin paslauga ir wake lock veikė ~4 h 21 min. **Visų trijų serverių automatinė boot grandinė VERIFIED.** Tai konkretaus reboot ir stebėjimo laikotarpio patvirtinimas, ne neriboto nepertraukiamo veikimo garantija.

### ESP32

`192.168.43.250/admin`: PASS iš P10 ir PASS iš laptopo priverstinai per Wi-Fi (HTTP 200, 0.461 s). P10 ping 2/2; MAC `28:84:85:49:ff:b8`.

Ankstesnio reboot testo ESP32 FAIL priežastis, **vartotojo patvirtinimu**, buvo fiziškai neprijungtas ESP32, ne firmware ar įrenginio gedimas. Vien timeout to įrodyti negalėjo; po prijungimo HTTP testai PASS. Firmware nekeistas ir neflashintas. `/admin` testas nėra naujas visos kamera → recognition → LCD grandinės testas.

### Resursų matavimai

| Momentas | Android available RAM | Laisva saugykla |
|---|---|---|
| Prieš Linux/HA | ~1.64 GiB | ~41 GB |
| Po Ubuntu, prieš HA | ~1.45 GiB | 39.87 GiB |
| Ankstesnis minimalus HA bandymas | ~1.24 GiB; HA RSS ~190 MiB | vėlesniame HA etape ~37.35 GiB |
| Po Kotlin boot APK atnaujinimo ir regresijos testų | 1,656,712 KiB (~1.58 GiB) | šiame etape iš naujo nematuota |
| Po antro reboot, uptime ~4 h 22 min | 1,866,140 KiB (~1.78 GiB) | šiame etape iš naujo nematuota |

## Tinklas ir Windows maršrutas

### MQTT etapo matavimai (2026-09-19)

Brokeris :1883 pasiekiamas LAN, anonymous ir blogas slaptažodis atmesti. Autentifikuotas publish/subscribe prieš ir po SIGKILL PASS; runit atkūrė procesą. Kotlin `/health`, HA HTTP ir SSH PASS, esami SSH/HA PID nepakeisti. Brokerio RSS 5,568 KiB (~5.4 MiB). Android available RAM prieš/po: 1,958,996 / 1,948,776 KiB (~1.868 / 1.858 GiB); laisva saugykla: 38,889,648 / 38,879,296 KiB (~37.088 / 37.078 GiB). Momentinis skirtumas ~10 MiB RAM ir ~10.1 MiB disko nėra izoliuotas ilgalaikis brokerio sunaudojimo matavimas. Išsamūs testai, keliai ir rollback — [MQTT.md](MQTT.md).

P10 ir ESP32 yra P20 Pro hotspot `192.168.43.0/24` tinkle. Laptopo patikrintas Wi-Fi IP `192.168.43.162`, gateway `192.168.43.1`.

Tailscale laptopui pateiktas to paties `192.168.43.0/24` tinklo maršrutas konfliktavo su vietiniu Wi-Fi: srautas į P10 buvo nukreipiamas per Tailscale, o Chrome gaudavo timeout, nors telefone `127.0.0.1:8123` veikė.

Laikinas pataisymas patikrintas PASS ir vėl perskaitytas iš ActiveStore:

```text
DestinationPrefix: 192.168.43.51/32
InterfaceAlias: Wi-Fi
InterfaceIndex: 3
NextHop: 0.0.0.0
RouteMetric: 1
PolicyStore: ActiveStore
```

Šis konkretus /32 maršrutas dingsta po Windows reboot. Jis netaiso viso /24, todėl ESP32 testams priverstinai naudojama Wi-Fi sąsaja. Nuolatinis sprendimas **TODO**; šiame etape maršrutai nekeičiami. Kitoje tinklo aplinkoje IP ir sąsajos indeksą reikia patikrinti iš naujo.

## Diagnostika (be reboot ir be konfigūracijos keitimo)

Laptopo PowerShell, naudojant esamą vietinę SSH konfigūraciją (privatus raktas ir konfigūracijos turinys į Git nekopijuojami):

```powershell
ssh -F "$env:USERPROFILE\.ssh\p10_termux.conf" -o BatchMode=yes p10-termux
Get-NetRoute -DestinationPrefix '192.168.43.51/32' -PolicyStore ActiveStore
Test-NetConnection 192.168.43.51 -Port 8123
curl.exe --noproxy '*' -L --max-time 15 -o NUL -w 'HTTP=%{http_code}\n' http://192.168.43.51:8123/
curl.exe --noproxy '*' --max-time 10 http://192.168.43.51:5000/health
curl.exe --interface 192.168.43.162 --noproxy '*' --max-time 10 -o NUL -w 'HTTP=%{http_code}\n' http://192.168.43.250/admin
```

Termux per SSH:

```sh
export SVDIR="$PREFIX/var/service"
sv status sshd homeassistant
pgrep -af 'runsv|sshd|homeassistant'
tail -n 40 "$PREFIX/var/log/sv/homeassistant/current"
tail -n 40 "$PREFIX/var/lib/proot-distro/containers/ubuntu/rootfs/root/homeassistant/home-assistant.log"
cat /proc/sys/kernel/random/boot_id
uptime
grep -E 'MemTotal|MemAvailable' /proc/meminfo
df -h "$HOME"
curl --max-time 10 http://127.0.0.1:5000/health
curl -L --max-time 15 -o /dev/null -w 'HTTP=%{http_code}\n' http://127.0.0.1:8123/
```

ADB (vietinis vykdomasis failas: `C:\Users\OldBoy\Android\Sdk\platform-tools\adb.exe`; pavyzdžiuose jis turi būti PATH):

```powershell
adb devices -l
adb shell cat /proc/sys/kernel/random/boot_id
adb shell uptime
adb shell dumpsys power | Select-String 'Wake Locks:|termux:service-wakelock'
adb shell dumpsys activity services lt.saldytuvas.recognizer
adb shell dumpsys package lt.saldytuvas.recognizer | Select-String 'BOOT_COMPLETED|BootCompletedReceiver'
adb shell logcat -d -t 1500 -s AndroidRuntime:E
```

Termux UID neturi `android.permission.DUMP`, todėl wake lock tikrinimui reikia ADB arba telefono UI; SSH `dumpsys power` permission denial nėra wake lock nebuvimo įrodymas.

Recognition testui naudoti lokaliai turimą leidžiamą JPEG, `POST /recognize`, `Content-Type: image/jpeg`, raw body. Nekviesti `/enroll` diagnostikos vietoje. Nuotraukų ir embeddings į Git nedėti.

## Known issues / TODO

- Nuolatinis Windows/Tailscale persidengiančių maršrutų sprendimas.
- Tailscale realaus nuotolinio pasiekiamumo ir paleidimo po reboot patikra atskirai nuo LAN.
- MQTT brokeris paruoštas; užbaigti HA onboarding ir per standartinę MQTT integraciją prijungti brokerį, tada patikrinti MQTT → HA. MQTT paleidimo po reboot testas atskirai. HACS vėliau, dabar neįdiegtas.
- `ffmpeg` ir `libturbojpeg` tik jei prireiks atitinkamų funkcijų. Jų nebuvimo pranešimai HA loguose žinomi; pilnos minimalios HA inicializacijos neblokuoja. Nediegti vien dėl logų išvalymo.
- Neatnaujinti HA/Python ir nekeisti sisteminio Python vien dėl būsimos funkcijos.

## Git ir duomenų saugojimas

Tikras šio laptopo projektas: `D:\_OldBoy_D\esp-32\saldytuvas` (vartotojo ankstesniuose pranešimuose kelias kartais užrašytas kitaip).

Boot kodo checkpoint `8c9c4c080a52b7e31b0f9a3999ba42a9ee2f108f` apėmė README, šį dokumentą, Android manifestą ir boot receiverį. Galutinio reboot rezultato atnaujinimas keičia tik dokumentaciją. Privatūs SSH raktai, slaptažodžiai, API/Tailscale tokenai, HA `.storage`, APK, programos duomenys ir testinės šeimos nuotraukos neįtraukiami. Senas APK paliktas tik laptopo `%TEMP%\p10-kotlin-boot-update\before.apk`; pilnas privačių duomenų eksportas nebuvo atliktas. Atnaujinimo metu išlikimas tikrintas kontrolinėmis sumomis, ne uninstall/reinstall.
