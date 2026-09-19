# P10 MQTT brokeris

2026-09-19. Brokerio diegimas ir proceso recovery PASS. HA MQTT integracija **PENDING**: HA pradinis onboarding dar neužbaigtas. Telefono reboot šiame etape neatliktas.

## Architektūra ir auditas

```text
Android
├── Kotlin AI :5000 (nepakeistas)
└── Termux + Termux:Boot + wake lock
    └── esamas runit
        ├── SSH :8022
        ├── Mosquitto :1883 (native ARM64)
        └── Ubuntu/proot → Home Assistant :8123
```

Prieš diegimą Termux ir Ubuntu Mosquitto nebuvo, /proc/net/tcp ir tcp6 nebuvo :1883 listenerio. HA config entries turėjo tik analytics ir backup, MQTT nebuvo. `/api/onboarding` visi keturi žingsniai (user, core_config, analytics, integration) buvo `done=false`.

Pasirinktas oficialios Termux saugyklos `mosquitto` **2.1.2-2** (brokeris 2.1.2, aarch64). Native procesui nereikia papildomo proot sluoksnio; jis naudoja jau veikiančią runit infrastruktūrą. Faktiniai autentifikacijos ir recovery testai šiame Android 9/Termux 0.118.3 telefone praėjo.

Įdiegti tik 8 nauji paketai: mosquitto, libmosquitto, libwebsockets, libuv, libcap, attr, c-ares, cjson. Atsisiųsta 1,373 kB, deklaruotas papildomas paketų dydis 6,832 kB. 0 esamų paketų atnaujinta. Kotlin, ESP32 firmware, HA versija, Python, boot skriptas, SSH/HA runit servisai nepakeisti. HACS ir Node-RED nediegti.

## Konfigūracija ir paslaptys

Termux `$HOME=/data/data/com.termux/files/home`, `$PREFIX=/data/data/com.termux/files/usr`.

| Paskirtis | Faktinis kelias |
|---|---|
| Brokerio konfigūracija | `$PREFIX/etc/mosquitto/p10.conf` |
| Runit run / finish | `$PREFIX/var/service/mosquitto/run`, `finish` |
| Runit log paleidimas | `$PREFIX/var/service/mosquitto/log/run` (paketo skriptas, kviečia svlogger) |
| Logas / rotacija | `$PREFIX/var/log/sv/mosquitto/current`, `config` |
| Persistence | `$PREFIX/var/lib/mosquitto/` |
| Privatus katalogas | `$HOME/.config/p10-mqtt/` (0700) |
| Slaptažodžių hash failas | privačiame kataloge `passwd` (0600), sukurtas mosquitto_passwd |
| Prisijungimo duomenys | privačiame kataloge `credentials.tsv` (0600), tik telefone |
| ACL | privačiame kataloge `acl` |
| Testinių klientų konfigūracijos | privačiame kataloge `p10_test/mosquitto_pub`, `p10_test/mosquitto_sub` |
| HA paskyros klientų konfigūracijos | privačiame kataloge `homeassistant/mosquitto_pub`, `homeassistant/mosquitto_sub` |
| Originalus paketo run | privačiame kataloge `package-run.original` |

Sukurtos dvi atskiros paskyros su 32 atsitiktinių baitų slaptažodžiais iš `/dev/urandom`. Slaptažodžiai neišvesti ir į Git nekopijuoti. `homeassistant` turi read/write `#`; diagnostinis `p10_test` tik `p10/test/#`. Būsimiems ESP32 reikės atskirų vartotojų ir temų teisių; HA paskyros jiems nenaudoti.

Listener: `1883 0.0.0.0`, `allow_anonymous false`, privalomi password_file ir acl_file. Visos IPv4 sąsajos, įskaitant LAN, reikalauja autentifikacijos. Portas 1883 **be TLS**: slaptažodis saugomas hash forma brokeryje, bet MQTT srautas nešifruotas. Naudoti patikimame vietiniame tinkle, nepublikuoti internete; platesnei prieigai reikėtų atskiro TLS/VPN sprendimo.

Įjungta persistence ir 300 s autosave. Tai negarantuoja paskutinių 300 s retained/session duomenų išlikimo po SIGKILL. Logai eina į stdout → svlogd; rotacija `s1048576`, `n3` (~1 MiB + 3 archyvai).

Runit `run` vykdo `exec mosquitto -c /data/data/com.termux/files/usr/etc/mosquitto/p10.conf 2>&1`. `finish` laukia 3 s prieš pakartotinį startą. `sv-enable mosquitto` atliktas, `down` failo nėra: servisas įtrauktas į esamą runsvdir ir turėtų startuoti per esamą boot grandinę. **MQTT tikras reboot dar netestuotas.**

## Testai

| Testas | Rezultatas |
|---|---|
| Laptopas → P10 :1883 TCP | PASS |
| Anonymous MQTT prisijungimas į LAN IP | Atmestas kaip reikalauta: `not authorised`, exit 5 — apsaugos PASS |
| Blogas slaptažodis | Atmestas — PASS |
| `p10_test` publisher → broker → subscriber per P10 LAN IP | PASS |
| ACL publish į neleistiną temą | PASS, MQTT5 PUBACK `Not authorized` (kliento exit gali būti 0, todėl vertintas atsakymas) |
| Brokerio SIGKILL → runit recovery | PASS, PID 20664 → 20703; patikrinta po 6 s |
| Publish/subscribe po recovery | PASS |
| HA skirtos MQTT paskyros publish/subscribe | PASS; tai nėra HA integracijos testas |
| Kotlin `/health` | PASS, HTTP 200 |
| HA :8123 | PASS, HTTP 200 |
| SSH su raktu | PASS; SSH PID 7577 ir HA proot PID 7578 nepasikeitė |
| MQTT test message → Home Assistant | NOT TESTED; integracija dar neprijungta |
| Telefono reboot su MQTT | NOT TESTED; neatliktas pagal etapo ribas |

## Resursai

| Rodiklis | Prieš | Po |
|---|---|---|
| Android MemAvailable | 1,958,996 KiB (~1.868 GiB) | 1,948,776 KiB (~1.858 GiB) |
| Laisva /data saugykla | 38,889,648 KiB (~37.088 GiB) | 38,879,296 KiB (~37.078 GiB) |
| Mosquitto RSS | nėra proceso | 5,568 KiB (~5.4 MiB) |

Momentinis sistemos RAM pokytis −9.98 MiB, disko −10.11 MiB (paketai, cache, logai ir įprasta sistemos veikla). Tai nėra izoliuotas ilgalaikis brokerio atminties matavimas.

## HA prijungimas — kitas žingsnis

1. Savininkas užbaigia esamą HA onboarding; jo paskyros slaptažodžio nelaikyti dokumentacijoje.
2. Standartiniame HA UI: Settings → Devices & services → Add integration → MQTT. Brokeris **127.0.0.1**, portas **1883**, vartotojas **homeassistant**, slaptažodis iš telefono privataus credentials failo. Ubuntu/proot dalijasi Android tinklu, todėl localhost pasiekia native brokerį.
3. MQTT integracijos listen funkcija užsiprenumeruoti `p10/test/ha`, iš autentifikuoto kliento paskelbti neretained testinę žinutę ir patvirtinti gavimą HA.

HA `.storage` rankiniu būdu neredaguota, onboarding neapeitas, nauja savininko paskyra automatiškai nekurta. MQTT paskyra nėra HA prisijungimo paskyra.

## Diagnostika ir rollback

Per SSH Termux aplinkoje:

```sh
export SVDIR="$PREFIX/var/service"
sv status mosquitto sshd homeassistant
tail -n 30 "$PREFIX/var/log/sv/mosquitto/current"
# Pirmame SSH terminale:
XDG_CONFIG_HOME="$HOME/.config/p10-mqtt/p10_test" mosquitto_sub -t p10/test/check -C 1 -W 30
# Kitame SSH terminale:
XDG_CONFIG_HOME="$HOME/.config/p10-mqtt/p10_test" mosquitto_pub -t p10/test/check -m check -q 1
```

Jeigu reikia atšaukti brokerio veikimą: `sv-disable mosquitto` su aukščiau nustatytu SVDIR sustabdo brokerį ir palieka `down` failą kitam boot. Konfigūracija ir kredencialai lieka telefone; Kotlin/SSH/HA neturi būti stabdomi. Nenaudoti bendro `service-daemon stop`. Paketų ar privačių duomenų trynimas rollback testui nereikalingas. Šis rollback šiame etape nevykdytas.

Oficialūs šaltiniai: [Termux paketas ir runit failai](https://github.com/termux/termux-packages/blob/master/packages/libmosquitto/mosquitto.subpackage.sh), [Mosquitto konfigūracija](https://mosquitto.org/man/mosquitto-conf-5.html), [HA MQTT integracija](https://www.home-assistant.io/integrations/mqtt/).
