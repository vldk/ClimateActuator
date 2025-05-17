#include <Arduino.h>
#include <EncButton.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <GyverTimer.h>
#include <Preferences.h>
#include "GOledMenuAda.h"
#include "driver/rtc_io.h"

#ifdef DEBUG_ENABLE
#define LOG(x) Serial.print(x)
#define LOGN(x) Serial.println(x)
#define MENU_ITEMS 10
#else
#define LOG(x)
#define LOGN(x)
#define MENU_ITEMS 9
#endif

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C //0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define ENC_L GPIO_NUM_1
#define ENC_R GPIO_NUM_2
#define ENC_BTN GPIO_NUM_3

#define LED_PIN GPIO_NUM_8
#define WND_SWITCH_PIN GPIO_NUM_4
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define DISPLAY_TIMEOUT 5000 //5000 // 5sec
#define MAX_TEMP 50.0
#define MIN_TEMP 10.0


#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex
#define WAKEUP_1              ENC_BTN         // Only RTC IO are allowed - ESP32 Pin example
#define WAKEUP_2              WND_SWITCH_PIN    // Only RTC IO are allowed - ESP32 Pin example

// Define bitmask for multiple GPIOs
uint64_t bitmask = BUTTON_PIN_BITMASK(WAKEUP_1) | BUTTON_PIN_BITMASK(WAKEUP_2);


Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
EncButton eb(ENC_L, ENC_R, ENC_BTN, INPUT_PULLUP);
Button wndSensor(WND_SWITCH_PIN);
GTimer displayIdleTimer(MS);
OledMenu<MENU_ITEMS, Adafruit_SSD1306> menu(&oled);
Preferences prefs;


RTC_DATA_ATTR float cur_t = 24.8; // TODO: remove RTC_DATA_ATTR after attach DHT11 sensor
float cur_h = 45.9;


struct Settings {
  float lowTemp = 22;
  float highTemp = 25;
  float turns = 1.2;
  bool isInverted = false;
  u_int checkPeriod = 20; // 60s 
} cfg;

RTC_DATA_ATTR bool oledEnabled = true;
RTC_DATA_ATTR bool isWndOpened = true;
bool isSleepWakeup = false;
bool isButtonWakeup = false;



void initMenu();
void initServo();
void initDisplay();
void toggleMainScreen(bool show);
void renderMainScreen();
void onMenuItemChange(const int index, const void* val, const byte valType);
bool onMenuItemPrintOverride(const int index, const void* val, const byte valType);
void encoder_cb();
void openValve();
void closeValve();
void idleDisplayTrigger();
void wakeDisplayTrigger();
void goToSleep();
void checkTemperature();
bool isIdleState();


// Method to print the reason by which ESP32 has been awaken from sleep
void define_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0: 
    case ESP_SLEEP_WAKEUP_EXT1:  
      isButtonWakeup = true;
      isSleepWakeup = true;
      // LOGN("Wakeup caused by external signal using RTC_CNTL"); 
      break;
    case ESP_SLEEP_WAKEUP_TIMER: 
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
    case ESP_SLEEP_WAKEUP_ULP:
      isSleepWakeup = true;
      isButtonWakeup = false;
    break;
    default: {
      isSleepWakeup = false;
      isButtonWakeup = false;
    }
  }
}

void initDisplay() {
  
  Wire.begin(7,9);      
  //SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    LOGN(F("SSD1306 allocation failed"));
  }
  LOGN(F("SSD1306 allocation OK"));
  // u8g2_for_adafruit_gfx.begin(oled);
  // u8g2_for_adafruit_gfx.setFont(u8g2_font_4x6_t_cyrillic);  // icon font

  // do not show Adafruit logo
  oled.clearDisplay();
  oled.drawPixel(0, 0, SSD1306_WHITE);
  oled.cp437(true);
  
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text  
  oled.display();

}

void initMenu() {
  // menu init
  menu.onChange(onMenuItemChange, false);
  menu.onPrintOverride(onMenuItemPrintOverride);
  
  menu.addItem(PSTR("VIDKR."));                                                                             // 0
  menu.addItem(PSTR("ZAKR."));                                                                              // 1
  menu.addItem(PSTR("TEMPER. VIDKR."), GM_N_FLOAT(0.5), &cfg.highTemp, &cfg.lowTemp, GM_N_FLOAT(MAX_TEMP)); // 2
  menu.addItem(PSTR("TEMPER. ZAKR."), GM_N_FLOAT(0.5), &cfg.lowTemp, GM_N_FLOAT(MIN_TEMP), &cfg.highTemp);  // 3
  menu.addItem(PSTR("PERIOD (s)"),  GM_N_U_INT(10), &cfg.checkPeriod, GM_N_U_INT(10), GM_N_U_INT(3600));    // 4
  menu.addItem(PSTR("OBERTIV"), GM_N_FLOAT(0.01), &cfg.turns, GM_N_FLOAT(0.01), GM_N_FLOAT(10));            // 5
  menu.addItem(PSTR("INVERTUVATY"), &cfg.isInverted);                                                       // 6
  menu.addItem(PSTR("RESET"));                                                                          // 7
  menu.addItem(PSTR("<<< EXIT"));                                                                       // 8
  #ifdef DEBUG_ENABLE
  menu.addItem(PSTR("-- SET -- "), GM_N_FLOAT(0.1), &cur_t, GM_N_FLOAT(MIN_TEMP), GM_N_FLOAT(MAX_TEMP)); // 9 // just for testing
  #endif

  eb.attach(encoder_cb);
}

void initServo() {

}

void toggleMainScreen(bool show) {
  if (show == true) {
    menu.showMenu(false);
    renderMainScreen();
  } else {
    menu.showMenu(true);
  }
}

void saveSettings() {
  // EEPROM.put(0, cfg);
  prefs.putBytes("0", &cfg, sizeof(cfg));
  LOGN("Saved to EEPROM");
}

void resetSettings() {
  Settings def;
  
  cfg.highTemp = def.highTemp;
  cfg.lowTemp = def.lowTemp;
  cfg.checkPeriod = def.checkPeriod;
  cfg.turns = def.turns;
  cfg.isInverted = def.isInverted;

  saveSettings();
}


void encoder_cb() {
  switch (eb.action()) {
    case EB_TURN:
      LOG(F("TURN:")); LOGN(eb.dir());

      if (menu.isMenuShowing) {
        if (eb.dir() == 1) {
          menu.selectNext(eb.fast());
        } else {
          menu.selectPrev(eb.fast());          
        }
      }
      wakeDisplayTrigger();
      break;
    case EB_CLICK:
      wakeDisplayTrigger();
      if (menu.isMenuShowing) {
        menu.toggleChangeSelected();
      }
      else {
        menu.showMenu(true);
      }
      break;
  }

  displayIdleTimer.reset();
  displayIdleTimer.setTimeout(DISPLAY_TIMEOUT);
}

void onMenuItemChange(const int index, const void* val, const byte valType) {
  LOG("valType: ");LOG(valType); LOG(" idx: "); LOGN(index);
  if (valType == VAL_ACTION) {
    
    if (index == 0) {
      openValve();
    } else if (index == 1) {
      closeValve();
    }  
    else if (index == 7) {
      resetSettings();
    }
    else if (index == 8) {
      toggleMainScreen(true);
    }
    #ifdef DEBUG_ENABLE
    else if  (index == 9) {
      checkTemperature();
    }
    #endif    
  } else {
    #ifdef DEBUG_ENABLE
    if (index == 9) {
      LOG("set manual new temp"); LOGN(cur_t);
    } else
    #endif  
    saveSettings();
  }
}

boolean onMenuItemPrintOverride(const int index, const void* val, const byte valType) {
  if (index == 4) {
    unsigned int minutes = cfg.checkPeriod / 60; // [mm]
    byte seconds = cfg.checkPeriod - (minutes * 60); // [ss]
    if (minutes < 10) {
      oled.print(0);
    }
    oled.print(minutes);
    oled.print(":");
    
    if (seconds < 10) {
      oled.print(0);
    }
    oled.print(seconds);
    
    return true;
  }
  
  return false;
}


void renderMainScreen() {
  LOGN("render main> enabled: "+ String(oledEnabled) + " menu is showing: " + String(menu.isMenuShowing));
  LOGN("exit?: " + String(!oledEnabled || menu.isMenuShowing));
  
  oled.clearDisplay();   
  oled.setTextWrap(false);
  
  // if (!oledEnabled || menu.isMenuShowing) return;


  char hum_str[32];
  snprintf(hum_str, sizeof(hum_str), "VOLOHIST: %.2f%%", cur_h);
  oled.setTextSize(1);
  oled.setCursor(20, 4);
  oled.print(hum_str);
  
  
  char temp_str[32];
  snprintf(temp_str, sizeof(temp_str), "%.1f%", cur_t);
  oled.setCursor(2, 16); 
  oled.setTextSize(3);
  oled.print(temp_str); oled.print(char(248)); oled.print("C");


  // oled.setTextSize(2);
  // oled.setCursor(16, SCREEN_HEIGHT - 18);
  // oled.print(isWndOpened ? "VIDKR." : "ZAKR.");
  
  oled.setCursor(0, SCREEN_HEIGHT - 18);
  oled.setTextSize(1);
  // oled.setTextWrap(false);
  oled.print("wakeup?: ");  oled.println(isSleepWakeup);
  oled.print("by btn?: ");  oled.println(isButtonWakeup);


  oled.display();
}


void idleDisplayTrigger(){
  menu.showMenu(false, true);
  oled.clearDisplay();
  oled.display();
  
  // oled.setPower(false);
  oled.ssd1306_command(SSD1306_DISPLAYOFF);
  oledEnabled = false;
  goToSleep();
}

void goToSleep() {
  #ifdef ENABLE_SLEEP
  LOGN("Going to sleep now. Would wakeup after " + String(cfg.checkPeriod) + " seconds.");
  esp_deep_sleep_start();
  #endif
}

void wakeDisplayTrigger() {
  if (!oledEnabled) {
    oled.ssd1306_command(SSD1306_DISPLAYON);
    oledEnabled = true;
  }
}


bool isIdleState() {
  return !oledEnabled;
}

void checkTemperature() {
  LOGN("display on: " + String(oledEnabled) + " opened: " + String(isWndOpened));
  LOG("LOW: "); LOG(cfg.lowTemp); LOG(" CUR: "); LOG(cur_t); LOG(" HI: "); LOGN(cfg.highTemp);
  
  if (cur_t >= cfg.highTemp) {
    openValve();
  } else if (cur_t < cfg.lowTemp) {
    closeValve();
  }

  if (isIdleState()) goToSleep();
}

void openValve() {
  if (isWndOpened) {
    LOGN(">>>> Value is opened already. Noting to do.");
    return;
  }

  LOG("!!!! Open valve open with ");
  LOG( (360 * cfg.turns) * (cfg.isInverted? -1 : 1) );
  LOGN(" deg");
  // isWndOpened = true;
  // digitalWrite(LED_PIN, HIGH); 
}

void closeValve() {
  if (!isWndOpened) {
    LOGN("<<<< Value is closed already. Noting to do.");
    return;
  }
  LOG("!!!! Close valve with ");
  LOG( (360 * cfg.turns) * (cfg.isInverted? 1 : -1) );
  LOGN(" deg");
  // isWndOpened = false;
  // digitalWrite(LED_PIN, LOW); 
}

void on_wnd_switch_change() {
  LOG("on_wnd_switch_change: "); LOGN(digitalRead(WND_SWITCH_PIN));
  isWndOpened = !digitalRead(WND_SWITCH_PIN);
  digitalWrite(LED_PIN, !isWndOpened); 
  if (!menu.isMenuShowing) {
    renderMainScreen();
  }
}

void setup() {
  #ifdef DEBUG_ENABLE
  Serial.begin(115200);
  delay(5000);
  #endif

  prefs.begin("0");
  prefs.getBytes("0", &cfg, sizeof(cfg));
  // LOG("eeprom len: "); LOGN(EEPROM.length());
  // EEPROM.get(0, cfg);  

  LOG("highTemp: ");LOGN(cfg.highTemp);

  pinMode(LED_PIN, OUTPUT);  
  pinMode(WND_SWITCH_PIN, INPUT_PULLUP);
  // attachInterrupt(WND_SWITCH_PIN, on_wnd_switch_change, CHANGE);
  // wndSensor.attach(on_wnd_switch_change);
  
  define_wakeup_reason();
  LOGN("Is awaked from sleep?: " + String(isSleepWakeup));
  LOGN("Is awaked by Btn?: " + String(isButtonWakeup));

  initDisplay();
  initMenu();
  initServo();

  if (isButtonWakeup || !isSleepWakeup) {
    wakeDisplayTrigger();
    toggleMainScreen(true);
    displayIdleTimer.setTimeout(DISPLAY_TIMEOUT); 
  }

  renderMainScreen();


  #ifdef ENABLE_SLEEP
  // esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK(ENC_BTN), ESP_EXT1_WAKEUP_ANY_HIGH);
    #ifdef ESP32C3
    // gpio_wakeup_enable(ENC_BTN, GPIO_INTR_LOW_LEVEL);
    // gpio_set_direction(gpio_num_t(ENC_BTN), GPIO_MODE_INPUT);
    // esp_sleep_enable_gpio_wakeup();
      //Use ext1 as a wake-up source

      // esp_sleep_enable_ext1_wakeup_io(bitmask, ESP_EXT1_WAKEUP_ANY_HIGH);
      esp_deep_sleep_enable_gpio_wakeup(bitmask, ESP_GPIO_WAKEUP_GPIO_LOW);
      // gpio_set_direction(gpio_num_t(WAKEUP_1), GPIO_MODE_INPUT);
      // gpio_set_direction(gpio_num_t(WAKEUP_2), GPIO_MODE_INPUT);
      // enable pull-down resistors and disable pull-up resistors
      // rtc_gpio_pullup_en(WAKEUP_1);
      // rtc_gpio_pulldown_dis(WAKEUP_1);
      // rtc_gpio_pulldown_en(WAKEUP_2);
      // rtc_gpio_pullup_dis(WAKEUP_2);
    #else // esp32-dev
    esp_sleep_enable_ext0_wakeup(ENC_BTN, 0);  //1 = High, 0 = Low
    rtc_gpio_pullup_en(ENC_BTN);
    rtc_gpio_pulldown_dis(ENC_BTN);
    #endif
    esp_sleep_enable_timer_wakeup(cfg.checkPeriod * uS_TO_S_FACTOR);
  #endif

  eb.tick();
  checkTemperature();
  
}

void loop() {
  // LOGN("Loop tick");
  eb.tick();
  // wndSensor.tick();
  if (displayIdleTimer.isReady()) idleDisplayTrigger();
}
