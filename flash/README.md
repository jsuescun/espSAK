# Flash espSAK (no IDE needed)

Install espSAK on your **TTGO T-Display** directly from your browser — no Arduino IDE, no compiling.

## What you need

* A **TTGO T-Display** board.
* A **USB-C data cable** (not a charge-only one).
* Desktop **Google Chrome** or **Microsoft Edge** (Web Serial is required; Firefox/Safari and phones won't work).

## Steps

1. Open the installer page: **https://jsuescun.github.io/espSAK/flash/**
2. Plug the TTGO into your computer with the USB-C cable.
3. Click **Install espSAK**.
4. In the pop-up, pick the serial port of your board and confirm.
5. Wait for it to finish — the device reboots into espSAK automatically.

That's it. The firmware is installed.

## Troubleshooting

* **No port appears when you click Install** -> the cable is charge-only, or you need the USB-serial driver. Install **CH9102** or **CP2102** (the TTGO uses one of these) for your operating system and try again.
* **"Web Serial not supported"** -> you're not using desktop Chrome/Edge. Switch browser.
* **Flashing doesn't start / fails immediately** -> put the board in download mode by hand: hold the **BOOT** button (next to USB, GPIO0), briefly press **RESET**, release **BOOT**, then click Install again.
* Flashing does **not** erase your microSD card; your `/config` settings and logs stay intact.

\---

## ⚠️ Disclaimer — read before flashing

**Use this firmware and installer entirely at your own risk.**

* This software is provided **"AS IS", without warranty of any kind**, express or implied, including but not limited to merchantability, fitness for a particular purpose and non-infringement.
* Flashing firmware can **brick, damage or render your device unusable**. You are solely responsible for your hardware, your data and anything connected to it.
* The author(s) and contributors accept **no liability** for any damage, data loss, device failure, malfunction, or any direct, indirect, incidental or consequential harm arising from the use, misuse or inability to use this firmware, the installer, or the information provided here.
* espSAK is intended **only** for education, experimentation and **authorized** wireless testing on equipment and networks **you own or have explicit permission to test**. Using it against other people's devices, networks or communications may be **illegal** in your jurisdiction. **You alone are responsible for complying with all applicable laws.** The author(s) assume no responsibility for any misuse.

By flashing this firmware you acknowledge and accept the above.

\---

## Third-party components \& licenses

The combined firmware image flashed by this installer embeds components that are **not** part of
the espSAK source code:

* **ESP-IDF / Arduino-ESP32 bootloader, partition table and `boot\_app0` image** — © Espressif
Systems, distributed under the **Apache License 2.0**. Redistribution (including in binary form)
is permitted under that license. See [https://www.apache.org/licenses/LICENSE-2.0](https://www.apache.org/licenses/LICENSE-2.0).
* The browser installer uses **ESP Web Tools** (© Nabu Casa / ESPHome), served from a CDN and not
redistributed here. See [https://esphome.github.io/esp-web-tools/](https://esphome.github.io/esp-web-tools/).

The espSAK application firmware itself is covered by the license stated in the main repository.
All product names, trademarks and registered trademarks are property of their respective owners.

