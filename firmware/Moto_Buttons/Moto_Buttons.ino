#include <bluefruit.h>
#include <Arduino.h>
#include "src/MyBfButton.h"
#include "src/MyBfButtonManager.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#define FW_VERSION "test_1.0.0"

// ==================== KONFIGURACE / COMPILE FLAGS ====================
// TEST_MODE: 1 = vývoj/testování, 0 = produkce.
//   Zapíná sériovou konzoli a zrcadlení stavu na built-in LED a používá
//   "Test" jméno zařízení. V produkci to vše vypne.
#define TEST_MODE 1

// USE_ANALOG: 1 = číst 5-směrný joystick (voltage divider na analog pinu),
//   0 = vůbec nepoužívat. Na cílovém HW joystick není a plovoucí pin jinak
//   občas generuje falešné stisky -> tehdy nech 0.
#define USE_ANALOG 0

// --- Odvozené přepínače (běžně neměnit) ---
#define MIRROR_BUILTIN_LED TEST_MODE   // built-in LED jen při testování
#define ENABLE_SERIAL      TEST_MODE   // sériová konzole jen při testování

// --- Debug makra: v produkci se přeloží do prázdna (žádný Serial v buildu) ---
#if ENABLE_SERIAL
  #define DBG_BEGIN(baud)  Serial.begin(baud)
  #define DBG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DBG_FLUSH()      Serial.flush()
#else
  #define DBG_BEGIN(baud)
  #define DBG_PRINT(...)
  #define DBG_PRINTLN(...)
  #define DBG_FLUSH()
#endif
// ====================================================================

using namespace Adafruit_LittleFS_Namespace;
const char* CONFIG_FILENAME   = "/keypad_cfg.dat";
const char* SETTINGS_FILENAME = "/keypad_set.dat";

// --- Piny tlačítek (hodnota = číslo pinu, které vrací getID()) ---
const unsigned int D1btnPin = 2;   // Dbtn1
const unsigned int D2btnPin = 1;   // Dbtn2
const unsigned int D3btnPin = 0;   // Dbtn3

// --- Stavová LED ---
// Na cílovém HW není vidět built-in RGB LED, používá se externí modrá LED.
// Je připojená na D3 nebo D4 -> budíme obě stejně, aby to fungovalo bez ohledu
// na to, do kterého pinu je reálně zapojená.
const uint8_t STATUS_LED_A = D3;
const uint8_t STATUS_LED_B = D4;
#define LED_ON  HIGH    // pokud je LED zapojená jako active-LOW, prohoď na LOW
#define LED_OFF LOW

// Built-in LED svítí v LOW (opačná polarita než D3/D4), budíme ji invertovaně.
const uint8_t BUILTIN_STATUS_LED = LED_BLUE;   // nebo LED_BUILTIN / LED_RED / LED_GREEN

bool isConnected = false;
bool ledState = false;
unsigned long previousMillis = 0;
const long blinkInterval = 500;   // pomalé blikání při odpojení

// --- Soft reset (čistý restart MCU) ---
// Combo = oba krajní knoflíky (Dbtn1 + Dbtn3) podržené po dobu RESET_HOLD_MS.
const unsigned long RESET_HOLD_MS = 3000;
unsigned long resetComboStart = 0;
bool resetComboActive = false;

// --- Digitální tlačítka ---
MyBfButton Dbtn1(MyBfButton::STANDALONE_DIGITAL, D1btnPin, true, LOW, HID_KEY_EQUAL, HID_KEY_NONE, HID_KEY_ARROW_UP);
MyBfButton Dbtn2(MyBfButton::STANDALONE_DIGITAL, D2btnPin, true, LOW, HID_KEY_C,     HID_KEY_NONE, HID_KEY_NONE);
MyBfButton Dbtn3(MyBfButton::STANDALONE_DIGITAL, D3btnPin, true, LOW, HID_KEY_MINUS, HID_KEY_NONE, HID_KEY_ARROW_DOWN);

// --- Analogový 5-směrný joystick (jen když USE_ANALOG) ---
#if USE_ANALOG
const unsigned int AbtnPin = 5;
MyBfButtonManager manager(AbtnPin, 5);
// Směry mají pevné klávesy (nejsou přemapovatelné z appky).
MyBfButton Abtn1(MyBfButton::ANALOG_BUTTON_ARRAY, 0, true, LOW, HID_KEY_ARROW_DOWN,  HID_KEY_NONE, HID_KEY_NONE);
MyBfButton Abtn2(MyBfButton::ANALOG_BUTTON_ARRAY, 1, true, LOW, HID_KEY_ARROW_LEFT,  HID_KEY_NONE, HID_KEY_NONE);
MyBfButton Abtn3(MyBfButton::ANALOG_BUTTON_ARRAY, 2, true, LOW, HID_KEY_ARROW_RIGHT, HID_KEY_NONE, HID_KEY_NONE);
MyBfButton Abtn4(MyBfButton::ANALOG_BUTTON_ARRAY, 3, true, LOW, HID_KEY_ARROW_UP,    HID_KEY_NONE, HID_KEY_NONE);
MyBfButton Abtn5(MyBfButton::ANALOG_BUTTON_ARRAY, 4, true, LOW, HID_KEY_C,           HID_KEY_NONE, HID_KEY_NONE);
#endif

// --- BLE init ---
BLEDis bledis;
BLEHidAdafruit blehid;
BLEDfu bledfu;
BLEService        configService("12345678-1234-5678-1234-56789abcdef0");
BLECharacteristic configCharacteristic("12345678-1234-5678-1234-56789abcdef1");   // mapování kláves (9 B)
BLECharacteristic settingsCharacteristic("12345678-1234-5678-1234-56789abcdef2"); // nastavení (viz SETTINGS_LEN)

// 9 vlastních kláves v paměti: pro každé tlačítko { single, double, long }.
// POZOR: handler indexuje pole jako customKeys[getID()*3] a getID() vrací
// ČÍSLO PINU (ne pořadí konstrukce). Proto je skutečné mapování:
//   sloty 0-2 -> pin 0 = Dbtn3
//   sloty 3-5 -> pin 1 = Dbtn2
//   sloty 6-8 -> pin 2 = Dbtn1
// Pořadí bytů z Android appky musí tomuhle odpovídat (data[0] -> Dbtn3).
uint8_t customKeys[9] = {
  HID_KEY_EQUAL, HID_KEY_NONE, HID_KEY_ARROW_UP,    // Dbtn3 (pin 0): single, double, long
  HID_KEY_C,     HID_KEY_NONE, HID_KEY_NONE,        // Dbtn2 (pin 1): single, double, long
  HID_KEY_MINUS, HID_KEY_NONE, HID_KEY_ARROW_DOWN   // Dbtn1 (pin 2): single, double, long
};

// --- Nastavení konfigurovatelné z Android appky (persistované do flash) ---
// Zápisem do settingsCharacteristic (UUID ...def2). Délka musí být SETTINGS_LEN.
#define SETTINGS_LEN 1
#define SET_RESTART_ON_DISCONNECT 0   // index v poli settings[]
uint8_t settings[SETTINGS_LEN] = {
  0   // [0] restartOnDisconnect: 0 = false (výchozí), 1 = true
};

// --- Prototypy ---
void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);
void config_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);
void settings_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);
void startAdv(void);
void sendKey(uint8_t keycode);
void ledWrite(bool on);
void bootBlink(void);
void updateStatusLed(void);
bool handleResetCombo(void);
void saveSettings(void);
void loadSettings(void);
void applySettings(void);
const char* btnName(uint8_t id);

// ---- LED helpery ----
void ledWrite(bool on) {
  digitalWrite(STATUS_LED_A, on ? LED_ON : LED_OFF);
  digitalWrite(STATUS_LED_B, on ? LED_ON : LED_OFF);
#if MIRROR_BUILTIN_LED
  digitalWrite(BUILTIN_STATUS_LED, on ? LOW : HIGH);   // built-in je active-LOW
#endif
}

// Krátké trojí bliknutí = viditelné potvrzení, že firmware (znovu) naběhl.
// Slouží zároveň jako potvrzení proběhlého resetu.
void bootBlink(void) {
  for (int i = 0; i < 3; i++) {
    ledWrite(true);  delay(60);
    ledWrite(false); delay(120);
  }
}

// Stavová logika LED:
//   připojeno   -> trvale svítí
//   odpojeno    -> pomalu bliká (advertising)
//   reset combo -> řeší handleResetCombo() (zrychlující se blikání)
void updateStatusLed(void) {
  if (isConnected) {
    ledWrite(true);
    return;
  }
  unsigned long now = millis();
  if (now - previousMillis >= blinkInterval) {
    previousMillis = now;
    ledState = !ledState;
    ledWrite(ledState);
  }
}

// Pošle jednu klávesu (přeskočí prázdné NONE sloty)
void sendKey(uint8_t keycode) {
  if (keycode == HID_KEY_NONE) return;
  uint8_t report[6] = { keycode, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE };
  blehid.keyboardReport(0, report);
  delay(10);
  blehid.keyRelease();
}

// Jméno tlačítka podle getID() (= číslo pinu). Použito jen pro serial debug.
const char* btnName(uint8_t id) {
  switch (id) {
    case 2:  return "Dbtn1";
    case 1:  return "Dbtn2";
    case 0:  return "Dbtn3";
    default: return "Dbtn?";
  }
}

void DpressHandler(MyBfButton *btn, MyBfButton::press_pattern_t pattern) {
  uint8_t id = btn->getID();
  int offset = id * 3;   // viz poznámka u customKeys[]

  switch (pattern) {
    case MyBfButton::SINGLE_PRESS:
      DBG_PRINT(btnName(id)); DBG_PRINT(" SHORT  -> key 0x"); DBG_PRINTLN(customKeys[offset], HEX);
      sendKey(customKeys[offset]);
      break;

    case MyBfButton::DOUBLE_PRESS:
      DBG_PRINT(btnName(id)); DBG_PRINT(" DOUBLE -> key 0x"); DBG_PRINTLN(customKeys[offset + 1], HEX);
      sendKey(customKeys[offset + 1]);
      break;

    case MyBfButton::LONG_PRESS:
      if (!Bluefruit.connected()) {
        DBG_PRINT(btnName(id)); DBG_PRINTLN(" LONG   -> restart advertising");
        Bluefruit.Advertising.start(0);
      } else {
        DBG_PRINT(btnName(id)); DBG_PRINT(" LONG   -> key 0x"); DBG_PRINTLN(customKeys[offset + 2], HEX);
        sendKey(customKeys[offset + 2]);
      }
      break;
  }
}

#if USE_ANALOG
// Joystick posílá jen single press (pevně dané klávesy z konstruktoru).
void ApressHandler(MyBfButton *btn, MyBfButton::press_pattern_t pattern) {
  if (pattern == MyBfButton::SINGLE_PRESS) {
    DBG_PRINT("Analog dir id "); DBG_PRINT(btn->getID());
    DBG_PRINT(" -> key 0x");     DBG_PRINTLN(btn->getKeyCodeSingle(), HEX);
    sendKey(btn->getKeyCodeSingle());
  }
}
#endif

void saveConfig() {
  DBG_PRINTLN("Ukladam config do flash...");
  InternalFS.remove(CONFIG_FILENAME);   // InternalFS vyžaduje smazat soubor před zápisem

  File file = InternalFS.open(CONFIG_FILENAME, FILE_O_WRITE);
  if (file) {
    file.write(customKeys, sizeof(customKeys));
    file.close();
    DBG_PRINTLN("Config ulozen.");
  } else {
    DBG_PRINTLN("Nepodarilo se otevrit soubor pro zapis.");
  }
}

void loadConfig() {
  DBG_PRINTLN("Nacitam config z flash...");

  if (InternalFS.exists(CONFIG_FILENAME)) {
    File file = InternalFS.open(CONFIG_FILENAME, FILE_O_READ);
    if (file) {
      int bytesRead = file.read(customKeys, sizeof(customKeys));
      file.close();

      if (bytesRead == sizeof(customKeys)) {
        DBG_PRINTLN("Config nacten.");
      } else {
        DBG_PRINTLN("Nesouhlasi velikost configu!");
      }
    } else {
      DBG_PRINTLN("Nepodarilo se otevrit soubor pro cteni.");
    }
  } else {
    DBG_PRINTLN("Zadny config nenalezen, pouzivam vychozi hodnoty.");
  }
}

// --- Nastavení (settings) - stejný princip jako config ---
void saveSettings() {
  DBG_PRINTLN("Ukladam settings do flash...");
  InternalFS.remove(SETTINGS_FILENAME);

  File file = InternalFS.open(SETTINGS_FILENAME, FILE_O_WRITE);
  if (file) {
    file.write(settings, sizeof(settings));
    file.close();
    DBG_PRINTLN("Settings ulozeny.");
  } else {
    DBG_PRINTLN("Settings: zapis selhal.");
  }
}

void loadSettings() {
  DBG_PRINTLN("Nacitam settings z flash...");

  if (InternalFS.exists(SETTINGS_FILENAME)) {
    File file = InternalFS.open(SETTINGS_FILENAME, FILE_O_READ);
    if (file) {
      int bytesRead = file.read(settings, sizeof(settings));
      file.close();

      if (bytesRead == sizeof(settings)) {
        DBG_PRINTLN("Settings nacteny.");
      } else {
        DBG_PRINTLN("Nesouhlasi velikost settings!");
      }
    } else {
      DBG_PRINTLN("Settings: cteni selhalo.");
    }
  } else {
    DBG_PRINTLN("Zadne settings, pouzivam vychozi hodnoty.");
  }
}

// Aplikuje aktuální settings[] na chování zařízení (lze volat za běhu).
void applySettings() {
  bool restart = (settings[SET_RESTART_ON_DISCONNECT] != 0);
  Bluefruit.Advertising.restartOnDisconnect(restart);
  DBG_PRINT("restartOnDisconnect = "); DBG_PRINTLN(restart);
}

void setup() {
  DBG_BEGIN(115200);
  DBG_PRINTLN("Starting...");

  // Inicializace flash a načtení configu + nastavení
  InternalFS.begin();
  loadConfig();
  loadSettings();

  // --- Stavová LED ---
  pinMode(STATUS_LED_A, OUTPUT);
  pinMode(STATUS_LED_B, OUTPUT);
#if MIRROR_BUILTIN_LED
  pinMode(BUILTIN_STATUS_LED, OUTPUT);
#endif
  ledWrite(false);

  // Piny digitálních tlačítek nastavuje knihovna (INPUT_PULLUP, protože
  // pullup=true), takže přímé digitalRead() v reset combu je spolehlivé.
  Dbtn1.onPress(DpressHandler); Dbtn1.onDoublePress(DpressHandler); Dbtn1.onPressFor(DpressHandler, 1000);
  Dbtn2.onPress(DpressHandler); Dbtn2.onDoublePress(DpressHandler); Dbtn2.onPressFor(DpressHandler, 1000);
  Dbtn3.onPress(DpressHandler); Dbtn3.onDoublePress(DpressHandler); Dbtn3.onPressFor(DpressHandler, 1000);

#if USE_ANALOG
  // Joystick: jen onPress (snappy odezva; double/long stejně nevyužíváme).
  manager.setADCResolution(1024);
  Abtn1.onPress(ApressHandler); manager.addButton(&Abtn1, 140, 165);
  Abtn2.onPress(ApressHandler); manager.addButton(&Abtn2, 280, 330);
  Abtn3.onPress(ApressHandler); manager.addButton(&Abtn3, 450, 480);
  Abtn4.onPress(ApressHandler); manager.addButton(&Abtn4, 600, 640);
  Abtn5.onPress(ApressHandler); manager.addButton(&Abtn5, 750, 800);
  manager.begin();
#endif

  // --- BLE start ---
  Bluefruit.begin();
  //Bluefruit.Security.setPIN("123456");
  //Bluefruit.Security.setMITM(true);

  configService.begin();

  // Charakteristika pro mapování kláves (9 bytů)
  configCharacteristic.setProperties(CHR_PROPS_WRITE);
  configCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  configCharacteristic.setFixedLen(9);   // 3 tlačítka * 3 události = 9 bytů
  configCharacteristic.setWriteCallback(config_write_callback);
  configCharacteristic.begin();

  // Charakteristika pro nastavení (SETTINGS_LEN bytů)
  settingsCharacteristic.setProperties(CHR_PROPS_WRITE);
  settingsCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  settingsCharacteristic.setFixedLen(SETTINGS_LEN);
  settingsCharacteristic.setWriteCallback(settings_write_callback);
  settingsCharacteristic.begin();

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  Bluefruit.setTxPower(8);
#if TEST_MODE
  Bluefruit.setName("Test Osmand Keyboard");
#else
  Bluefruit.setName("Osmand Keyboard");
#endif

  bledis.setManufacturer("BuuCzech Development");
  bledis.setModel("MotoButtons (XIAO nRF52840) fw " FW_VERSION);
  DBG_PRINT("Firmware "); DBG_PRINTLN(FW_VERSION);
  bledis.begin();
  blehid.begin();
  bledfu.begin();   // OTA - potřeba i v produkci

  // --- Bonding podle verze knihovny ---
  #if ARDUINO_NRF52_ADAFRUIT_VERSION_MAJOR >= 1
    BluefruitBonds.begin();
  #endif

  startAdv();

  bootBlink();   // viditelné potvrzení (re)startu firmwaru
}

void startAdv(void) {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);

  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addService(bledfu);
  Bluefruit.Advertising.addService(configService);
  Bluefruit.Advertising.addName();

  // restartOnDisconnect je nastavitelný z Android appky (settings[0]).
  // false = po odpojení se advertising NEspustí sám (znovu až dlouhým stiskem);
  // true  = zařízení se samo zase nabízí k připojení.
  applySettings();

  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// --- Callbacky ---
void connect_callback(uint16_t conn_handle) {
  (void) conn_handle;
  isConnected = true;
  ledWrite(true);
  DBG_PRINTLN("Pripojeno");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;
  isConnected = false;
  previousMillis = 0;   // ať blikání naběhne hned
  DBG_PRINTLN("Odpojeno");
}

void config_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len == 9) {
    DBG_PRINTLN("Prijata nova konfigurace klaves z Androidu!");
    for (int i = 0; i < 9; i++) {
      customKeys[i] = data[i];
    }
    saveConfig();
  }
}

void settings_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len == SETTINGS_LEN) {
    DBG_PRINTLN("Prijata nova nastaveni z Androidu!");
    for (int i = 0; i < SETTINGS_LEN; i++) {
      settings[i] = data[i];
    }
    saveSettings();
    applySettings();   // aplikovat hned za běhu (není nutný restart)
  }
}

// --- Soft reset combo ---
// Vrací true, pokud combo právě probíhá (pak loop přeskočí normální čtení
// tlačítek, aby se během reset gesta neodeslaly žádné klávesy).
bool handleResetCombo(void) {
  // tlačítka jsou aktivní v LOW
  bool comboHeld = (digitalRead(D1btnPin) == LOW) && (digitalRead(D3btnPin) == LOW);

  if (!comboHeld) {
    if (resetComboActive) {
      resetComboActive = false;
      ledWrite(false);   // zhasnout indikátor (stavová logika ho hned přepíše)
    }
    return false;
  }

  if (!resetComboActive) {
    resetComboActive = true;
    resetComboStart = millis();
  }

  unsigned long held = millis() - resetComboStart;

  if (held >= RESET_HOLD_MS) {
    DBG_PRINTLN("Reset combo - restartuji...");
    DBG_FLUSH();
    for (int i = 0; i < 10; i++) {   // rychlý záblesk = potvrzení
      ledWrite(i % 2 == 0);
      delay(50);
    }
    ledWrite(false);
    NVIC_SystemReset();   // čistý restart MCU - sem se už nevrátí
  }

  // Indikátor: blikání se zrychluje, jak se blíží reset
  unsigned long blinkRate = map(held, 0, RESET_HOLD_MS, 200, 40);
  ledWrite((millis() / blinkRate) % 2 == 0);
  return true;
}

void loop() {
  // Reset combo má přednost: pokud probíhá, neřeš nic dalšího
  if (handleResetCombo()) {
    return;
  }

  Dbtn1.read();
  Dbtn2.read();
  Dbtn3.read();

#if USE_ANALOG
  manager.loop();
#endif

  updateStatusLed();
}
