//espSAK, multi tool over ttgo-tdisplay esp32 device 
//(C) 2023-2026 -jsuescun 
//
//
// v0.15: BT tools uses the big carousel style; new Web server item to download
//        SD files over a password-protected (WPA2) AP. Main menu now has 7 items.
// v0.16: item 3 is now "Wifi tools" (carousel sub-menu): Wifi scan, Probe scan,
//        Deauth detector and Channel heatmap (the last three use 802.11
//        promiscuous mode with channel hopping). Passive monitoring only.
// v0.17: BT tools -> "4-Trackers": detects AirTag/Find My, Tile and SmartTag
//        BLE advertisers, and a "Watch" mode that follows one tracker's RSSI
//        (anti-stalking hot/cold meter).
// v0.18: charging bolt on the battery chip (USB detected by voltage); battery
//        shown on all carousel sub-menus; logs go to /captive /wifi /probe /bt
//        /track folders as .txt; "5-SD Card" is now a sub-menu (Explore / Wipe
//        all / Web srv). (Long-press back kept on release for Button2 compat.)
// v0.20: charging shown as a blue battery + bolt to its right (menu & sub-menus);
//        web server browses sub-folders and can delete files; wifi status icon
//        restyled and spaced further from the BT icon.
// v0.21: polish pack - timestamped log headers, formatted uptime, screen
//        brightness (B2 cycles it in System info), auto-standby on inactivity,
//        and a new Wifi tool "5-Clients" mapping client MACs to their AP.
// v0.22: captive logging fix - read loop now has a 3s idle timeout so the file
//        always closes & flushes (half-open clients no longer lose the write);
//        folders/log-names are also (re)created on a late SD remount.
// v0.23: /config/ folder - config.txt (brightness, SSIDs, web pass, long-press
//        ms, idle minutes, captive html path) created with defaults if missing;
//        captive page served from /config/index.html (created from the compiled
//        page if absent). NOTE: configurable long-press needs the Button2 library.
// v0.24: fix - at boot sd_init is set before loadConfig(), so /config is actually
//        created/loaded (previously skipped). Clean behaviour when SD is absent.
// v0.25: Wipe all now keeps /config. Captive is a sub-menu (Start/Stop/Status);
//        Status shows IP + last requests. Captive no longer paints the screen
//        red - the wifi icon turns red on a capture. Captive & web-server AP IPs
//        are configurable. index_html served via FPSTR (PROGMEM-safe).
// v0.26: captive "4-Portal" picker - choose the served page from a list of
//        "Default (internal)" + every /config/*.html; selection saved to
//        captive_html (empty/invalid = internal). /config/index.html kept as
//        an editable template.
// v0.27: BT tools -> "5-Advertise": broadcast as a generic BLE peripheral
//        (named / iBeacon) from /config/ble_devices.txt. Passive dev/test use.
// v0.28: each advertiser gets its own static-random MAC (4th cfg field, else
//        derived from the name); iBeacon built as raw bytes and started from
//        the GAP "data ready" event (fixes empty-payload advertising).
//
// IMPORTANT: compile with Partition Scheme = "Huge APP (3MB No OTA/1MB SPIFFS)".
//
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <SPI.h>
#include <TFT_eSPI.h> //modify librery user_setup_select.h - // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
#include <esp_adc_cal.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include "BluetoothSerial.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_gap_ble_api.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
//#include <MFRC522.h>
#include <FS.h>
#include <SD.h>
#include <Button2.h>
#include "espsak.h"
#include "webpage.h"
#include "Free_Fonts.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Enable it in Tools or use an ESP32 with Classic BT.
#endif

//using sdcard reader over spi in ttgo-tdisplay
SPIClass SDSPI(HSPI);
#define MY_CS       33
#define MY_SCLK     25
#define MY_MISO     27
#define MY_MOSI     26

#ifndef TFT_BL
#define TFT_BL 4            // TTGO T-Display backlight
#endif

#define ADC_EN              14
#define ADC_PIN             34
#define BUTTON_1            35
#define BUTTON_2            0

Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

TFT_eSPI tft = TFT_eSPI();
BluetoothSerial SerialBT;

String version = "0.28";

char buff[512];
int vref = 1100;

String working_mode = "Waiting";   // "Waiting" / "Captive"
File myFile;
File dir;
String captive_fname = "/captive/cap_000.txt";
String wifi_fname    = "/wifi/scan_000.txt";
String probe_fname   = "/probe/probe_000.txt";
String bt_fname      = "/bt/bt_000.txt";
String track_fname   = "/track/trk_000.txt";
int sd_init = false;

// ---- configurable settings (persisted in /config/config.txt) ----
String captiveSsid     = "freeWifi";
String websrvSsid      = "espSAK-files";
String websrvPass      = "";                 // empty = random password each session
int    longpressMs     = 400;                // long-press threshold (requires Button2 library)
int    idleMin         = 5;                  // auto-standby minutes
String captiveHtmlPath = "";                 // "" or missing file = internal page
String captiveIp       = "192.168.1.1";      // captive AP gateway/IP
String websrvIp        = "192.168.4.1";      // web server AP gateway/IP

int captive_count = 0;

IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress myIP;
const byte DNS_PORT = 53;
DNSServer dnsServer;

WiFiServer server(80);

// ------------------------------------------------------------------
//  State machine
// ------------------------------------------------------------------
enum AppState { ST_MENU, ST_INFO, ST_WIFI, ST_BT, ST_SDCARD, ST_WEBSRV, ST_CAPTIVE, ST_STANDBY };

enum CaptiveMode { CAPM_MENU, CAPM_STATUS, CAPM_PORTAL };
CaptiveMode capMode = CAPM_MENU;
int capMenuPos = 0;                 // 0=Start 1=Stop 2=Status 3=Portal
const int CAP_MENU_MAX = 3;

// portal HTML picker: slot 0 = "" (internal default), 1.. = /config/*.html
#define MAX_HTML 20
String htmlList[MAX_HTML];
int htmlCount = 0;
int htmlCursor = 0;

// last captive requests (ring buffer) for the status screen
#define CAP_LOG_N 5
String capLog[CAP_LOG_N];
int capLogCount = 0;

enum WifiMode { WFM_MENU, WFM_SCAN, WFM_PROBE, WFM_DEAUTH, WFM_HEATMAP, WFM_CLIENTS };
WifiMode wifiMode = WFM_MENU;
int wifiMenuPos = 0;
const int WIFI_MENU_MAX = 4;   // items 0..4

// screen brightness + inactivity auto-standby
int brightnessLevel = 3;
const uint8_t brightLevels[4] = {64, 128, 192, 255};   // 25/50/75/100 %
unsigned long lastActivity = 0;
unsigned long idleTimeout = 300000UL;                   // set from idleMin
AppState appState = ST_MENU;

enum SdMode { SDM_MENU, SDM_LIST, SDM_ACTIONS, SDM_VIEW, SDM_CONFIRM, SDM_INFO, SDM_WIPE };
int sdMenuPos = 0;
const int SD_MENU_MAX = 2;   // items 0..2 (Explore / Wipe / Web srv)
SdMode sdMode = SDM_LIST;

enum BtMode { BTM_MENU, BTM_DISCOVERY, BTM_SPP, BTM_BLESCAN, BTM_TRACKERS, BTM_WATCH, BTM_ADV_LIST, BTM_ADV_ON };
BtMode btMode = BTM_MENU;

bool intentNext = false, intentSelect = false, intentBack = false;

const int max_menu = 6;
int menu_pos = 0;

static uint8_t advRaw[31];
// ------------------------------------------------------------------
//  Battery (1S LiPo)
// ------------------------------------------------------------------
unsigned long lastBattSample = 0;
float batteryVoltage = 0;
int   batteryPct = 0;

float readBatteryVoltage()
{
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(ADC_PIN);
  float v = (float)sum / 16.0;
  return (v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
}

int voltageToPct(float v)
{
  static const float lut[][2] = {
    {4.20, 100}, {4.10, 90}, {4.00, 80}, {3.90, 70}, {3.85, 60},
    {3.80, 50},  {3.75, 40}, {3.70, 30}, {3.65, 20}, {3.55, 10},
    {3.40, 5},   {3.30, 0}
  };
  const int N = 12;
  if (v >= lut[0][0])     return 100;
  if (v <= lut[N - 1][0]) return 0;
  for (int i = 0; i < N - 1; i++) {
    if (v <= lut[i][0] && v > lut[i + 1][0]) {
      float frac = (v - lut[i + 1][0]) / (lut[i][0] - lut[i + 1][0]);
      return (int)(lut[i + 1][1] + frac * (lut[i][1] - lut[i + 1][1]));
    }
  }
  return 0;
}

void sampleBattery(unsigned long now)
{
  if (lastBattSample != 0 && (now - lastBattSample < 2000)) return;
  lastBattSample = (now == 0) ? 1 : now;
  float v = readBatteryVoltage();
  if (batteryVoltage == 0) batteryVoltage = v;
  else batteryVoltage = batteryVoltage * 0.7 + v * 0.3;
  batteryPct = voltageToPct(batteryVoltage);
}

String fmtUptime()
{
  unsigned long s = millis() / 1000;
  unsigned long h = s / 3600; s %= 3600;
  unsigned long m = s / 60;   s %= 60;
  char b[16];
  sprintf(b, "%luh%02lum%02lus", h, m, s);
  return String(b);
}

void applyBrightness()
{
  uint8_t duty = brightLevels[brightnessLevel];
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, duty);
#else
  ledcWrite(0, duty);
#endif
}

void drawBatteryChip()
{
  bool charging = (batteryVoltage > 4.3);   // USB present (battery alone never exceeds ~4.2V)
  int x = 2, y = 2, w = 30, h = 15;
  tft.fillRoundRect(x, y, w + 12, h, 3, TFT_BLACK);   // extra width for the bolt
  int bx = x + 3, by = y + 3, bw = 20, bh = 9;

  uint16_t col;
  if (charging) col = TFT_CYAN;             // charging -> blue/cyan battery
  else col = (batteryPct > 40) ? TFT_GREEN : ((batteryPct > 15) ? TFT_YELLOW : TFT_RED);
  if (!charging && batteryPct < 10 && ((millis() / 500) % 2)) col = TFT_BLACK;

  tft.drawRect(bx, by, bw, bh, TFT_WHITE);
  tft.fillRect(bx + bw, by + 2, 2, bh - 4, TFT_WHITE);
  int fillw = (bw - 2) * batteryPct / 100;
  if (fillw < 0) fillw = 0;
  if (fillw > bw - 2) fillw = bw - 2;
  tft.fillRect(bx + 1, by + 1, fillw, bh - 2, col);

  // lightning bolt to the RIGHT of the battery while charging
  if (charging) {
    int gx = bx + bw + 5, gy = by - 1;
    tft.fillTriangle(gx + 4, gy,     gx,     gy + 6, gx + 4, gy + 6, TFT_YELLOW);
    tft.fillTriangle(gx + 1, gy + 5, gx + 5, gy + 5, gx + 1, gy + 11, TFT_YELLOW);
  }
}

// ---- vector status icons (right side) ----
void drawWifiIcon(int x, int y, uint16_t color)
{
  int cx = x + 9, cy = y + 13;               // base dot (bottom centre)
  for (int k = 0; k < 3; k++) {
    int r = 4 + k * 4;                        // tighter, even arcs (4,8,12)
    for (int a = 225; a <= 315; a += 4) {     // clean 90deg fan
      float rad = a * 0.0174533;
      int px = cx + (int)(r * cos(rad));
      int py = cy + (int)(r * sin(rad));
      tft.fillCircle(px, py, 1, color);       // round dots read smoother than squares
    }
  }
  tft.fillCircle(cx, cy, 2, color);           // base dot
}

void drawBtIcon(int x, int y, uint16_t color)
{
  int cx  = x + 6;
  int top = y + 2,  bot = y + 18;
  int yq1 = y + 6,  yq3 = y + 14;  // upper/lower quarter heights
  int xr  = cx + 5;                // right knees
  int xl  = cx - 3;                // left tips
  for (int o = 0; o <= 1; o++) {   // o-loop thickens the glyph by 1px
    tft.drawLine(cx + o, top, cx + o, bot, color);   // vertical spine
    tft.drawLine(cx + o, top, xr + o, yq1, color);   // top -> upper-right knee
    tft.drawLine(xr + o, yq1, xl + o, yq3, color);   // knee -> lower-left tip
    tft.drawLine(cx + o, bot, xr + o, yq3, color);   // bottom -> lower-right knee
    tft.drawLine(xr + o, yq3, xl + o, yq1, color);   // knee -> upper-left tip
  }
}

// Persistent status icons: grey = idle, BT blue when a BT function is active,
// WiFi green when captive AP or a WiFi scan is running.
void drawRadioIcons()
{
  bool captiveHit = (working_mode == "Captive") && (captive_count > 0);
  bool wifiActive = (working_mode == "Captive") || (appState == ST_WIFI) || (appState == ST_WEBSRV);
  bool btActive   = (appState == ST_BT);
  uint16_t wifiCol = captiveHit ? TFT_RED : (wifiActive ? TFT_GREEN : TFT_DARKGREY);
  drawBtIcon(tft.width() - 46, 2, btActive ? TFT_BLUE : TFT_DARKGREY);
  drawWifiIcon(tft.width() - 22, 2, wifiCol);
}

// ------------------------------------------------------------------
//  SD mount
// ------------------------------------------------------------------
bool initSD()
{
  SDSPI.begin(MY_SCLK, MY_MISO, MY_MOSI, MY_CS);
  for (int a = 0; a < 3; a++) {
    uint32_t freq = (a == 0) ? 4000000 : 1000000;
    if (SD.begin(MY_CS, SDSPI, freq)) return true;
    Serial.printf("SD mount attempt %d failed\n", a + 1);
    SD.end();
    delay(80);
  }
  return false;
}

void ensureFolders()
{
  const char *dirs[] = {"/captive", "/wifi", "/probe", "/bt", "/track"};
  for (int i = 0; i < 5; i++) if (!SD.exists(dirs[i])) SD.mkdir(dirs[i]);
}

// first free "<dir>/<prefix>NNN.txt"
String nextName(const char *dir, const char *prefix)
{
  char path[48];
  for (int c = 0; c < 1000; c++) {
    snprintf(path, sizeof(path), "%s/%s%03d.txt", dir, prefix, c);
    if (!SD.exists(path)) return String(path);
  }
  return String(dir) + "/" + prefix + "999.txt";
}

// create folders and pick fresh log names (used at boot and on a late SD remount)
void prepareSdFiles()
{
  ensureFolders();
  captive_fname = nextName("/captive", "cap_");
  wifi_fname    = nextName("/wifi",    "scan_");
  probe_fname   = nextName("/probe",   "probe_");
  bt_fname      = nextName("/bt",      "bt_");
  track_fname   = nextName("/track",   "trk_");
}

// ==================================================================
//  CONFIG  (/config/config.txt  +  /config/index.html)
// ==================================================================
void ensureConfigFolder() { if (sd_init && !SD.exists("/config")) SD.mkdir("/config"); }

void writeConfig()
{
  if (!sd_init) return;
  ensureConfigFolder();
  File f = SD.open("/config/config.txt", FILE_WRITE);   // "w" = create/truncate
  if (!f) return;
  f.println("# espSAK config (editable; rewritten by the device)");
  f.println("brightness="   + String(brightnessLevel));   // 0..3
  f.println("captive_ssid=" + captiveSsid);
  f.println("websrv_ssid="  + websrvSsid);
  f.println("websrv_pass="  + websrvPass);                 // empty = random
  f.println("longpress_ms=" + String(longpressMs));
  f.println("idle_min="     + String(idleMin));
  f.println("captive_html=" + captiveHtmlPath);
  f.println("captive_ip="   + captiveIp);
  f.println("websrv_ip="    + websrvIp);
  f.close();
}

void loadConfig()
{
  if (!sd_init) return;
  ensureConfigFolder();
  if (!SD.exists("/config/config.txt")) {
    writeConfig();                           // first run: create with defaults
  } else {
    File f = SD.open("/config/config.txt", FILE_READ);
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String k = line.substring(0, eq);  k.trim();
        String v = line.substring(eq + 1); v.trim();
        if      (k == "brightness")   brightnessLevel = v.toInt();
        else if (k == "captive_ssid") captiveSsid = v;
        else if (k == "websrv_ssid")  websrvSsid = v;
        else if (k == "websrv_pass")  websrvPass = v;
        else if (k == "longpress_ms") longpressMs = v.toInt();
        else if (k == "idle_min")     idleMin = v.toInt();
        else if (k == "captive_html") captiveHtmlPath = v;
        else if (k == "captive_ip")   captiveIp = v;
        else if (k == "websrv_ip")    websrvIp = v;
      }
      f.close();
    }
  }
  // keep /config/index.html as an editable template of the compiled page
  if (!SD.exists("/config/index.html")) {
    File h = SD.open("/config/index.html", FILE_WRITE);
    if (h) { h.print(FPSTR(index_html)); h.close(); }
  }
}

void applyConfig()
{
  if (brightnessLevel < 0 || brightnessLevel > 3) brightnessLevel = 3;
  applyBrightness();
  if (idleMin < 1) idleMin = 1;
  idleTimeout = (unsigned long)idleMin * 60000UL;
  btn1.setLongClickTime(longpressMs);        // requires the Button2 library
}

// stream the (configurable) captive page from SD, else fall back to the compiled one
void streamCaptiveHtml(WiFiClient &client)
{
  if (sd_init && captiveHtmlPath.length() > 0) {
    File f = SD.open(captiveHtmlPath, FILE_READ);
    if (f && !f.isDirectory()) {
      uint8_t b[256];
      while (f.available()) { int n = f.read(b, 256); if (n <= 0) break; client.write(b, n); }
      f.close();
      return;
    }
    if (f) f.close();
  }
  client.print(FPSTR(index_html));     // "" or missing file -> internal default
}

void espDelay(int ms)
{
  esp_sleep_enable_timer_wakeup(ms * 1000);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_light_sleep_start();
}

// ------------------------------------------------------------------
//  Menu (kept visually identical)
// ------------------------------------------------------------------
void print_menu()
{
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FM12);
  tft.setTextDatum(MC_DATUM);

  switch (menu_pos) {
    case 1: tft.setTextColor(TFT_WHITE, TFT_BLACK);  tft.drawString("1-System info",  tft.width() / 2, tft.height() / 2); break;
    case 2: tft.setTextColor(TFT_GOLD, TFT_BLACK);   tft.drawString("2-Captive AP",    tft.width() / 2, tft.height() / 2); break;
    case 3: tft.setTextColor(TFT_GREEN, TFT_BLACK);  tft.drawString("3-Wifi tools",    tft.width() / 2, tft.height() / 2); break;
    case 4: tft.setTextColor(TFT_BLUE, TFT_BLACK);   tft.drawString("4-BT tools",      tft.width() / 2, tft.height() / 2); break;
    case 5: tft.setTextColor(TFT_ORANGE, TFT_BLACK); tft.drawString("5-SD Card",       tft.width() / 2, tft.height() / 2); break;
    case 6: tft.setTextColor(TFT_RED, TFT_BLACK);    tft.drawString("6-Standby mode",  tft.width() / 2, tft.height() / 2); break;
    default:
      tft.fillScreen(TFT_BLACK);
      tft.pushImage(0, 0, 240, 135, espsak_logo);
  }
}

void drawMenu()
{
  print_menu();
  drawBatteryChip();
  drawRadioIcons();
}

unsigned long lastChip = 0;
void updateMenuBattery(unsigned long now)
{
  if (now - lastChip > 500) { lastChip = now; drawBatteryChip(); }
}

// ------------------------------------------------------------------
//  System info (live)
// ------------------------------------------------------------------
unsigned long lastInfo = 0;
void enterInfo() { appState = ST_INFO; lastInfo = 0; }

void updateInfo(unsigned long now)
{
  if (lastInfo != 0 && (now - lastInfo < 1000)) return;
  lastInfo = (now == 0) ? 1 : now;

  String voltage   = "Volts.:" + String(batteryVoltage) + "V (" + String(batteryPct) + "%)";
  String seconds   = "Uptime:" + fmtUptime();
  String mode      = "Mode:" + working_mode + "  Heap:" + String(ESP.getFreeHeap() / 1024) + "K";
  String bright    = "Bright:" + String(brightLevels[brightnessLevel] * 100 / 255) + "% (B2)";

  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("espSAK V" + version, 2, 2);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);   tft.drawString(voltage, 4, 28);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);   tft.drawString(seconds, 4, 42);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);  tft.drawString(mode, 4, 56);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);tft.drawString(bright, 4, 70);
  if (!sd_init) { tft.setTextColor(TFT_RED, TFT_BLACK);    tft.drawString("sdcard not found", 4, 84); }
  else          { tft.setTextColor(TFT_ORANGE, TFT_BLACK); tft.drawString("sdcard OK", 4, 84); }
  if (batteryPct < 15) { tft.setTextColor(TFT_RED, TFT_BLACK); tft.drawString("LOW BATTERY", 4, 108); }
  drawRadioIcons();
}

// ------------------------------------------------------------------
//  WiFi scan (async + paged)
// ------------------------------------------------------------------
#define WIFI_PER_PAGE 6
bool wifiScanning = false;
int  wifiCount = 0;
int  wifiPage  = 0;

void writeWifiLog()
{
  if (!sd_init) return;
  myFile = SD.open(wifi_fname, FILE_APPEND);
  if (!myFile) { Serial.println("error opening " + wifi_fname); return; }
  myFile.println("-----espSAK wifiScan V" + version + " @ " + fmtUptime() + " -----");
  for (int i = 0; i < wifiCount; i++) {
    sprintf(buff, "[%d]:%s(%d)", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    myFile.println(buff);
  }
  myFile.close();
}

void renderWifiPage()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Wifi scan", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  if (wifiCount == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.drawString("no networks found", tft.width() / 2, tft.height() / 2);
    return;
  }
  int pages = (wifiCount + WIFI_PER_PAGE - 1) / WIFI_PER_PAGE;
  int start = wifiPage * WIFI_PER_PAGE;
  int line = 24;
  for (int i = start; i < start + WIFI_PER_PAGE && i < wifiCount; i++) {
    sprintf(buff, "[%d]%s(%d)", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    tft.drawString(String(buff), 0, line);
    line += 15;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "pg %d/%d  B2:next  B1-long:back", wifiPage + 1, pages);
  tft.drawString(String(buff), 0, 120);
}

void enterWifiScan()
{
  wifiMode = WFM_SCAN;
  wifiScanning = true; wifiCount = 0; wifiPage = 0;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Wifi scan...", 2, 2);
  drawRadioIcons();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.scanNetworks(true);
}

void updateWifiScan(unsigned long now)
{
  if (!wifiScanning) return;
  int res = WiFi.scanComplete();
  if (res == WIFI_SCAN_RUNNING) return;
  wifiScanning = false;
  wifiCount = (res < 0) ? 0 : res;
  Serial.printf("Found %d net\n", wifiCount);
  writeWifiLog();
  renderWifiPage();
}

// ==================================================================
//  WIFI TOOLS - promiscuous sniffers (probe / deauth / heatmap)
// ==================================================================
#define MAX_PROBE 40
#define PROBE_PER_PAGE 6
struct ProbeEntry { char ssid[33]; char mac[18]; int rssi; };
ProbeEntry probes[MAX_PROBE];
volatile int probeCount = 0;
int probePage = 0;

volatile uint32_t deauthTotal = 0;
volatile uint32_t deauthWindow = 0;
uint32_t deauthRate = 0;
unsigned long lastDeauthSec = 0;

volatile uint32_t chanPkts[14];   // index 1..13

int snifChannel = 1;
unsigned long lastHop = 0;
unsigned long lastWifiToolDraw = 0;

void hopChannel(unsigned long now)
{
  if (now - lastHop > 300) {
    lastHop = now;
    snifChannel++;
    if (snifChannel > 13) snifChannel = 1;
    esp_wifi_set_channel(snifChannel, WIFI_SECOND_CHAN_NONE);
  }
}

// store a directed probe request (source MAC + requested SSID)
void handleProbe(const uint8_t *p, int len, int rssi)
{
  if (len < 26) return;
  if (p[24] != 0) return;                 // first tag must be SSID (tag 0)
  int ssidLen = p[25];
  if (ssidLen == 0) return;               // skip broadcast/wildcard probes
  if (ssidLen > 32) ssidLen = 32;
  if (26 + ssidLen > len) return;
  char ssid[33];
  memcpy(ssid, &p[26], ssidLen); ssid[ssidLen] = 0;
  char mac[18];
  sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", p[10], p[11], p[12], p[13], p[14], p[15]);
  for (int i = 0; i < probeCount; i++) {
    if (strcmp(probes[i].mac, mac) == 0 && strcmp(probes[i].ssid, ssid) == 0) {
      probes[i].rssi = rssi; return;      // dedup
    }
  }
  if (probeCount < MAX_PROBE) {
    strncpy(probes[probeCount].ssid, ssid, 33); probes[probeCount].ssid[32] = 0;
    strncpy(probes[probeCount].mac, mac, 18);   probes[probeCount].mac[17] = 0;
    probes[probeCount].rssi = rssi;
    probeCount++;
  }
}

// ---- AP <-> client map ----
#define MAX_CLI 40
struct CliEntry { uint8_t client[6]; uint8_t bssid[6]; };
CliEntry clis[MAX_CLI];
volatile int cliCount = 0;
int cliPage = 0;

void handleClient(const uint8_t *p)
{
  uint8_t ds = p[1] & 0x03;                 // ToDS | FromDS
  const uint8_t *a1 = p + 4, *a2 = p + 10;
  const uint8_t *bssid, *client;
  if (ds == 0x01)      { bssid = a1; client = a2; }   // ToDS: client -> AP
  else if (ds == 0x02) { bssid = a2; client = a1; }   // FromDS: AP -> client
  else return;                                        // adhoc / WDS: skip
  if (client[0] & 0x01) return;             // skip multicast/broadcast
  if (bssid[0] & 0x01) return;
  for (int i = 0; i < cliCount; i++)
    if (memcmp(clis[i].client, client, 6) == 0 && memcmp(clis[i].bssid, bssid, 6) == 0) return;
  if (cliCount < MAX_CLI) {
    memcpy(clis[cliCount].client, client, 6);
    memcpy(clis[cliCount].bssid, bssid, 6);
    cliCount++;
  }
}

void snifferCB(void *buf, wifi_promiscuous_pkt_type_t type)
{
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  const uint8_t *p = pkt->payload;
  int rssi = pkt->rx_ctrl.rssi;
  int len  = pkt->rx_ctrl.sig_len;

  if (snifChannel >= 1 && snifChannel <= 13) chanPkts[snifChannel]++;   // heatmap

  uint8_t ftype   = (p[0] >> 2) & 0x03;    // 0=mgmt 2=data
  uint8_t subtype = (p[0] >> 4) & 0x0F;

  if (ftype == 2) {                        // data frame
    if (wifiMode == WFM_CLIENTS) handleClient(p);
    return;
  }
  if (ftype != 0) return;                  // only management beyond here

  if (subtype == 4) {                      // probe request
    if (wifiMode == WFM_PROBE) handleProbe(p, len, rssi);
  } else if (subtype == 12 || subtype == 10) {   // deauth / disassoc
    deauthTotal++;
    deauthWindow++;
  }
}

void startSniffer()
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(10);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&snifferCB);
  snifChannel = 1;
  esp_wifi_set_channel(snifChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = millis();
}

void stopSniffer()
{
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

// ---- Probe scan ----
void writeProbeLog()
{
  if (!sd_init) return;
  myFile = SD.open(probe_fname, FILE_APPEND);
  if (!myFile) return;
  myFile.println("-----espSAK probeScan V" + version + " @ " + fmtUptime() + " -----");
  for (int i = 0; i < probeCount; i++) {
    sprintf(buff, "%s probes \"%s\" rssi=%d", probes[i].mac, probes[i].ssid, probes[i].rssi);
    myFile.println(buff);
  }
  myFile.close();
}

void renderProbe()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("Probe scan", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int n = probeCount;
  if (n == 0) {
    tft.drawString("listening...", 2, 40);
  } else {
    int pages = (n + PROBE_PER_PAGE - 1) / PROBE_PER_PAGE;
    if (probePage >= pages) probePage = 0;
    int start = probePage * PROBE_PER_PAGE;
    int line = 24;
    for (int i = start; i < start + PROBE_PER_PAGE && i < n; i++) {
      sprintf(buff, "%s (%d)", probes[i].ssid, probes[i].rssi);
      tft.drawString(String(buff), 2, line);
      line += 15;
    }
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d probes ch%d B2:pg L:back", probeCount, snifChannel);
  tft.drawString(String(buff), 2, 120);
}

void enterProbe()
{
  wifiMode = WFM_PROBE;
  probeCount = 0; probePage = 0;
  startSniffer();
  renderProbe();
}

void updateProbe(unsigned long now)
{
  hopChannel(now);
  if (now - lastWifiToolDraw > 700) { lastWifiToolDraw = now; renderProbe(); }
}

// ---- Deauth detector ----
void renderDeauth()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("Deauth detect", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  sprintf(buff, "Total frames: %u", (unsigned)deauthTotal);
  tft.drawString(String(buff), 2, 40);
  sprintf(buff, "Rate: %u /s", (unsigned)deauthRate);
  tft.drawString(String(buff), 2, 58);
  if (deauthRate > 5) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setFreeFont(FM12);
    tft.drawString("ATTACK NEARBY?", 2, 84);
  }
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "ch%d  B1-long: back", snifChannel);
  tft.drawString(String(buff), 2, 120);
}

void enterDeauth()
{
  wifiMode = WFM_DEAUTH;
  deauthTotal = 0; deauthWindow = 0; deauthRate = 0; lastDeauthSec = millis();
  startSniffer();
  renderDeauth();
}

void updateDeauth(unsigned long now)
{
  hopChannel(now);
  if (now - lastDeauthSec >= 1000) {
    lastDeauthSec = now;
    deauthRate = deauthWindow;
    deauthWindow = 0;
    renderDeauth();
  }
}

// ---- Heatmap (channel activity) ----
void renderHeatmap()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Ch heatmap", 2, 2);
  drawRadioIcons();

  uint32_t mx = 1;
  for (int c = 1; c <= 13; c++) if (chanPkts[c] > mx) mx = chanPkts[c];

  int baseY = 108, maxH = 78, bw = 15, x0 = 6, step = 17;
  for (int c = 1; c <= 13; c++) {
    int h = (int)(((uint64_t)chanPkts[c] * maxH) / mx);
    int x = x0 + (c - 1) * step;
    uint16_t col = (c == snifChannel) ? TFT_YELLOW : TFT_GREEN;
    if (h > 0) tft.fillRect(x, baseY - h, bw, h, col);
    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(c), x, baseY + 3);
  }
  tft.setTextFont(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("busiest channels - B1-long: back", 2, 126);
}

void enterHeatmap()
{
  wifiMode = WFM_HEATMAP;
  for (int c = 0; c < 14; c++) chanPkts[c] = 0;
  startSniffer();
  renderHeatmap();
}

void updateHeatmap(unsigned long now)
{
  hopChannel(now);
  if (now - lastWifiToolDraw > 500) { lastWifiToolDraw = now; renderHeatmap(); }
}

// ---- Wifi tools sub-menu (carousel) ----
// ---- Clients (AP map) ----
void writeClientsLog()
{
  if (!sd_init) return;
  myFile = SD.open(wifi_fname, FILE_APPEND);
  if (!myFile) return;
  myFile.println("-----espSAK apClients V" + version + " @ " + fmtUptime() + " -----");
  for (int i = 0; i < cliCount; i++) {
    sprintf(buff, "client %02X:%02X:%02X:%02X:%02X:%02X  ap %02X:%02X:%02X:%02X:%02X:%02X",
            clis[i].client[0], clis[i].client[1], clis[i].client[2], clis[i].client[3], clis[i].client[4], clis[i].client[5],
            clis[i].bssid[0],  clis[i].bssid[1],  clis[i].bssid[2],  clis[i].bssid[3],  clis[i].bssid[4],  clis[i].bssid[5]);
    myFile.println(buff);
  }
  myFile.close();
}

void renderClients()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("Clients", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int n = cliCount;
  if (n == 0) {
    tft.drawString("listening...", 2, 40);
  } else {
    int pages = (n + 5) / 6;
    if (cliPage >= pages) cliPage = 0;
    int start = cliPage * 6;
    int line = 24;
    for (int i = start; i < start + 6 && i < n; i++) {
      sprintf(buff, "%02X:%02X:%02X>%02X:%02X:%02X",
              clis[i].client[3], clis[i].client[4], clis[i].client[5],
              clis[i].bssid[3],  clis[i].bssid[4],  clis[i].bssid[5]);
      tft.drawString(String(buff), 2, line);
      line += 15;
    }
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d cli ch%d B2:pg L:back", cliCount, snifChannel);
  tft.drawString(String(buff), 2, 120);
}

void enterClients()
{
  wifiMode = WFM_CLIENTS;
  cliCount = 0; cliPage = 0;
  startSniffer();
  renderClients();
}

void updateClients(unsigned long now)
{
  hopChannel(now);
  if (now - lastWifiToolDraw > 700) { lastWifiToolDraw = now; renderClients(); }
}

void renderWifiMenu()
{
  switch (wifiMenuPos) {
    case 0: drawCarouselItem("1-Wifi scan",  TFT_GREEN);   break;
    case 1: drawCarouselItem("2-Probe scan", TFT_GOLD);    break;
    case 2: drawCarouselItem("3-Deauth dt",  TFT_RED);     break;
    case 3: drawCarouselItem("4-Heatmap",    TFT_CYAN);    break;
    case 4: drawCarouselItem("5-Clients",    TFT_MAGENTA); break;
  }
}

void enterWifiTools()
{
  if (working_mode == "Captive") { server.end(); dnsServer.stop(); working_mode = "Waiting"; }
  appState = ST_WIFI;
  wifiMode = WFM_MENU;
  wifiMenuPos = 0;
  renderWifiMenu();
}

void wfOnNext()
{
  switch (wifiMode) {
    case WFM_MENU:
      wifiMenuPos = (wifiMenuPos < WIFI_MENU_MAX) ? wifiMenuPos + 1 : 0;
      renderWifiMenu();
      break;
    case WFM_SCAN:
      if (!wifiScanning && wifiCount > WIFI_PER_PAGE) {
        int pages = (wifiCount + WIFI_PER_PAGE - 1) / WIFI_PER_PAGE;
        wifiPage = (wifiPage + 1) % pages;
        renderWifiPage();
      }
      break;
    case WFM_PROBE:
      if (probeCount > PROBE_PER_PAGE) {
        int pages = (probeCount + PROBE_PER_PAGE - 1) / PROBE_PER_PAGE;
        probePage = (probePage + 1) % pages;
        renderProbe();
      }
      break;
    case WFM_CLIENTS:
      if (cliCount > 6) {
        int pages = (cliCount + 5) / 6;
        cliPage = (cliPage + 1) % pages;
        renderClients();
      }
      break;
    default: break;
  }
}

void wfOnSelect()
{
  if (wifiMode != WFM_MENU) return;
  switch (wifiMenuPos) {
    case 0: enterWifiScan(); break;
    case 1: enterProbe();    break;
    case 2: enterDeauth();   break;
    case 3: enterHeatmap();  break;
    case 4: enterClients();  break;
  }
}

void wfOnBack()
{
  switch (wifiMode) {
    case WFM_MENU:
      appState = ST_MENU; drawMenu();
      break;
    case WFM_SCAN:
      wifiScanning = false;
      WiFi.scanDelete();
      WiFi.mode(WIFI_OFF);
      wifiMode = WFM_MENU; renderWifiMenu();
      break;
    case WFM_PROBE:
      writeProbeLog();
      stopSniffer();
      wifiMode = WFM_MENU; renderWifiMenu();
      break;
    case WFM_CLIENTS:
      writeClientsLog();
      stopSniffer();
      wifiMode = WFM_MENU; renderWifiMenu();
      break;
    case WFM_DEAUTH:
    case WFM_HEATMAP:
      stopSniffer();
      wifiMode = WFM_MENU; renderWifiMenu();
      break;
  }
}

void updateWifi(unsigned long now)
{
  switch (wifiMode) {
    case WFM_SCAN:    updateWifiScan(now); break;
    case WFM_PROBE:   updateProbe(now);    break;
    case WFM_DEAUTH:  updateDeauth(now);   break;
    case WFM_HEATMAP: updateHeatmap(now);  break;
    case WFM_CLIENTS: updateClients(now);  break;
    default: break;
  }
}

// ==================================================================
//  SD CARD BROWSER (Fase 2)
// ==================================================================
#define SD_PER_PAGE 6
#define MAX_SD 100
#define MAX_VIEW 2048
#define MAX_LINES 256

struct SdEntry { char name[40]; bool isDir; bool isUp; uint32_t size; };
SdEntry sdEntries[MAX_SD];
int sdCount = 0;
int sdCursor = 0;
String sdPath = "/";
String sdSizeStr = "";

String selPath = "", selName = "";
uint32_t infoSize = 0;
int actCursor = 0;
int confirmCursor = 0;

char viewBuf[MAX_VIEW];
int  viewLen = 0;
int  lineStart[MAX_LINES];
int  lineCount = 0;
int  viewPage = 0;
int  viewCPL = 20, viewLPP = 6, viewLineH = 15;

String baseName(const char *n)
{
  String s = String(n);
  int idx = s.lastIndexOf('/');
  if (idx >= 0) s = s.substring(idx + 1);
  return s;
}
String joinPath(String d, String name)
{
  if (d.endsWith("/")) return d + name;
  return d + "/" + name;
}
String parentPath(String p)
{
  if (p == "/" || p.length() == 0) return "/";
  int idx = p.lastIndexOf('/');
  if (idx <= 0) return "/";
  return p.substring(0, idx);
}

void loadDir(String path)
{
  sdPath = path; sdCount = 0; sdCursor = 0;
  if (!sd_init) return;
  if (path != "/") {
    strcpy(sdEntries[0].name, "..");
    sdEntries[0].isDir = true; sdEntries[0].isUp = true; sdEntries[0].size = 0;
    sdCount = 1;
  }
  File d = SD.open(path);
  if (!d) return;
  d.rewindDirectory();
  while (sdCount < MAX_SD) {
    File e = d.openNextFile();
    if (!e) break;
    String nm = baseName(e.name());
    nm = nm.substring(0, 39);
    strncpy(sdEntries[sdCount].name, nm.c_str(), 40);
    sdEntries[sdCount].name[39] = 0;
    sdEntries[sdCount].isDir = e.isDirectory();
    sdEntries[sdCount].isUp = false;
    sdEntries[sdCount].size = (uint32_t)e.size();
    e.close();
    sdCount++;
  }
  d.close();
}

void renderSDList()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  String hdr = sdPath;
  if (hdr.length() > 20) hdr = "..." + hdr.substring(hdr.length() - 17);
  tft.drawString(hdr, 2, 2);

  if (!sd_init) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD not mounted", tft.width() / 2, tft.height() / 2);
    return;
  }
  if (sdCount == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    tft.drawString("(empty)", tft.width() / 2, tft.height() / 2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("B1-long: up/back", 2, 120);
    return;
  }
  int page = sdCursor / SD_PER_PAGE;
  int pages = (sdCount + SD_PER_PAGE - 1) / SD_PER_PAGE;
  int start = page * SD_PER_PAGE;
  int line = 22;
  for (int i = start; i < start + SD_PER_PAGE && i < sdCount; i++) {
    bool sel = (i == sdCursor);
    tft.setTextColor(sel ? TFT_GREEN : TFT_GOLD, TFT_BLACK);
    String fn = String(sel ? ">" : " ");
    String nm = String(sdEntries[i].name);
    if (nm.length() > 16) nm = nm.substring(0, 16);
    fn += nm;
    if (sdEntries[i].isUp)       fn += "/ <UP>";
    else if (sdEntries[i].isDir) fn += "/ <DIR>";
    else {
      if (sdEntries[i].size >= 1024) fn += " " + String(sdEntries[i].size / 1024) + "KB";
      else                           fn += " " + String(sdEntries[i].size) + "B";
    }
    tft.drawString(fn, 2, line);
    line += 15;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d/%d pg%d/%d B2:v B1:ok L:up", sdCursor + 1, sdCount, page + 1, pages);
  tft.drawString(String(buff), 2, 120);
}

void renderSDActions()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(selName.substring(0, 20), 2, 2);
  const char *opts[3] = {"View", "Delete", "Cancel"};
  int y = 40;
  for (int i = 0; i < 3; i++) {
    bool sel = (i == actCursor);
    tft.setTextColor(sel ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    tft.drawString(String(sel ? "> " : "  ") + opts[i], 12, y);
    y += 18;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B2:move B1:ok B1-long:back", 2, 120);
}

void renderConfirm()
{
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FM9);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("Delete file?", tft.width() / 2, 22);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(selName.substring(0, 24), tft.width() / 2, 46);
  tft.setTextColor(confirmCursor == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  tft.drawString(confirmCursor == 0 ? "> NO" : "  NO", tft.width() / 2 - 40, 80);
  tft.setTextColor(confirmCursor == 1 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  tft.drawString(confirmCursor == 1 ? "> YES" : "  YES", tft.width() / 2 + 40, 80);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B2:toggle B1:ok L:cancel", 2, 120);
}

void renderInfo()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(selName.substring(0, 20), 2, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Binary file", 2, 28);
  sprintf(buff, "Size: %u bytes", infoSize);
  tft.drawString(String(buff), 2, 44);
  String hex = "";
  for (int i = 0; i < 16 && i < viewLen; i++) {
    char h[4]; sprintf(h, "%02X ", (uint8_t)viewBuf[i]); hex += h;
  }
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(hex.substring(0, 24), 2, 64);
  if (hex.length() > 24) tft.drawString(hex.substring(24), 2, 78);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B1-long: back", 2, 120);
}

int viewPages()
{
  int p = (lineCount + viewLPP - 1) / viewLPP;
  return (p < 1) ? 1 : p;
}

void computeViewLayout()
{
  tft.setFreeFont(FM9);
  int cw = tft.textWidth("W"); if (cw < 1) cw = 6;
  viewCPL = (tft.width() - 4) / cw;
  if (viewCPL < 1) viewCPL = 1;
  if (viewCPL > 120) viewCPL = 120;
  int fh = tft.fontHeight(); if (fh < 1) fh = 14;
  viewLineH = fh;
  int top = 24, reserved = 16;
  viewLPP = (tft.height() - top - reserved) / viewLineH;
  if (viewLPP < 1) viewLPP = 1;
}

void computeLines()
{
  lineCount = 0;
  int i = 0;
  while (i < viewLen && lineCount < MAX_LINES) {
    lineStart[lineCount++] = i;
    int col = 0;
    while (i < viewLen) {
      char c = viewBuf[i];
      if (c == '\n') { i++; break; }
      if (c == '\r') { i++; continue; }
      col++; i++;
      if (col >= viewCPL) break;
    }
  }
}

void renderViewPage()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(selName.substring(0, viewCPL), 2, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (lineCount == 0) {
    tft.drawString("(empty)", 2, 24);
  } else {
    int startLine = viewPage * viewLPP;
    int y = 24;
    char lbuf[128];
    for (int l = startLine; l < startLine + viewLPP && l < lineCount; l++) {
      int s = lineStart[l];
      int e = (l + 1 < lineCount) ? lineStart[l + 1] : viewLen;
      int n = 0;
      for (int k = s; k < e && n < viewCPL && n < 127; k++) {
        char c = viewBuf[k];
        if (c == '\n' || c == '\r') continue;
        lbuf[n++] = c;
      }
      lbuf[n] = 0;
      tft.drawString(lbuf, 2, y);
      y += viewLineH;
    }
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "pg %d/%d B2:more B1-long:back", viewPage + 1, viewPages());
  tft.drawString(String(buff), 2, 120);
}

void openInfo(uint32_t fsize) { infoSize = fsize; sdMode = SDM_INFO; renderInfo(); }

void openViewer(String path)
{
  File f = SD.open(path, FILE_READ);
  if (!f) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FM9);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Cannot open", tft.width() / 2, tft.height() / 2);
    delay(700);
    sdMode = SDM_LIST; renderSDList();
    return;
  }
  uint32_t fsize = f.size();
  viewLen = 0;
  int nonprint = 0;
  while (f.available() && viewLen < MAX_VIEW - 1) {
    int c = f.read();
    if (c < 0) break;
    viewBuf[viewLen++] = (char)c;
    if (!(c == '\t' || c == '\n' || c == '\r' || (c >= 32 && c <= 126))) nonprint++;
  }
  viewBuf[viewLen] = 0;
  f.close();
  bool binary = (viewLen > 0) && ((nonprint * 100 / viewLen) > 15);
  if (binary) { openInfo(fsize); return; }
  computeViewLayout();
  computeLines();
  viewPage = 0;
  sdMode = SDM_VIEW;
  renderViewPage();
}

void renderSdMenu()
{
  switch (sdMenuPos) {
    case 0: drawCarouselItem("1-Explore",  TFT_GOLD);    break;
    case 1: drawCarouselItem("2-Wipe all", TFT_RED);     break;
    case 2: drawCarouselItem("3-Web srv",  TFT_MAGENTA); break;
  }
}

void enterSDCard()
{
  appState = ST_SDCARD;
  sdMode = SDM_MENU;
  sdMenuPos = 0;
  if (!sd_init) { sd_init = initSD(); if (sd_init) { prepareSdFiles(); loadConfig(); applyConfig(); } }
  renderSdMenu();
}

// recursive delete; reopens the dir each step to avoid delete-while-iterating issues
void rmRf(String path)
{
  File d = SD.open(path);
  if (!d) return;
  bool isdir = d.isDirectory();
  d.close();
  if (!isdir) { SD.remove(path); return; }
  while (true) {
    File dd = SD.open(path);
    if (!dd) return;
    dd.rewindDirectory();
    File e = dd.openNextFile();
    if (!e) { dd.close(); break; }
    String child = joinPath(path, baseName(e.name()));
    bool childDir = e.isDirectory();
    e.close(); dd.close();
    if (childDir) rmRf(child);
    else SD.remove(child);
  }
  if (path != "/") SD.rmdir(path);
}

void wipeAll()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("Wiping...", tft.width() / 2, tft.height() / 2);
  // delete every top-level item EXCEPT /config (keeps settings + captive html)
  while (true) {
    File dd = SD.open("/");
    if (!dd) break;
    dd.rewindDirectory();
    String target = ""; bool isdir = false;
    while (true) {
      File e = dd.openNextFile();
      if (!e) break;
      String full = joinPath("/", baseName(e.name()));
      bool dir = e.isDirectory();
      e.close();
      if (full == "/config") continue;   // preserve config
      target = full; isdir = dir; break;
    }
    dd.close();
    if (target == "") break;
    if (isdir) rmRf(target); else SD.remove(target);
  }
  ensureFolders();                       // recreate the empty log structure
  captive_fname = nextName("/captive", "cap_");
  wifi_fname    = nextName("/wifi",    "scan_");
  probe_fname   = nextName("/probe",   "probe_");
  bt_fname      = nextName("/bt",      "bt_");
  track_fname   = nextName("/track",   "trk_");
}

void renderWipeConfirm()
{
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FM9);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("WIPE ALL FILES?", tft.width() / 2, 20);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("deletes all logs", tft.width() / 2, 42);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("(/config is kept)", tft.width() / 2, 58);
  tft.setTextColor(confirmCursor == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  tft.drawString(confirmCursor == 0 ? "> NO" : "  NO", tft.width() / 2 - 40, 78);
  tft.setTextColor(confirmCursor == 1 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  tft.drawString(confirmCursor == 1 ? "> YES" : "  YES", tft.width() / 2 + 40, 78);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B2:toggle B1:ok L:cancel", 2, 120);
}

void sdConfirmWipe()
{
  if (confirmCursor == 1) {
    wipeAll();
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FM12);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Done", tft.width() / 2, tft.height() / 2);
    delay(800);
  }
  sdMode = SDM_MENU;
  renderSdMenu();
}

void enterSDList()
{
  appState = ST_SDCARD;
  sdMode = SDM_LIST;
  if (!sd_init) sd_init = initSD();
  if (sd_init) {
    String t = "";
    uint8_t ct = SD.cardType();
    if (ct == CARD_MMC) t = "MMC";
    else if (ct == CARD_SD) t = "SDSC";
    else if (ct == CARD_SDHC) t = "SDHC";
    else t = "UNK";
    sdSizeStr = t + "-" + String((uint32_t)(SD.cardSize() / 1024 / 1024)) + "MB";
    loadDir("/");
  }
  renderSDList();
}

void sdOpenSelected()
{
  if (sdCount == 0) return;
  SdEntry &e = sdEntries[sdCursor];
  if (e.isUp) { loadDir(parentPath(sdPath)); renderSDList(); return; }
  if (e.isDir) { loadDir(joinPath(sdPath, String(e.name))); renderSDList(); return; }
  selName = String(e.name);
  selPath = joinPath(sdPath, selName);
  actCursor = 0;
  sdMode = SDM_ACTIONS;
  renderSDActions();
}

void sdRunAction()
{
  if (actCursor == 0) openViewer(selPath);
  else if (actCursor == 1) { confirmCursor = 0; sdMode = SDM_CONFIRM; renderConfirm(); }
  else { sdMode = SDM_LIST; renderSDList(); }
}

void sdConfirmChoice()
{
  if (confirmCursor == 1) {
    bool ok = SD.remove(selPath);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FM9);
    tft.setTextColor(ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.drawString(ok ? "Deleted" : "Delete failed", tft.width() / 2, tft.height() / 2);
    delay(700);
    loadDir(sdPath);
  }
  sdMode = SDM_LIST;
  renderSDList();
}

void sdOnNext()
{
  switch (sdMode) {
    case SDM_MENU:    sdMenuPos = (sdMenuPos < SD_MENU_MAX) ? sdMenuPos + 1 : 0; renderSdMenu(); break;
    case SDM_LIST:    if (sdCount > 0) { sdCursor = (sdCursor + 1) % sdCount; renderSDList(); } break;
    case SDM_ACTIONS: actCursor = (actCursor + 1) % 3; renderSDActions(); break;
    case SDM_VIEW:    if (viewPages() > 1) { viewPage = (viewPage + 1) % viewPages(); renderViewPage(); } break;
    case SDM_CONFIRM: confirmCursor ^= 1; renderConfirm(); break;
    case SDM_WIPE:    confirmCursor ^= 1; renderWipeConfirm(); break;
    case SDM_INFO:    break;
  }
}

void sdOnSelect()
{
  switch (sdMode) {
    case SDM_MENU:
      if (sdMenuPos == 0) enterSDList();
      else if (sdMenuPos == 1) { confirmCursor = 0; sdMode = SDM_WIPE; renderWipeConfirm(); }
      else enterWebSrv();
      break;
    case SDM_LIST:    sdOpenSelected(); break;
    case SDM_ACTIONS: sdRunAction(); break;
    case SDM_VIEW:    if (viewPages() > 1) { viewPage = (viewPage + 1) % viewPages(); renderViewPage(); } break;
    case SDM_CONFIRM: sdConfirmChoice(); break;
    case SDM_WIPE:    sdConfirmWipe(); break;
    case SDM_INFO:    break;
  }
}

void sdOnBack()
{
  switch (sdMode) {
    case SDM_MENU:
      appState = ST_MENU; drawMenu();
      break;
    case SDM_LIST:
      if (sdPath == "/" || !sd_init) { sdMode = SDM_MENU; renderSdMenu(); }
      else { loadDir(parentPath(sdPath)); renderSDList(); }
      break;
    case SDM_WIPE:
      sdMode = SDM_MENU; renderSdMenu();
      break;
    default:
      sdMode = SDM_LIST; renderSDList();
      break;
  }
}

// ==================================================================
//  BLUETOOTH (Fase 3 - Classic part)
// ==================================================================
const int BT_MENU_MAX = 4;   // items 0..4

// ---- BLE advertiser (generic: named / iBeacon / Eddystone) ----
#define MAX_ADV 20
struct AdvEntry { char name[24]; char type[10]; char params[80]; uint8_t mac[6]; };
AdvEntry advList[MAX_ADV];
int advCount = 0;
int advCursor = 0;
int advActive = -1;          // index currently advertising, -1 = none
uint8_t advMfr[25];          // iBeacon manufacturer data (the stack wraps it)
esp_ble_adv_params_t advParams;

// adv-data config is asynchronous: start advertising only once it completes
void advGapCB(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
      Serial.printf("[adv] DATA_SET status=%d -> start\n", param->adv_data_cmpl.status);
      esp_ble_gap_start_advertising(&advParams);
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      Serial.printf("[adv] ADV_START status=%d\n", param->adv_start_cmpl.status);
      break;
    case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
      Serial.printf("[adv] SET_RAND_ADDR status=%d\n", param->set_rand_addr_cmpl.status);
      break;
    
case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
  esp_ble_gap_start_advertising(&advParams);
  break;
    default: break;
  }
}

// bring up a private BLE stack for the advertiser (no BLEDevice; modal use only)
void bleAdvInit()
{
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t a = esp_bt_controller_init(&cfg);
  esp_err_t b = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
  esp_err_t c = esp_bluedroid_init();
  esp_err_t d = esp_bluedroid_enable();
  esp_ble_gap_register_callback(advGapCB);
  Serial.printf("[adv] init ctrl=%d en=%d bd=%d bden=%d\n", a, b, c, d);
}

void bleAdvDeinit()
{
  esp_ble_gap_stop_advertising();
  esp_bluedroid_disable();
  esp_bluedroid_deinit();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
}
int btMenuPos = 0;

// --- SPP console ---
String sppLine = "";
String sppCwd  = "/";
bool lastHasClient = false;

// --- Classic discovery ---
#define MAX_BT 40
struct BtEntry { char name[32]; char addr[20]; int rssi; uint32_t cod; };
BtEntry btEntries[MAX_BT];
volatile int btCount = 0;
bool btDiscovering = false;
unsigned long btDiscStart = 0;
int btPage = 0;

const char* btMajorClass(uint32_t cod)
{
  uint8_t major = (cod >> 8) & 0x1F;
  switch (major) {
    case 1: return "Computer";
    case 2: return "Phone";
    case 3: return "Network";
    case 4: return "Audio/Vid";
    case 5: return "Periph";
    case 6: return "Imaging";
    case 7: return "Wearable";
    case 8: return "Toy";
    case 9: return "Health";
    default: return "Misc";
  }
}

String resolveArg(String arg)
{
  if (arg.length() == 0) return "";
  if (arg.startsWith("/")) return arg;
  return joinPath(sppCwd, arg);
}

void sppHelp()
{
  SerialBT.println("Commands:");
  SerialBT.println(" help         this help");
  SerialBT.println(" info         system info");
  SerialBT.println(" bat          battery status");
  SerialBT.println(" ls [path]    list directory");
  SerialBT.println(" cat <file>   show text file");
  SerialBT.println(" rm <file>    delete file");
  SerialBT.println(" cd <path>    change dir");
  SerialBT.println(" reboot       restart device");
}

void sppInfo()
{
  SerialBT.println("=== espSAK console V" + version + " ===");
  SerialBT.println("Battery : " + String(batteryVoltage) + "V (" + String(batteryPct) + "%)");
  SerialBT.println("Uptime  : " + String(millis() / 1000) + " s");
  SerialBT.println("SD      : " + String(sd_init ? "OK" : "not mounted"));
  SerialBT.println("FreeHeap: " + String(ESP.getFreeHeap()));
  SerialBT.println("CWD     : " + sppCwd);
}

void sppLs(String arg)
{
  if (!sd_init) { SerialBT.println("SD not mounted"); return; }
  String p = (arg == "") ? sppCwd : resolveArg(arg);
  File d = SD.open(p);
  if (!d || !d.isDirectory()) { SerialBT.println("not a dir: " + p); if (d) d.close(); return; }
  d.rewindDirectory();
  while (true) {
    File e = d.openNextFile();
    if (!e) break;
    String nm = baseName(e.name());
    if (e.isDirectory()) SerialBT.println("  " + nm + "/");
    else SerialBT.println("  " + nm + "  " + String((uint32_t)e.size()) + "B");
    e.close();
  }
  d.close();
}

void sppCat(String arg)
{
  if (arg == "") { SerialBT.println("usage: cat <file>"); return; }
  if (!sd_init) { SerialBT.println("SD not mounted"); return; }
  String p = resolveArg(arg);
  File f = SD.open(p, FILE_READ);
  if (!f || f.isDirectory()) { SerialBT.println("cannot open: " + p); if (f) f.close(); return; }
  uint32_t sent = 0; const uint32_t CAP = 16384; uint8_t cb2[128];
  while (f.available() && sent < CAP) {
    int n = f.read(cb2, 128);
    if (n <= 0) break;
    SerialBT.write(cb2, n);
    sent += n;
  }
  SerialBT.println();
  if (f.available()) SerialBT.println("[truncated at 16KB]");
  f.close();
}

void sppRm(String arg)
{
  if (arg == "") { SerialBT.println("usage: rm <file>"); return; }
  if (!sd_init) { SerialBT.println("SD not mounted"); return; }
  String p = resolveArg(arg);
  if (SD.remove(p)) SerialBT.println("deleted: " + p);
  else SerialBT.println("delete failed: " + p);
}

void sppCd(String arg)
{
  if (arg == "" || arg == "/") sppCwd = "/";
  else if (arg == "..") sppCwd = parentPath(sppCwd);
  else {
    String p = resolveArg(arg);
    File d = SD.open(p);
    if (d && d.isDirectory()) { sppCwd = p; d.close(); }
    else { SerialBT.println("no such dir"); if (d) d.close(); }
  }
  SerialBT.println("cwd: " + sppCwd);
}

void sppExecute(String line)
{
  line.trim();
  if (line.length() == 0) return;
  int sp = line.indexOf(' ');
  String cmd = (sp < 0) ? line : line.substring(0, sp);
  String arg = (sp < 0) ? "" : line.substring(sp + 1);
  arg.trim();
  cmd.toLowerCase();
  if (cmd == "help") sppHelp();
  else if (cmd == "info") sppInfo();
  else if (cmd == "bat") SerialBT.println("Battery: " + String(batteryVoltage) + "V (" + String(batteryPct) + "%)");
  else if (cmd == "ls") sppLs(arg);
  else if (cmd == "cat") sppCat(arg);
  else if (cmd == "rm") sppRm(arg);
  else if (cmd == "cd") sppCd(arg);
  else if (cmd == "reboot") { SerialBT.println("Rebooting..."); delay(300); ESP.restart(); }
  else SerialBT.println("unknown cmd '" + cmd + "', type help");
}

void sppBanner()
{
  SerialBT.println();
  sppInfo();
  sppHelp();
  SerialBT.print("> ");
}

void renderSPP(String status)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("BT Console", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Name: espSAK", 2, 34);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("Pair & accept on PC", 2, 50);
  tft.drawString("(no PIN needed)", 2, 64);
  tft.setTextColor(status == "Client connected" ? TFT_GREEN : TFT_CYAN, TFT_BLACK);
  tft.drawString(status, 2, 90);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B1-long: exit", 2, 120);
}

void enterSPP()
{
  btMode = BTM_SPP;
  sppLine = ""; sppCwd = "/"; lastHasClient = false;
  SerialBT.begin("espSAK");   // Just Works, no PIN
  renderSPP("Waiting for client");
}

void serviceSPP(unsigned long now)
{
  bool c = SerialBT.hasClient();
  if (c && !lastHasClient) { sppBanner(); renderSPP("Client connected"); }
  if (!c && lastHasClient) { renderSPP("Waiting for client"); }
  lastHasClient = c;

  if (!c) return;

  while (SerialBT.available()) {
    char ch = SerialBT.read();
    if (ch == '\r' || ch == '\n') {         // PuTTY sends CR on Enter
      if (sppLine.length() > 0) {           // ignore the LF of a CRLF pair and empty lines
        SerialBT.print("\r\n");             // echo the newline
        sppExecute(sppLine);
        sppLine = "";
        SerialBT.print("> ");
      }
    } else if (ch == 8 || ch == 127) {      // backspace / del
      if (sppLine.length() > 0) {
        sppLine.remove(sppLine.length() - 1);
        SerialBT.print("\b \b");            // erase on terminal
      }
    } else if (ch >= 32 && ch < 127 && sppLine.length() < 120) {
      sppLine += ch;
      SerialBT.write(ch);                   // echo the typed char
    }
  }
}

// discovery callback (runs in BT task context)
void btDiscoveryCallback(BTAdvertisedDevice *d)
{
  if (btCount >= MAX_BT) return;
  int i = btCount;
  String nm = d->haveName() ? String(d->getName().c_str()) : String("<n/a>");
  nm = nm.substring(0, 31);
  strncpy(btEntries[i].name, nm.c_str(), 32); btEntries[i].name[31] = 0;
  String ad = String(d->getAddress().toString().c_str());
  ad = ad.substring(0, 19);
  strncpy(btEntries[i].addr, ad.c_str(), 20); btEntries[i].addr[19] = 0;
  btEntries[i].rssi = d->haveRSSI() ? d->getRSSI() : 0;
  btEntries[i].cod  = d->haveCOD()  ? d->getCOD()  : 0;
  btCount++;
}

void writeBtLog()
{
  if (!sd_init) return;
  myFile = SD.open(bt_fname, FILE_APPEND);
  if (!myFile) { Serial.println("error opening " + bt_fname); return; }
  myFile.println("-----espSAK btDiscovery V" + version + " @ " + fmtUptime() + " -----");
  for (int i = 0; i < btCount; i++) {
    sprintf(buff, "%s %s rssi=%d class=%s(0x%06X)",
            btEntries[i].addr, btEntries[i].name, btEntries[i].rssi,
            btMajorClass(btEntries[i].cod), (unsigned int)btEntries[i].cod);
    myFile.println(buff);
  }
  myFile.close();
}

void renderDiscovery()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("BT discovery", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  if (btCount == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.drawString("no devices", tft.width() / 2, tft.height() / 2);
    return;
  }
  int pages = (btCount + 5) / 6;
  int start = btPage * 6;
  int line = 24;
  for (int i = start; i < start + 6 && i < btCount; i++) {
    sprintf(buff, "%s %d %s", btEntries[i].name, btEntries[i].rssi, btMajorClass(btEntries[i].cod));
    tft.drawString(String(buff), 2, line);
    line += 15;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d dev pg%d/%d B2:next L:back", btCount, btPage + 1, pages);
  tft.drawString(String(buff), 2, 120);
}

void enterDiscovery()
{
  btMode = BTM_DISCOVERY;
  btCount = 0; btDiscovering = true; btDiscStart = millis(); btPage = 0;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("BT discovery", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("Discovering 6s...", 2, 40);
  SerialBT.begin("espSAK");
  SerialBT.discoverAsync(btDiscoveryCallback);
}

unsigned long lastDiscCount = 0;
void updateBtDiscovery(unsigned long now)
{
  if (!btDiscovering) return;
  if (now - btDiscStart > 6000) {
    SerialBT.discoverAsyncStop();
    btDiscovering = false;
    writeBtLog();
    btPage = 0;
    renderDiscovery();
  } else if (now - lastDiscCount > 1000) {
    lastDiscCount = now;
    tft.fillRect(2, 70, 200, 16, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(FM9);
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    sprintf(buff, "found: %d", btCount);
    tft.drawString(String(buff), 2, 70);
  }
}

// ---------------- BLE scan (Bluedroid) ----------------
#define MAX_BLE 40
struct BleEntry { char name[32]; char addr[20]; int rssi; char vendor[16]; };
BleEntry bleEntries[MAX_BLE];
volatile int bleCount = 0;
bool bleScanning = false;
volatile bool bleScanDone = false;
int blePage = 0;
unsigned long lastBleCount = 0;

String bleVendor(uint16_t cid)
{
  switch (cid) {
    case 0x004C: return "Apple";
    case 0x0006: return "Microsoft";
    case 0x00E0: return "Google";
    case 0x0075: return "Samsung";
    case 0x0059: return "Nordic";
    case 0x0087: return "Garmin";
    default: { char b[8]; sprintf(b, "0x%04X", cid); return String(b); }
  }
}

class BleScanCB : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice d)
  {
    if (bleCount >= MAX_BLE) return;
    int i = bleCount;
    String nm = d.haveName() ? String(d.getName().c_str()) : String("<n/a>");
    nm = nm.substring(0, 31);
    strncpy(bleEntries[i].name, nm.c_str(), 32); bleEntries[i].name[31] = 0;
    String ad = String(d.getAddress().toString().c_str());
    ad = ad.substring(0, 19);
    strncpy(bleEntries[i].addr, ad.c_str(), 20); bleEntries[i].addr[19] = 0;
    bleEntries[i].rssi = d.getRSSI();
    String vend = "-";
    if (d.haveManufacturerData()) {
      String md = d.getManufacturerData();   // Arduino String in current ESP32 cores
      if (md.length() >= 2) {
        uint16_t cid = (uint8_t)md[0] | ((uint8_t)md[1] << 8);  // company id, little-endian
        vend = bleVendor(cid);
      }
    }
    vend = vend.substring(0, 15);
    strncpy(bleEntries[i].vendor, vend.c_str(), 16); bleEntries[i].vendor[15] = 0;
    bleCount++;
  }
};
BleScanCB bleCB;

void bleScanComplete(BLEScanResults r) { bleScanDone = true; }   // runs in BLE task

void writeBleLog()
{
  if (!sd_init) return;
  myFile = SD.open(bt_fname, FILE_APPEND);
  if (!myFile) { Serial.println("error opening " + bt_fname); return; }
  myFile.println("-----espSAK bleScan V" + version + " @ " + fmtUptime() + " -----");
  for (int i = 0; i < bleCount; i++) {
    sprintf(buff, "%s %s rssi=%d vendor=%s",
            bleEntries[i].addr, bleEntries[i].name, bleEntries[i].rssi, bleEntries[i].vendor);
    myFile.println(buff);
  }
  myFile.close();
}

void renderBleScan()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("BLE scan", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  if (bleCount == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.drawString("no devices", tft.width() / 2, tft.height() / 2);
    return;
  }
  int pages = (bleCount + 5) / 6;
  int start = blePage * 6;
  int line = 24;
  for (int i = start; i < start + 6 && i < bleCount; i++) {
    sprintf(buff, "%s %d %s", bleEntries[i].name, bleEntries[i].rssi, bleEntries[i].vendor);
    tft.drawString(String(buff), 2, line);
    line += 15;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d dev pg%d/%d B2:next L:back", bleCount, blePage + 1, pages);
  tft.drawString(String(buff), 2, 120);
}

void enterBleScan()
{
  btMode = BTM_BLESCAN;
  bleCount = 0; bleScanning = true; bleScanDone = false; blePage = 0;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("BLE scan", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("Scanning 6s...", 2, 40);

  BLEDevice::init("");
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(&bleCB, false);
  pScan->setActiveScan(true);     // request scan response (names, more data)
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(6, bleScanComplete, false);   // 6 seconds, async (seconds in Bluedroid)
}

void updateBleScan(unsigned long now)
{
  if (!bleScanning) return;
  if (bleScanDone) {
    bleScanning = false;
    BLEDevice::getScan()->clearResults();
    writeBleLog();
    blePage = 0;
    renderBleScan();
  } else if (now - lastBleCount > 1000) {
    lastBleCount = now;
    tft.fillRect(2, 70, 200, 16, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(FM9);
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    sprintf(buff, "found: %d", bleCount);
    tft.drawString(String(buff), 2, 70);
  }
}

// ---------------- BLE Trackers (AirTag / Tile / SmartTag) + Watch ----------------
#define MAX_TRK 30
struct TrkEntry { char type[12]; char addr[20]; int rssi; };
TrkEntry trk[MAX_TRK];
volatile int trkCount = 0;
int trkCursor = 0;
bool trkScanning = false;
volatile bool trkScanDone = false;

// watch mode
String watchAddr = "";
char   watchType[12] = "";
volatile int watchRssi = -127;
int  prevWatchRssi = -127;
volatile unsigned long watchLastSeen = 0;
volatile bool watchScanEnded = false;
unsigned long lastWatchDraw = 0;

// Returns a label for known trackers, or nullptr if not a tracker.
const char* classifyTracker(BLEAdvertisedDevice &d)
{
  if (d.haveManufacturerData()) {
    String md = d.getManufacturerData();
    if (md.length() >= 3 && (uint8_t)md[0] == 0x4C && (uint8_t)md[1] == 0x00) {
      if ((uint8_t)md[2] == 0x12) return "FindMy";   // Apple Find My (separated AirTag etc.)
    }
  }
  if (d.haveServiceUUID()) {
    BLEUUID u = d.getServiceUUID();
    if (u.equals(BLEUUID((uint16_t)0xFEED))) return "Tile";
    if (u.equals(BLEUUID((uint16_t)0xFD5A))) return "SmartTag";
  }
  return nullptr;
}

class TrkScanCB : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice d)
  {
    const char *label = classifyTracker(d);
    if (!label) return;
    if (trkCount >= MAX_TRK) return;
    char ad[20];
    strncpy(ad, d.getAddress().toString().c_str(), 20); ad[19] = 0;
    for (int i = 0; i < trkCount; i++) {
      if (strcmp(ad, trk[i].addr) == 0) { trk[i].rssi = d.getRSSI(); return; }
    }
    int i = trkCount;
    strncpy(trk[i].type, label, 12); trk[i].type[11] = 0;
    strncpy(trk[i].addr, ad, 20);    trk[i].addr[19] = 0;
    trk[i].rssi = d.getRSSI();
    trkCount++;
  }
};
TrkScanCB trkCB;

class WatchCB : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice d)
  {
    if (watchAddr.equals(d.getAddress().toString().c_str())) {
      watchRssi = d.getRSSI();
      watchLastSeen = millis();
    }
  }
};
WatchCB watchCB;

void trkScanComplete(BLEScanResults r) { trkScanDone = true; }
void watchScanComplete(BLEScanResults r) { watchScanEnded = true; }

void writeTrkLog()
{
  if (!sd_init) return;
  myFile = SD.open(track_fname, FILE_APPEND);
  if (!myFile) return;
  myFile.println("-----espSAK trackers V" + version + " @ " + fmtUptime() + " -----");
  for (int i = 0; i < trkCount; i++) {
    sprintf(buff, "%s %s rssi=%d", trk[i].addr, trk[i].type, trk[i].rssi);
    myFile.println(buff);
  }
  myFile.close();
}

void renderTrackers()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("Trackers", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  if (trkScanning) {
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    tft.drawString("Scanning 6s...", 2, 40);
    sprintf(buff, "found: %d", trkCount);
    tft.drawString(String(buff), 2, 70);
    return;
  }
  if (trkCount == 0) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("no trackers nearby", 2, 40);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("B1-long: back", 2, 120);
    return;
  }
  int page = trkCursor / 6;
  int start = page * 6;
  int line = 24;
  for (int i = start; i < start + 6 && i < trkCount; i++) {
    bool sel = (i == trkCursor);
    tft.setTextColor(sel ? TFT_GREEN : TFT_GOLD, TFT_BLACK);
    sprintf(buff, "%s%s (%d)", sel ? ">" : " ", trk[i].type, trk[i].rssi);
    tft.drawString(String(buff), 2, line);
    line += 15;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d/%d B2:mv B1:watch L:back", trkCursor + 1, trkCount);
  tft.drawString(String(buff), 2, 120);
}

void enterTrackers()
{
  btMode = BTM_TRACKERS;
  trkCount = 0; trkCursor = 0; trkScanning = true; trkScanDone = false;
  renderTrackers();
  BLEDevice::init("");
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(&trkCB, false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(6, trkScanComplete, false);
}

void updateTrackers(unsigned long now)
{
  if (!trkScanning) return;
  if (trkScanDone) {
    trkScanning = false;
    BLEDevice::getScan()->clearResults();
    writeTrkLog();
    trkCursor = 0;
    renderTrackers();
  } else if (now - lastBleCount > 1000) {
    lastBleCount = now;
    renderTrackers();     // refresh "found: N"
  }
}

void startWatchScan()
{
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(&watchCB, false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  watchScanEnded = false;
  pScan->start(5, watchScanComplete, false);   // rolling 5s windows
}

void renderWatch()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("Watch", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString(String(watchType) + " " + watchAddr, 2, 28);

  bool lost = (millis() - watchLastSeen > 6000);
  if (lost) {
    tft.setFreeFont(FM12);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("out of range", 2, 64);
  } else {
    int rs = watchRssi;
    if (rs < -100) rs = -100; if (rs > -30) rs = -30;
    int pct = map(rs, -100, -30, 0, 100);
    int w = (tft.width() - 8) * pct / 100;
    uint16_t col = (pct > 66) ? TFT_RED : (pct > 33) ? TFT_GOLD : TFT_GREEN;  // closer = hot
    tft.fillRect(4, 52, tft.width() - 8, 18, TFT_DARKGREY);
    tft.fillRect(4, 52, w, 18, col);
    tft.setFreeFont(FM12);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    sprintf(buff, "%d dBm", watchRssi);
    tft.drawString(String(buff), 4, 80);
    const char *trend = "=";
    if (watchRssi > prevWatchRssi + 2) trend = "CLOSER";
    else if (watchRssi < prevWatchRssi - 2) trend = "farther";
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(trend, 130, 80);
    prevWatchRssi = watchRssi;
  }
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B1-long: back", 2, 120);
}

void enterWatch()
{
  btMode = BTM_WATCH;
  watchAddr = String(trk[trkCursor].addr);
  strncpy(watchType, trk[trkCursor].type, 12); watchType[11] = 0;
  watchRssi = -127; prevWatchRssi = -127;
  watchLastSeen = millis();
  lastWatchDraw = 0;
  startWatchScan();
  renderWatch();
}

void updateWatch(unsigned long now)
{
  if (watchScanEnded) {
    BLEDevice::getScan()->clearResults();
    startWatchScan();
  }
  if (now - lastWatchDraw > 500) { lastWatchDraw = now; renderWatch(); }
}

// Reusable big-carousel item (same look as the main menu) for sub-menus.
void drawCarouselItem(const char *label, uint16_t color)
{
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FM12);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(label, tft.width() / 2, tft.height() / 2);
  drawRadioIcons();
  drawBatteryChip();
}

// ---------------- BLE advertiser ----------------
void writeDefaultAdvCfg()
{
  if (!sd_init) return;
  ensureConfigFolder();
  File f = SD.open("/config/ble_devices.txt", FILE_WRITE);
  if (!f) return;
  f.println("# espSAK - BLE advertiser definitions");
  f.println("# One device per line:  name | type | params | mac(optional)");
  f.println("#   named   | <advertised name>");
  f.println("#   ibeacon | <uuid>,<major>,<minor>");
  f.println("# mac is optional; if omitted it is derived from the name.");
  f.println("# A fixed mac must start C0..FF (BLE static-random address).");
  f.println("# Lines starting with # and blank lines are ignored.");
  f.println();
  f.println("Test Device   | named   | espSAK-BLE                              | C0:00:00:00:00:01");
  f.println("My Wearable   | named   | FitBand-X                               | C0:00:00:00:00:02");
  f.println("Lab Beacon 1  | ibeacon | E2C56DB5-DFFB-48D2-B060-D0F5A71096E0,1,1 | C0:00:00:00:00:03");
  f.println("Lab Beacon 2  | ibeacon | E2C56DB5-DFFB-48D2-B060-D0F5A71096E0,10,5");
  f.println("Desk Beacon   | ibeacon | 8EC76EA3-6668-48DA-9866-75BE8BC86F4D,100,200");
  f.close();
}

// parse "AA:BB:CC:DD:EE:FF" -> out[6]; returns true if 6 valid bytes
bool parseMac(String s, uint8_t out[6])
{
  s.trim();
  if (s.length() == 0) return false;
  int n = 0, start = 0;
  for (int i = 0; i <= (int)s.length() && n < 6; i++) {
    if (i == (int)s.length() || s[i] == ':') {
      String b = s.substring(start, i); b.trim();
      if (b.length() == 0) return false;
      out[n++] = (uint8_t)strtol(b.c_str(), NULL, 16);
      start = i + 1;
    }
  }
  return n == 6;
}

// deterministic static-random mac from a name (so each item is stable + distinct)
void macFromName(const char *s, uint8_t out[6])
{
  uint32_t h1 = 2166136261u, h2 = 0x811c9dc5u;
  for (const char *p = s; *p; p++) { h1 = (h1 ^ (uint8_t)*p) * 16777619u; h2 = h2 * 31 + (uint8_t)*p; }
  out[0] = (h1 >> 24) & 0xFF; out[1] = (h1 >> 16) & 0xFF; out[2] = (h1 >> 8) & 0xFF;
  out[3] = h1 & 0xFF;         out[4] = (h2 >> 8) & 0xFF;  out[5] = h2 & 0xFF;
  out[0] = (out[0] & 0x3F) | 0xC0;   // force the top two bits -> BLE static random
}

void parseAdvLine(String line)
{
  if (advCount >= MAX_ADV) return;
  // split into up to 4 fields on '|'
  String f[4]; int nf = 0, start = 0;
  for (int i = 0; i <= (int)line.length() && nf < 4; i++) {
    if (i == (int)line.length() || line[i] == '|') {
      f[nf] = line.substring(start, i); f[nf].trim(); nf++;
      start = i + 1;
    }
  }
  if (nf < 2 || f[0].length() == 0) return;
  String nm = f[0], ty = f[1], pa = (nf >= 3) ? f[2] : "";
  strncpy(advList[advCount].name,   nm.c_str(), 24); advList[advCount].name[23] = 0;
  strncpy(advList[advCount].type,   ty.c_str(), 10); advList[advCount].type[9]  = 0;
  strncpy(advList[advCount].params, pa.c_str(), 80); advList[advCount].params[79] = 0;
  // mac: 4th field if valid, else derived from the name
  uint8_t m[6];
  if (nf >= 4 && parseMac(f[3], m)) {
    m[0] = (m[0] & 0x3F) | 0xC0;           // ensure static-random
    memcpy(advList[advCount].mac, m, 6);
  } else {
    macFromName(advList[advCount].name, advList[advCount].mac);
  }
  advCount++;
}

void loadAdvList()
{
  advCount = 0;
  if (!sd_init) return;
  ensureConfigFolder();
  if (!SD.exists("/config/ble_devices.txt")) writeDefaultAdvCfg();
  File f = SD.open("/config/ble_devices.txt", FILE_READ);
  if (!f) return;
  while (f.available() && advCount < MAX_ADV) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;
    parseAdvLine(line);
  }
  f.close();
}

uint8_t hexByte(String &s, int pos)
{
  if (pos + 1 >= (int)s.length()) return 0;
  char b[3] = { s[pos], s[pos + 1], 0 };
  return (uint8_t)strtol(b, NULL, 16);
}

// fill advMfr[] with the iBeacon manufacturer-data body; returns its length (25)
int buildIBeaconMfr(int idx)
{
  AdvEntry &e = advList[idx];
  String p = String(e.params);
  int c1 = p.indexOf(','), c2 = p.indexOf(',', c1 + 1);
  String uuid = p.substring(0, c1); uuid.replace("-", ""); uuid.trim();
  uint16_t major = p.substring(c1 + 1, c2).toInt();
  uint16_t minor = p.substring(c2 + 1).toInt();
  int n = 0;
  advMfr[n++] = 0x4C; advMfr[n++] = 0x00;             // Apple company id
  advMfr[n++] = 0x02; advMfr[n++] = 0x15;             // iBeacon type + length 21
  for (int i = 0; i < 16; i++) advMfr[n++] = hexByte(uuid, i * 2);
  advMfr[n++] = (major >> 8) & 0xFF; advMfr[n++] = major & 0xFF;
  advMfr[n++] = (minor >> 8) & 0xFF; advMfr[n++] = minor & 0xFF;
  advMfr[n++] = 0xC5;                                 // measured power (-59 dBm)
  return n;
}

void renderAdvOn()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("Advertising", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  if (advActive >= 0) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(String(advList[advActive].name), 2, 40);
    tft.setTextColor(TFT_GOLD, TFT_BLACK);
    tft.drawString(String(advList[advActive].type), 2, 60);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(advList[advActive].params).substring(0, 28), 2, 80);
    uint8_t *m = advList[advActive].mac;
    sprintf(buff, "mac %02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(String(buff), 2, 98);
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B1-long: stop", 2, 120);
}

// convierte una cadena hex ("1BFF4C00...") en bytes; ignora separadores no-hex
int hexStringToBytes(const String &hex, uint8_t *out, int maxLen)
{
  int n = 0, i = 0, L = hex.length();
  while (i + 1 < L && n < maxLen) {
    char c1 = hex[i], c2 = hex[i + 1];
    if (!isxdigit((unsigned char)c1) || !isxdigit((unsigned char)c2)) { i++; continue; }
    char b[3] = { c1, c2, 0 };
    out[n++] = (uint8_t)strtol(b, NULL, 16);
    i += 2;
  }
  return n;
}
void startAdv(int idx)
{
  AdvEntry &e = advList[idx];
  String type = String(e.type);

  esp_ble_gap_stop_advertising();

  advParams = {};
  advParams.adv_int_min = 0x30;
  advParams.adv_int_max = 0x60;
  advParams.adv_type = ADV_TYPE_IND;
  advParams.own_addr_type = BLE_ADDR_TYPE_RANDOM;
  advParams.channel_map = ADV_CHNL_ALL;
  advParams.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

  esp_err_t r1 = esp_ble_gap_set_rand_addr(e.mac);            // this item's own MAC

  if (type == "raw") {
    // params = paquete AD completo en hex (length+type+payload, ya listo)
    int len = hexStringToBytes(String(e.params), advRaw, sizeof(advRaw));
    if (len <= 0 || len > 31) {
      Serial.printf("[adv] '%s' raw: longitud invalida (%d)\n", e.name, len);
      return;
    }
    esp_err_t r2 = esp_ble_gap_config_adv_data_raw(advRaw, len);
    Serial.printf("[adv] '%s' type=raw len=%d set_rand_addr=%d config_adv_data_raw=%d\n",
                  e.name, len, r1, r2);
  }
  else {
    esp_ble_adv_data_t d = {};
    d.set_scan_rsp = false;
    d.include_txpower = false;
    d.flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);

    if (type == "ibeacon") {
      int len = buildIBeaconMfr(idx);
      d.flag = 0;                                               // sin flags AD -> 3 bytes menos
      d.include_name = false;
      d.manufacturer_len = len;
      d.p_manufacturer_data = advMfr;
    } else {                                                     // named
      String nm = (strlen(e.params) > 0) ? String(e.params) : String(e.name);
      esp_ble_gap_set_device_name(nm.c_str());
      d.include_name = true;
    }
    esp_err_t r2 = esp_ble_gap_config_adv_data(&d);               // callback arranca adv cuando esté listo
    Serial.printf("[adv] '%s' type=%s set_rand_addr=%d config_adv_data=%d\n",
                  e.name, e.type, r1, r2);
  }

  advActive = idx;
  btMode = BTM_ADV_ON;
  renderAdvOn();
}

void stopAdv()
{
  if (advActive >= 0) { esp_ble_gap_stop_advertising(); advActive = -1; }
}

void renderAdvList()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("Advertise", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  if (advCount == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(sd_init ? "no ble_devices.txt" : "SD not mounted", 2, 40);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("B1-long: back", 2, 120);
    return;
  }
  int page = advCursor / 6, start = page * 6, line = 24;
  for (int i = start; i < start + 6 && i < advCount; i++) {
    bool sel = (i == advCursor);
    tft.setTextColor(sel ? TFT_GREEN : TFT_GOLD, TFT_BLACK);
    sprintf(buff, "%s%s (%s)", sel ? ">" : " ", advList[i].name, advList[i].type);
    tft.drawString(String(buff).substring(0, 28), 2, line);
    line += 15;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d/%d B2:mv B1:adv L:back", advCursor + 1, advCount);
  tft.drawString(String(buff), 2, 120);
}

void enterAdvList()
{
  btMode = BTM_ADV_LIST;
  advActive = -1;
  bleAdvInit();                        // private BLE stack (no BLEDevice)
  loadAdvList();
  advCursor = 0;
  renderAdvList();
}

void renderBtMenu()
{
  switch (btMenuPos) {
    case 0: drawCarouselItem("1-Serial console", TFT_CYAN);    break;
    case 1: drawCarouselItem("2-BT discovery",   TFT_GOLD);    break;
    case 2: drawCarouselItem("3-BLE scan",       TFT_GREEN);   break;
    case 3: drawCarouselItem("4-Trackers",       TFT_MAGENTA); break;
    case 4: drawCarouselItem("5-Advertise",      TFT_CYAN);    break;
  }
}

void enterBT()
{
  // BT and WiFi are mutually exclusive
  if (working_mode == "Captive") { server.end(); dnsServer.stop(); working_mode = "Waiting"; }
  WiFi.mode(WIFI_OFF);
  appState = ST_BT;
  btMode = BTM_MENU;
  btMenuPos = 0;
  renderBtMenu();
}

void btOnNext()
{
  switch (btMode) {
    case BTM_MENU:
      btMenuPos = (btMenuPos < BT_MENU_MAX) ? btMenuPos + 1 : 0;
      renderBtMenu();
      break;
    case BTM_DISCOVERY:
      if (!btDiscovering && btCount > 6) {
        int pages = (btCount + 5) / 6;
        btPage = (btPage + 1) % pages;
        renderDiscovery();
      }
      break;
    case BTM_BLESCAN:
      if (!bleScanning && bleCount > 6) {
        int pages = (bleCount + 5) / 6;
        blePage = (blePage + 1) % pages;
        renderBleScan();
      }
      break;
    case BTM_TRACKERS:
      if (!trkScanning && trkCount > 0) {
        trkCursor = (trkCursor + 1) % trkCount;
        renderTrackers();
      }
      break;
    case BTM_ADV_LIST:
      if (advCount > 0) { advCursor = (advCursor + 1) % advCount; renderAdvList(); }
      break;
    case BTM_SPP:
    case BTM_WATCH:
    case BTM_ADV_ON: break;
  }
}

void btOnSelect()
{
  if (btMode == BTM_MENU) {
    if (btMenuPos == 0) enterSPP();
    else if (btMenuPos == 1) enterDiscovery();
    else if (btMenuPos == 2) enterBleScan();
    else if (btMenuPos == 3) enterTrackers();
    else enterAdvList();
  } else if (btMode == BTM_TRACKERS) {
    if (!trkScanning && trkCount > 0) enterWatch();
  } else if (btMode == BTM_ADV_LIST) {
    if (advCount > 0) startAdv(advCursor);
  }
}

void btOnBack()
{
  switch (btMode) {
    case BTM_MENU:
      appState = ST_MENU; drawMenu();
      break;
    case BTM_SPP:
      SerialBT.end();
      btMode = BTM_MENU; renderBtMenu();
      break;
    case BTM_DISCOVERY:
      if (btDiscovering) { SerialBT.discoverAsyncStop(); btDiscovering = false; }
      SerialBT.end();
      btMode = BTM_MENU; renderBtMenu();
      break;
    case BTM_BLESCAN:
      if (bleScanning) { BLEDevice::getScan()->stop(); bleScanning = false; }
      BLEDevice::deinit(false);     // false = keep controller memory so Classic can init again
      btMode = BTM_MENU; renderBtMenu();
      break;
    case BTM_TRACKERS:
      if (trkScanning) { BLEDevice::getScan()->stop(); trkScanning = false; }
      BLEDevice::deinit(false);
      btMode = BTM_MENU; renderBtMenu();
      break;
    case BTM_WATCH:
      BLEDevice::getScan()->stop();
      btMode = BTM_TRACKERS; renderTrackers();   // BLE stays init for the list
      break;
    case BTM_ADV_ON:
      stopAdv();
      btMode = BTM_ADV_LIST; renderAdvList();     // BLE stays init for the list
      break;
    case BTM_ADV_LIST:
      stopAdv();
      bleAdvDeinit();
      btMode = BTM_MENU; renderBtMenu();
      break;
  }
}

void updateBT(unsigned long now)
{
  if (btMode == BTM_SPP) serviceSPP(now);
  else if (btMode == BTM_DISCOVERY) updateBtDiscovery(now);
  else if (btMode == BTM_BLESCAN) updateBleScan(now);
  else if (btMode == BTM_TRACKERS) updateTrackers(now);
  else if (btMode == BTM_WATCH) updateWatch(now);
}

// ==================================================================
//  WEB SERVER (download SD files over a password-protected AP)
// ==================================================================
String webPass = "";

void renderWebSrv()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM9);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("Web server", 2, 2);
  drawRadioIcons();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("SSID: " + websrvSsid, 2, 30);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("Pass: " + webPass, 2, 46);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("http://" + WiFi.softAPIP().toString(), 2, 62);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Open the IP in browser", 2, 82);
  if (!sd_init) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD not mounted!", 2, 98);
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B1-long: stop", 2, 120);
}

void enterWebSrv()
{
  // mutually exclusive with the captive AP (both use the WiFi AP)
  if (working_mode == "Captive") { server.end(); dnsServer.stop(); working_mode = "Waiting"; }
  appState = ST_WEBSRV;

  // fixed password from config if valid (>=8), else a random 8-digit one
  if (websrvPass.length() >= 8) webPass = websrvPass;
  else { webPass = ""; for (int i = 0; i < 8; i++) webPass += String((char)('0' + esp_random() % 10)); }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(websrvSsid.c_str(), webPass.c_str());   // WPA2 (password >= 8 chars)
  { IPAddress ip; ip.fromString(websrvIp); WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0)); }
  delay(100);
  server.begin();
  Serial.println("Web server AP up, pass=" + webPass);
  renderWebSrv();
}

void webSend404(WiFiClient &client)
{
  client.println("HTTP/1.1 404 Not Found");
  client.println("Connection: close");
  client.println();
  client.println("Not found");
}

void webSendIndex(WiFiClient &client, String dir)
{
  if (dir == "") dir = "/";
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.print("<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>");
  client.print("<title>espSAK files</title></head><body style='font-family:sans-serif'>");
  client.print("<h2>espSAK</h2><p><b>");
  client.print(dir);
  client.print("</b></p>");
  if (!sd_init) {
    client.print("<p>SD not mounted</p>");
  } else {
    client.print("<ul>");
    if (dir != "/") {
      client.print("<li><a href=\"/?p=");
      client.print(parentPath(dir));
      client.print("\">../</a></li>");
    }
    File d = SD.open(dir);
    if (d) {
      d.rewindDirectory();
      while (true) {
        File f = d.openNextFile();
        if (!f) break;
        String nm = baseName(f.name());
        String full = joinPath(dir, nm);
        if (f.isDirectory()) {
          client.print("<li><a href=\"/?p=");
          client.print(full); client.print("\">");
          client.print(nm); client.print("/</a></li>");
        } else {
          client.print("<li><a href=\"/get?p=");
          client.print(full); client.print("\">");
          client.print(nm); client.print("</a> (");
          client.print((uint32_t)f.size());
          client.print(" B) <a href=\"/del?p=");
          client.print(full); client.print("\" style='color:red'>[X]</a></li>");
        }
        f.close();
      }
      d.close();
    }
    client.print("</ul>");
  }
  client.print("</body></html>");
}

void webSendFile(WiFiClient &client, String path)
{
  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) { webSend404(client); if (f) f.close(); return; }
  String name = baseName(path.c_str());
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/octet-stream");
  client.print("Content-Length: "); client.println((uint32_t)f.size());
  client.print("Content-Disposition: attachment; filename=\""); client.print(name); client.println("\"");
  client.println("Connection: close");
  client.println();
  uint8_t fb[512];
  while (f.available()) {
    int n = f.read(fb, 512);
    if (n <= 0) break;
    client.write(fb, n);
  }
  f.close();
}

void webDelete(WiFiClient &client, String path)
{
  SD.remove(path);
  client.println("HTTP/1.1 303 See Other");
  client.print("Location: /?p="); client.println(parentPath(path));
  client.println("Connection: close");
  client.println();
}

// extract a query parameter value from a "path?a=1&b=2" target
String webQ(String target, String key)
{
  int q = target.indexOf('?');
  if (q < 0) return "";
  String qs = target.substring(q + 1);
  String want = key + "=";
  int k = qs.indexOf(want);
  if (k < 0) return "";
  int v = k + want.length();
  int amp = qs.indexOf('&', v);
  return (amp < 0) ? qs.substring(v) : qs.substring(v, amp);
}

void serviceWebSrv()
{
  WiFiClient client = server.available();
  if (!client) return;

  String reqLine = "", currentLine = "";
  unsigned long t0 = millis();
  while (client.connected() && millis() - t0 < 2000) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') {
        if (currentLine.length() == 0) break;      // blank line = end of headers
        if (reqLine == "") reqLine = currentLine;  // keep the request line
        currentLine = "";
      } else if (c != '\r') currentLine += c;
    }
  }

  String target = "/";
  int s = reqLine.indexOf(' ');
  int e = reqLine.indexOf(' ', s + 1);
  if (s >= 0 && e > s) target = reqLine.substring(s + 1, e);

  int q = target.indexOf('?');
  String base = (q < 0) ? target : target.substring(0, q);
  String p = webQ(target, "p");
  if (p == "") p = "/";

  if (base == "/get")      webSendFile(client, p);
  else if (base == "/del") webDelete(client, p);
  else                     webSendIndex(client, p);   // "/" and browse links

  client.stop();
}

void updateWebSrv(unsigned long now) { serviceWebSrv(); }

// ------------------------------------------------------------------
//  Captive (background service)
// ------------------------------------------------------------------
void startCaptive()
{
  captive_count = 0; capLogCount = 0;
  for (int i = 0; i < CAP_LOG_N; i++) capLog[i] = "";
  IPAddress ip; ip.fromString(captiveIp);
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(captiveSsid.c_str(), "");
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  myIP = WiFi.softAPIP();
  server.begin();
  dnsServer.start(53, "*", myIP);
  working_mode = "Captive";
  Serial.println("Captive AP up: " + myIP.toString());
}

void stopCaptive()
{
  server.end();
  dnsServer.stop();
  WiFi.mode(WIFI_OFF);
  working_mode = "Waiting";
}

// brief modal, then back to the main menu
void captiveModal(const char *msg, bool withIp)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  if (withIp) {
    tft.drawString(msg, tft.width() / 2, -12 + tft.height() / 2);
    tft.drawString(myIP.toString(), tft.width() / 2, 12 + tft.height() / 2);
  } else {
    tft.drawString(msg, tft.width() / 2, tft.height() / 2);
  }
  delay(1200);
  appState = ST_MENU; menu_pos = 2;
  drawMenu();
}

void capLogAdd(String s)
{
  capLog[capLogCount % CAP_LOG_N] = s.substring(0, 80);
  capLogCount++;
}

// ---- captive sub-menu (carousel) ----
void renderCaptiveMenu()
{
  switch (capMenuPos) {
    case 0: drawCarouselItem("1-Start",  TFT_GREEN);   break;
    case 1: drawCarouselItem("2-Stop",   TFT_RED);     break;
    case 2: drawCarouselItem("3-Status", TFT_GOLD);    break;
    case 3: drawCarouselItem("4-Portal", TFT_MAGENTA); break;
  }
}

// build the list: slot 0 = default (internal), then every /config/*.html
void loadHtmlList()
{
  htmlCount = 0; htmlCursor = 0;
  htmlList[htmlCount++] = "";                 // 0 = internal default
  if (sd_init) {
    File d = SD.open("/config");
    if (d) {
      d.rewindDirectory();
      while (htmlCount < MAX_HTML) {
        File e = d.openNextFile();
        if (!e) break;
        String nm = baseName(e.name());
        if (!e.isDirectory() && nm.endsWith(".html")) htmlList[htmlCount++] = "/config/" + nm;
        e.close();
      }
      d.close();
    }
  }
  for (int i = 0; i < htmlCount; i++) if (htmlList[i] == captiveHtmlPath) { htmlCursor = i; break; }
}

void renderPortalList()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("Portal", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  int page = htmlCursor / 6, start = page * 6, line = 24;
  for (int i = start; i < start + 6 && i < htmlCount; i++) {
    bool sel = (i == htmlCursor);
    bool active = (htmlList[i] == captiveHtmlPath);
    tft.setTextColor(sel ? TFT_GREEN : TFT_GOLD, TFT_BLACK);
    String nm = (i == 0) ? "Default (internal)" : baseName(htmlList[i].c_str());
    String s = String(sel ? ">" : " ") + nm + (active ? " *" : "");
    tft.drawString(s.substring(0, 28), 2, line);
    line += 15;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  sprintf(buff, "%d/%d B2:mv B1:use L:back", htmlCursor + 1, htmlCount);
  tft.drawString(String(buff), 2, 120);
}

void renderCaptiveStatus()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FM12);
  tft.setTextColor(TFT_GOLD, TFT_BLACK);
  tft.drawString("Captive", 2, 2);
  drawRadioIcons();
  tft.setFreeFont(FM9);
  bool on = (working_mode == "Captive");
  tft.setTextColor(on ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(on ? ("ON  " + WiFi.softAPIP().toString()) : "OFF", 2, 26);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("clients: " + String(captive_count), 2, 42);

  int shown  = (capLogCount < CAP_LOG_N) ? capLogCount : CAP_LOG_N;
  int oldest = (capLogCount < CAP_LOG_N) ? 0 : (capLogCount % CAP_LOG_N);
  int y = 60;
  if (shown == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("(no requests yet)", 2, y);
  } else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    for (int i = 0; i < shown; i++) {
      String s = capLog[(oldest + i) % CAP_LOG_N];
      tft.drawString(s.substring(0, 30), 2, y);
      y += 13;
    }
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("B1-long: back", 2, 120);
}

void enterCaptive()
{
  appState = ST_CAPTIVE;
  capMode = CAPM_MENU;
  capMenuPos = 0;
  renderCaptiveMenu();
}

void updateCaptive(unsigned long now)
{
  static unsigned long last = 0;
  if (capMode == CAPM_STATUS && now - last > 1000) { last = now; renderCaptiveStatus(); }
}

void capOnNext()
{
  if (capMode == CAPM_MENU) {
    capMenuPos = (capMenuPos < CAP_MENU_MAX) ? capMenuPos + 1 : 0;
    renderCaptiveMenu();
  } else if (capMode == CAPM_PORTAL) {
    if (htmlCount > 0) { htmlCursor = (htmlCursor + 1) % htmlCount; renderPortalList(); }
  }
}

void capOnSelect()
{
  if (capMode == CAPM_PORTAL) {             // choose the active portal
    captiveHtmlPath = htmlList[htmlCursor];
    writeConfig();                          // persist selection
    renderPortalList();                     // the '*' moves to the chosen entry
    return;
  }
  if (capMode != CAPM_MENU) return;
  if (capMenuPos == 0) {                    // Start
    if (working_mode != "Captive") startCaptive();
    captiveModal("Captive started", true);
  } else if (capMenuPos == 1) {             // Stop
    if (working_mode == "Captive") stopCaptive();
    captiveModal("Captive stopped", false);
  } else if (capMenuPos == 2) {             // Status
    capMode = CAPM_STATUS;
    renderCaptiveStatus();
  } else {                                  // Portal
    loadHtmlList();
    capMode = CAPM_PORTAL;
    renderPortalList();
  }
}

void capOnBack()
{
  if (capMode == CAPM_STATUS || capMode == CAPM_PORTAL) { capMode = CAPM_MENU; renderCaptiveMenu(); }
  else { appState = ST_MENU; drawMenu(); }
}

void serviceCaptive()
{
  dnsServer.processNextRequest();
  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("New captive client");
  captive_count++;
  drawRadioIcons();                 // flip the wifi icon to red (detection) - no screen wipe

  myFile = SD.open(captive_fname, FILE_APPEND);
  if (myFile) {
    myFile.println(String(captive_count) + "-----espSAK captive V" + version + " @ " + fmtUptime() + " -----");
    myFile.flush();
  } else Serial.println("error opening " + captive_fname);

  String currentLine = "";
  unsigned long t0 = millis();
  while (client.connected() && millis() - t0 < 3000) {   // 3s idle timeout -> always closes & flushes
    if (client.available()) {
      t0 = millis();
      char c = client.read();
      if (c == '\n') {
        if (currentLine.length() == 0) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println();
          streamCaptiveHtml(client);
          client.println();
          break;
        } else {
          if (myFile) myFile.println(currentLine);
          if (currentLine.startsWith("GET ")) capLogAdd(currentLine);   // for the status screen
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
      }
    }
  }
  client.stop();
  if (myFile) myFile.close();
  if (appState == ST_CAPTIVE && capMode == CAPM_STATUS) renderCaptiveStatus();
}

// ------------------------------------------------------------------
//  Standby
// ------------------------------------------------------------------
void enterStandby()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setFreeFont(FSS9);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Entering standby mode...", tft.width() / 2, -14 + tft.height() / 2);
  tft.drawString("Press again to wake up..", tft.width() / 2, 14 + tft.height() / 2);
  espDelay(3000);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
  delay(200);
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------
//  Navigation
// ------------------------------------------------------------------
void enterCurrentMenuItem()
{
  switch (menu_pos) {
    case 1: enterInfo();     break;
    case 2: enterCaptive();  break;
    case 3: enterWifiTools(); break;
    case 4: enterBT();       break;
    case 5: enterSDCard();   break;
    case 6: enterStandby();  break;
    default: break;
  }
}

void onNext()
{
  if (appState == ST_SDCARD)  { sdOnNext(); return; }
  if (appState == ST_BT)      { btOnNext(); return; }
  if (appState == ST_WIFI)    { wfOnNext(); return; }
  if (appState == ST_CAPTIVE) { capOnNext(); return; }
  if (appState == ST_INFO) {                 // B2 cycles screen brightness
    brightnessLevel = (brightnessLevel + 1) % 4;
    applyBrightness();
    writeConfig();                           // persist brightness
    lastInfo = 0;                            // force immediate redraw
    return;
  }
  switch (appState) {
    case ST_MENU:
      menu_pos = (menu_pos < max_menu) ? menu_pos + 1 : 0;
      drawMenu();
      break;
    default: break;
  }
}

void onSelect()
{
  if (appState == ST_SDCARD)  { sdOnSelect(); return; }
  if (appState == ST_BT)      { btOnSelect(); return; }
  if (appState == ST_WIFI)    { wfOnSelect(); return; }
  if (appState == ST_CAPTIVE) { capOnSelect(); return; }
  if (appState == ST_MENU) enterCurrentMenuItem();
}

void onBack()
{
  if (appState == ST_SDCARD)  { sdOnBack(); return; }
  if (appState == ST_BT)      { btOnBack(); return; }
  if (appState == ST_WIFI)    { wfOnBack(); return; }
  if (appState == ST_CAPTIVE) { capOnBack(); return; }
  switch (appState) {
    case ST_MENU:
      menu_pos = 0; drawMenu();
      break;
    case ST_WEBSRV:
      server.end();
      WiFi.mode(WIFI_OFF);
      appState = ST_SDCARD; sdMode = SDM_MENU; renderSdMenu();
      break;
    default:
      appState = ST_MENU; drawMenu();
      break;
  }
}

void handleIntents()
{
  if (intentNext || intentSelect || intentBack) lastActivity = millis();
  if (intentNext)   { intentNext = false;   onNext();   }
  if (intentSelect) { intentSelect = false; onSelect(); }
  if (intentBack)   { intentBack = false;   onBack();   }
}

void button_init()
{
  btn1.setClickHandler([](Button2 & b)     { intentSelect = true; });
  btn1.setLongClickHandler([](Button2 & b) { intentBack   = true; });
  btn2.setClickHandler([](Button2 & b)     { intentNext   = true; });
}

void button_loop()
{
  btn1.loop();
  btn2.loop();
}

// ------------------------------------------------------------------
//  Setup
// ------------------------------------------------------------------
void setup()
{


  Serial.begin(115200);
  Serial.println();
  Serial.println("#######################################");
  Serial.println("Initializing espSAK system...");
  Serial.println("#######################################");
  Serial.printf("ESP32 Chip model = %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("This chip has %d cores\n", ESP.getChipCores());

  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);

  button_init();

  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    Serial.printf("eFuse Vref:%u mV", adc_chars.vref);
    vref = adc_chars.vref;
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
    Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
  } else {
    Serial.println("Default Vref: 1100mV");
  }
  Serial.println();

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 240, 135, espsak_logo);

  if (!initSD()) {
    Serial.println("Card Mount Failed");
    sd_init = false;
  } else {
    sd_init = true;              // must be set before loadConfig (it guards on sd_init)
    prepareSdFiles();
    loadConfig();
    Serial.println(captive_fname);
    Serial.println(wifi_fname);
    Serial.println(probe_fname);
    Serial.println(bt_fname);
    Serial.println(track_fname);
  }

  // backlight PWM for brightness control
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
#endif
  applyConfig();                    // brightness + long-press time + idle timeout
  lastActivity = millis();

  sampleBattery(0);
  appState = ST_MENU;
  menu_pos = 0;
  drawMenu();
}

// ------------------------------------------------------------------
//  Loop (non-blocking)
// ------------------------------------------------------------------
void loop()
{
  unsigned long now = millis();

  sampleBattery(now);

  if (working_mode == "Captive") serviceCaptive();

  handleIntents();

  switch (appState) {
    case ST_MENU:     updateMenuBattery(now); break;
    case ST_INFO:     updateInfo(now);        break;
    case ST_WIFI:     updateWifi(now);        break;
    case ST_BT:       updateBT(now);          break;
    case ST_WEBSRV:   updateWebSrv(now);      break;
    case ST_CAPTIVE:  updateCaptive(now);     break;
    default: break;
  }

  // auto-standby when idle on a passive screen (never mid-scan or mid-service)
  if ((now - lastActivity > idleTimeout) && working_mode != "Captive" &&
      (appState == ST_MENU || appState == ST_INFO || appState == ST_SDCARD)) {
    enterStandby();
  }

  button_loop();
}
