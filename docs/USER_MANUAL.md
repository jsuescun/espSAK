# espSAK — User Manual

A pocket Wi-Fi / Bluetooth analysis tool. This manual is for **using** the device — no building or coding required. Just power it on, navigate with the two buttons, and read the screen.

> All screen drawings below are simplified text mock-ups of the 240×135 colour display.

---

## 1. At a glance

The top of almost every screen shows a small **status bar**:

```
┌───────────────────────────────────┐
│ [██▒]  battery        BT    Wi-Fi  │   <- battery % + radio icons
│                                   │
│           (screen content)        │
│                                   │
└───────────────────────────────────┘
```

- **Battery chip** (top-left): green when full, yellow mid, red when low. Turns **blue with a ⚡ bolt** while charging over USB-C.
- **BT icon** (top-right area): grey when idle, **blue** when a Bluetooth function is active.
- **Wi-Fi icon** (top-right corner): grey when idle, **green** when Wi-Fi is active, **red** when the captive portal has captured something.

---

## 2. The two buttons

The device is driven entirely by two buttons.

| What you want | Press |
|---|---|
| Move to the next item / next page | **Button 2** — short press |
| Select / enter / confirm | **Button 1** — short press |
| Go back / go up a level | **Button 1** — long press |
| Change screen brightness | **Button 2** while in *System info* |
| Wake the device from standby | **Button 1** |

Most list and menu screens show a short hint at the bottom, e.g. `B2:next  B1:ok  L:back` (where **L** means a long press of Button 1).

---

## 3. Main menu

Short-press **Button 2** to cycle through the menu; **Button 1** to open an item.

```
┌───────────────────────────────────┐
│ [██▒]                   BT   Wi-Fi │
│                                   │
│            3-Wifi tools           │
│                                   │
└───────────────────────────────────┘
```

Menu items: **1-System info · 2-Captive AP · 3-Wifi tools · 4-BT tools · 5-SD Card · 6-Standby mode**.

---

## 4. System info

Live device status. Press **Button 2** here to cycle the **screen brightness** (25 / 50 / 75 / 100 %).

```
┌───────────────────────────────────┐
│ espSAK V0.27             BT  Wi-Fi │
│ Volts.:3.90V (72%)                │
│ Uptime:0h12m34s                   │
│ Mode:Waiting  Heap:145K           │
│ Bright:75% (B2)                   │
│ sdcard OK                         │
└───────────────────────────────────┘
```

If the battery is very low you'll also see **LOW BATTERY** in red.

---

## 5. Captive AP

A Wi-Fi access point with a login page. Any device that connects and submits the page has its request logged to the SD card. Opening *2-Captive AP* shows a sub-menu: **Start / Stop / Status / Portal**.

**Start** / **Stop** show a brief confirmation and return to the main menu. The portal keeps running in the background after Start.

```
┌───────────────────────────────────┐
│          Captive started          │
│            192.168.1.1            │
└───────────────────────────────────┘
```

**Status** shows whether it's running, the address, how many clients connected, and the latest requests captured:

```
┌───────────────────────────────────┐
│ Captive                  BT  Wi-Fi │
│ ON  192.168.1.1                   │
│ clients: 3                        │
│ GET /1?u=john&p=hunter2 HTTP/1.1  │
│ GET /generate_204 HTTP/1.1        │
│ B1-long: back                     │
└───────────────────────────────────┘
```

> When a client is captured, the Wi-Fi icon turns **red** no matter which screen you are on.

**Portal** lets you choose which login page is served, from a list of *Default (internal)* plus any `.html` files on the SD card. The `*` marks the active one.

```
┌───────────────────────────────────┐
│ Portal                            │
│ > Default (internal) *            │
│   free_wifi.html                  │
│   cafe_wifi.html                  │
│ 1/3  B2:mv  B1:use  L:back        │
└───────────────────────────────────┘
```

---

## 6. Wi-Fi tools

Open *3-Wifi tools* for a sub-menu: **Wifi scan · Probe scan · Deauth dt · Heatmap · Clients**. (The last four passively monitor the air and hop across channels.)

**Wifi scan** — nearby networks with signal strength:

```
┌───────────────────────────────────┐
│ Wifi scan                   Wi-Fi │
│ [1]MyHomeNet(-42)                 │
│ [2]Neighbour_5G(-71)              │
│ [3]CoffeeShop(-80)                │
│ pg 1/2  B2:next  B1-long:back     │
└───────────────────────────────────┘
```

**Probe scan** — the network names nearby phones are searching for.

**Deauth dt** — counts disconnect-attack frames; shows a red **ATTACK NEARBY?** if the rate is high:

```
┌───────────────────────────────────┐
│ Deauth detect               Wi-Fi │
│ Total frames: 0                   │
│ Rate: 0 /s                        │
│ ch7   B1-long: back               │
└───────────────────────────────────┘
```

**Heatmap** — a live bar chart of how busy each Wi-Fi channel is (the current channel is highlighted). Handy to pick a quiet channel for your own router.

**Clients** — which devices are talking to which access point (shown as `client→AP`).

All Wi-Fi tool results are logged to the SD card.

---

## 7. Bluetooth tools

Open *4-BT tools* for a sub-menu: **Serial console · BT discovery · BLE scan · Trackers · Advertise**.

**Serial console** — turns the device into a wireless serial terminal. Pair with it from a phone/PC (no PIN) and you can list and read SD files remotely.

```
┌───────────────────────────────────┐
│ BT Console               BT       │
│ Name: espSAK                      │
│ Pair & accept on PC               │
│ (no PIN needed)                   │
│ Waiting for client                │
│ B1-long: exit                     │
└───────────────────────────────────┘
```

**BT discovery** — nearby classic Bluetooth devices and their type (Phone, Audio, Computer…).

**BLE scan** — Bluetooth Low Energy devices with their manufacturer:

```
┌───────────────────────────────────┐
│ BLE scan                 BT       │
│ MyBand -58 Xiaomi                 │
│ <n/a> -74 Apple                   │
│ Sensor3 -80 0x0157                │
│ 7 dev pg1/2 B2:next L:back        │
└───────────────────────────────────┘
```

**Trackers** — finds item trackers nearby (AirTag/Find My, Tile, SmartTag). Pick one and **Watch** it: a hot/cold bar follows its signal so you can tell if it's getting closer — useful to check whether an unknown tracker is following you.

```
┌───────────────────────────────────┐
│ Watch                    BT       │
│ FindMy A1:B2:C3:D4:E5:F6          │
│ [██████████▒▒▒▒▒]  CLOSER         │
│ -47 dBm                           │
│ B1-long: back                     │
└───────────────────────────────────┘
```

**Advertise** — makes the device broadcast as a BLE peripheral you define, for testing your own apps/scanners. Choose an entry from the list; it then advertises until you stop it.

```
┌───────────────────────────────────┐
│ Advertise                BT       │
│ > Test Device (named)             │
│   Lab Beacon 1 (ibeacon)          │
│   My Wearable (named)             │
│ 1/3  B2:mv  B1:adv  L:back        │
└───────────────────────────────────┘
```

The advertised devices are read from `/config/ble_devices.cfg` on the SD card. See §10.

---

## 8. SD Card

Open *5-SD Card* for a sub-menu: **Explore · Wipe all · Web srv**.

**Explore** — a file browser. Move the cursor with Button 2, open with Button 1, go up a folder with a long press.

```
┌───────────────────────────────────┐
│ /wifi                             │
│ >../ <UP>                         │
│  scan_000.txt  2KB                │
│  scan_001.txt  5KB                │
│ 2/3 pg1/1 B2:v B1:ok L:up         │
└───────────────────────────────────┘
```

Selecting a file offers **View / Delete / Cancel**. Text files are shown page by page; other files show a short info/hex preview.

**Wipe all** — deletes every log with a double confirmation (your `/config` settings are kept).

```
┌───────────────────────────────────┐
│          WIPE ALL FILES?          │
│           deletes all logs        │
│          (/config is kept)        │
│        > NO            YES         │
│ B2:toggle  B1:ok  L:cancel        │
└───────────────────────────────────┘
```

**Web srv** — starts a password-protected Wi-Fi access point so you can download your logs from a phone/laptop browser, no cable needed:

```
┌───────────────────────────────────┐
│ Web server                  Wi-Fi │
│ SSID: espSAK-files                │
│ Pass: 48217390                    │
│ http://192.168.4.1                │
│ Open the IP in browser            │
│ B1-long: stop                     │
└───────────────────────────────────┘
```

Connect to that Wi-Fi with the password shown, open the address in a browser, and you can **browse folders, download and delete** files.

---

## 9. Standby

*6-Standby mode* puts the device into a deep low-power sleep to save battery. Press **Button 1** to wake it. The device also goes to standby on its own after a few minutes of inactivity (only on idle screens — never during a scan or while the captive/web server is running).

---

## 10. The SD card: logs and settings

Insert a **FAT-formatted microSD card**. The device creates its folders automatically. You never have to format or prepare anything by hand.

### Where things are saved

```
/captive   captured login requests
/wifi      network scans and client maps
/probe     probe-request captures
/bt        Bluetooth discovery and BLE scans
/track     detected trackers
/config    your settings and portal pages
```

Each log is a plain `.txt` file with a timestamped header; new sessions never overwrite old ones.

### Settings — `/config/config.txt`

A text file you can edit on a computer (or leave alone — it's created with sensible defaults). Keys:

| Setting | What it does |
|---|---|
| `brightness` | Screen brightness level, 0–3 |
| `captive_ssid` | Name of the captive portal Wi-Fi |
| `websrv_ssid` | Name of the SD web-server Wi-Fi |
| `websrv_pass` | Web-server password (empty = a new random one each time) |
| `longpress_ms` | How long a "long press" must be held |
| `idle_min` | Minutes of inactivity before auto-standby |
| `captive_html` | Which login page to serve (empty = built-in) |
| `captive_ip` | Captive portal address |
| `websrv_ip` | Web-server address |

### Custom login pages

Drop any `.html` file into `/config/` and it will appear in **Captive → Portal** to choose from. A ready-to-edit template is created for you at `/config/index.html`.

### BLE advertiser list — `/config/ble_devices.cfg`

One device per line, used by **BT tools → Advertise**:

```
# name | type | params
Test Device  | named   | espSAK-BLE
Lab Beacon 1 | ibeacon | E2C56DB5-DFFB-48D2-B060-D0F5A71096E0,1,1
```

`named` advertises a plain device name; `ibeacon` advertises a beacon with your own UUID / major / minor.

---

## 11. Tips

- **Screen too bright/dim?** System info → Button 2.
- **Can't get your logs off the card?** Use *SD Card → Web srv* and a browser — no need to remove the card.
- **Battery warning / charging?** Watch the battery chip: red = low, blue with a bolt = charging.
- **Bluetooth and Wi-Fi don't run at the same time** — entering a Bluetooth tool turns Wi-Fi off, and vice-versa. This is normal.

---

## ⚠️ Safety & responsible use

espSAK is a **passive analysis** tool for learning and for testing **your own** devices and networks, or ones you have explicit permission to test. Capturing, monitoring or interacting with other people's devices, networks or communications may be **illegal** where you live. You are solely responsible for complying with the law. The authors accept no liability for misuse or for any damage.
