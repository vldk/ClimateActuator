#include <Arduino.h>
#include <GyverOLED.h>
#include <GyverOLEDMenu.h>
#include <EncButton.h>
#include <GyverTimer.h>
#include "driver/rtc_io.h"

#ifdef DEBUG_ENABLE
#define LOG(x) Serial.print(x)
#define LOGN(x) Serial.println(x)
#else
#define LOG(x)
#define LOGN(x)
#endif

// #define MENU_PARAMS_LEFT_OFFSET 68 // 92
// #define MENU_ITEM_SELECT_W 100 //127
#define EB_FAST_TIME 120 


#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex

#define DISPLAY_TIMEOUT 5000 //5000 // 5sec
#define MAX_TEMP 50.0
#define MIN_TEMP 10.0

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  10          /* Time ESP32 will go to sleep (in seconds) */

#define ENC_L GPIO_NUM_12
#define ENC_R GPIO_NUM_13
#define ENC_BTN GPIO_NUM_14

GTimer displayIdleTimer(MS);
// GTimer temperatureCheckTimer(MS);
EncButton eb(ENC_L, ENC_R, ENC_BTN, INPUT_PULLUP);
GyverOLED<SSD1306_128x64> oled;
OledMenu<10, GyverOLED<SSD1306_128x64>> menu(&oled);


void initMenu();
void initServo();
void initOled();
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


RTC_DATA_ATTR float cur_t = 24.8; // TODO: remove RTC_DATA_ATTR after attach DHT11 sensor
float cur_h = 45.9;

// struct settings {
  float lowTemp = 22;
  float highTemp = 25;
  float turns = 1.2;
  bool isInverted = false;
  u_int checkPeriod = 20; // 60s 
// }

RTC_DATA_ATTR bool oledEnabled = true;
RTC_DATA_ATTR bool isValveOpened = true; // TODO: probably need to use endstop or make it configurable with menu (with pre-defined endstop pin)
bool isSleepWakeup = false;
bool isButtonWakeup = false;


// Method to print the reason by which ESP32 has been awaken from sleep
void define_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : 
    case ESP_SLEEP_WAKEUP_EXT1 :  
      isButtonWakeup = true;
      isSleepWakeup = true;
      // LOGN("Wakeup caused by external signal using RTC_CNTL"); 
      break;
    case ESP_SLEEP_WAKEUP_TIMER : // Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : // Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : // Serial.println("Wakeup caused by ULP program"); break;
      isSleepWakeup = true;
      isButtonWakeup = false;
    break;
    default : {
      isSleepWakeup = false;
      isButtonWakeup = false;
      // Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
    }
  }
}


void setup() {
#ifdef DEBUG_ENABLE
  Serial.begin(115200);
  // delay(1000);
#endif
  
  define_wakeup_reason();
  LOGN("Is awaked from sleep?: " + String(isSleepWakeup));
  LOGN("Is awaked by Btn?: " + String(isButtonWakeup));
  initOled();
  initMenu();
  initServo();
    
  if (isButtonWakeup || !isSleepWakeup) {
    wakeDisplayTrigger();
    toggleMainScreen(true);
    displayIdleTimer.setTimeout(DISPLAY_TIMEOUT); 
  }

  // esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK(ENC_BTN), ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_ext0_wakeup(ENC_BTN, 0);  //1 = High, 0 = Low
  
  rtc_gpio_pullup_en(ENC_BTN);
  rtc_gpio_pulldown_dis(ENC_BTN);

  esp_sleep_enable_timer_wakeup(checkPeriod * uS_TO_S_FACTOR);
  eb.tick();
  checkTemperature();
}

void loop() {
  // LOGN("Loop tick");
  eb.tick();

  if (displayIdleTimer.isReady()) idleDisplayTrigger();
  // if (temperatureCheckTimer.isReady()) checkTemperature();
  // renderMainScreen();
}

void initOled() {
  // display init
  oled.init();
  Wire.setClock(400000L);
  oled.clear();
  oled.update();
}

void initMenu() {
  // menu init
  menu.onChange(onMenuItemChange, true);
  menu.onPrintOverride(onMenuItemPrintOverride);
  
  menu.addItem(PSTR("ВIДКРИТИ"));                                                                   // 0
  menu.addItem(PSTR("ЗАКРИТИ"));                                                                    // 1
  menu.addItem(PSTR("ТЕМР. ВIДК."), GM_N_FLOAT(0.5), &highTemp, &lowTemp, GM_N_FLOAT(MAX_TEMP));    // 2
  menu.addItem(PSTR("ТЕМР. ЗАКР."), GM_N_FLOAT(0.5), &lowTemp, GM_N_FLOAT(MIN_TEMP), &highTemp);    // 3
  menu.addItem(PSTR("ПЕРІОД (c)"),  GM_N_U_INT(10), &checkPeriod, GM_N_U_INT(10), GM_N_U_INT(3600));// 4
  menu.addItem(PSTR("К-ТЬ ОБЕРТ."), GM_N_FLOAT(0.01), &turns, GM_N_FLOAT(0.01), GM_N_FLOAT(10));    // 5
  menu.addItem(PSTR("IНВЕРТУВАТИ"), &isInverted);                                                   // 6
  menu.addItem(PSTR("СКИНУТИ"));                                                                    // 7
  menu.addItem(PSTR("<- ВИХIД"));                                                                   // 8
  menu.addItem(PSTR("-- SET -- "), GM_N_FLOAT(0.1), &cur_t, GM_N_FLOAT(MIN_TEMP), GM_N_FLOAT(MAX_TEMP)); // 9 // just for testing

  
  eb.attach(encoder_cb);
}

void initServo() {

}

bool isIdleState() {
  return !oledEnabled;
}

void checkTemperature() {
  LOGN("display on: " + String(oledEnabled) + " opened: " + String(isValveOpened));
  LOG("LOW: "); LOG(lowTemp); LOG(" CUR: "); LOG(cur_t); LOG(" HI: "); LOGN(highTemp);
  
  if (cur_t >= highTemp) {
    openValve();
  } else if (cur_t < lowTemp) {
    closeValve();
  }

  if (isIdleState()) goToSleep();
}


void goToSleep() {
  // #ifndef DEBUG_ENABLE  
  LOGN("Going to sleep now. Would wakeup after " + String(checkPeriod) + " seconds.");
  esp_deep_sleep_start();
  // #endif
}

void idleDisplayTrigger(){
  menu.showMenu(false, true);
  oled.clear();
  oled.setPower(false);
  oledEnabled = false;
  goToSleep();
}

void wakeDisplayTrigger() {
  if (!oledEnabled) {
    oled.setPower(true);
    oledEnabled = true;
  }
  // menu.showMenu(false, true);
  // oled.clear();
  
}

void onMenuItemChange(const int index, const void* val, const byte valType) {
  if (valType == VAL_ACTION) {
    if (index == 8) {
      toggleMainScreen(true);
    }
    // else if (index == 4) {
    //   temperatureCheckTimer.setInterval(checkPeriod * 1000);
    // }
    else if (index == 0) {
      openValve();
    } else if (index == 1) {
      closeValve();
    }
  }
}

boolean onMenuItemPrintOverride(const int index, const void* val, const byte valType) {
  if (index == 4) {
    unsigned int minutes = checkPeriod / 60; // [mm]
    byte seconds = checkPeriod - (minutes * 60); // [ss]
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

void toggleMainScreen(bool show) {
  if (show == true) {
    menu.showMenu(false);
    renderMainScreen();
  } else {
    menu.showMenu(true);
  }
}

void renderMainScreen() {
  LOGN("render main> enabled: "+ String(oledEnabled) + " menu is showing: " + String(menu.isMenuShowing));
  LOGN("exit?: " + String(!oledEnabled || menu.isMenuShowing));
  // if (!oledEnabled || menu.isMenuShowing) return;
  oled.clear();   
  oled.update();

  // --------------------------
  char temp_str[32];
  snprintf(temp_str, sizeof(temp_str), "T:%.1fC%", cur_t);
  oled.setCursor(1, 0); 
  oled.setScale(3);
  oled.print(temp_str);

  oled.setScale(2);
  oled.setCursorXY(0, 31);
  oled.print("VP: "); oled.print(isValveOpened ? "ВIДК" : "ЗАКР");
  oled.update();


  oled.update();
  char hum_str[32];
  snprintf(hum_str, sizeof(hum_str), "Вол: %.2f%%", cur_h);
  oled.setScale(1);
  oled.setCursor(1, 6);
  oled.print(hum_str);
  oled.update();
  
}

void openValve() {
  if (isValveOpened) {
    LOGN(">>>> Value is opened already. Noting to do.");
    return;
  }

  LOG("!!!! Open valve open with ");
  LOG( (360 * turns) * (isInverted? -1 : 1) );
  LOGN(" deg");

  isValveOpened = true;
}

void closeValve() {
  if (!isValveOpened) {
    LOGN("<<<< Value is closed already. Noting to do.");
    return;
  }
  LOG("!!!! Close valve with ");
  LOG( (360 * turns) * (isInverted? 1 : -1) );
  LOGN(" deg");
  isValveOpened = false;
}

void encoder_cb() {
  switch (eb.action()) {
    case EB_TURN:
      if (menu.isMenuShowing) {
        if (eb.dir() == 1) {
          menu.selectPrev(eb.fast());
        } else {
          menu.selectNext(eb.fast());
        }
      }
      wakeDisplayTrigger();
      break;
    case EB_CLICK:
      wakeDisplayTrigger();
      // if (!oledEnabled) {
      //   oled.setPower(true);
      //   oledEnabled = true;
      //   renderMainScreen();
      //   return;
      // }
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
