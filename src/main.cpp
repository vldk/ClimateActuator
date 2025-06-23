#include <Arduino.h>
#include <EncButton.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <GyverTimer.h>
#include <Preferences.h>
#include <ServoSmooth.h>
#include "GOledMenuAda.h"
#include "driver/rtc_io.h"
#include "DHT.h"
#include <Adafruit_INA219.h>


#ifdef DEBUG_ENABLE
#define LOG(x) Serial.print(x)
#define LOGN(x) Serial.println(x)
#define MENU_ITEMS 12
// #define DISPLAY_TIMEOUT 120000
#else
#define LOG(x)
#define LOGN(x)
#define MENU_ITEMS 10
// #define DISPLAY_TIMEOUT 10000 // 10sec
#endif

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C //0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define ENC_BTN GPIO_NUM_1
#define ENC_L GPIO_NUM_2
#define ENC_R GPIO_NUM_3

#define DHT_PIN GPIO_NUM_5

#define LED_PIN GPIO_NUM_8
#define HIGHT_ENDSTOP_PIN GPIO_NUM_20
#define LOW_ENDSTOP_PIN GPIO_NUM_21
#define SERVO_PIN GPIO_NUM_10
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define MAX_TEMP 50.0
#define MIN_TEMP 10.0
#define KICK_DELAY 1000 // freeze for 1 sec during rotation start
#define LION_BATTERIES_COUNT 2


#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex
#define WAKEUP_1  ENC_BTN         // Only RTC IO are allowed - ESP32 Pin example
// #define WAKEUP_2  WND_SWITCH_PIN    // Only RTC IO are allowed - ESP32 Pin example

// Define bitmask for multiple GPIOs
uint64_t bitmask = BUTTON_PIN_BITMASK(WAKEUP_1) /* | BUTTON_PIN_BITMASK(WAKEUP_2) */;


Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
EncButton eb(ENC_L, ENC_R, ENC_BTN, INPUT_PULLUP);
// Button hightEndstor(HIGHT_ENDSTOP_PIN, INPUT_PULLUP, HIGH);
// Button lowEndstor(LOW_ENDSTOP_PIN, INPUT_PULLUP, HIGH) ;
GTimer displayIdleTimer(MS);
GTimer animTimer(MS);
GTimer temperatureTimer(MS);
OledMenu<MENU_ITEMS, Adafruit_SSD1306> menu(&oled);
ServoSmooth servo;
Preferences prefs;
DHT dht(DHT_PIN, DHT11);
Adafruit_INA219 ina219;


RTC_DATA_ATTR float cur_t = 24.8; // TODO: remove RTC_DATA_ATTR after attach DHT11 sensor
float cur_h = 45.9;

byte rotateDirection = 1; // no rotation
byte animationPos = 0;


#define ROTATE_UPWARD 500
#define ROTATE_STOP 1500
#define ROTATE_DOWNWARD 2500

struct Settings {
  float lowTemp = 22;
  float highTemp = 25;
  float tempCorrection = -1.5;
  u_int checkPeriod = 20; //TODO: set to 60s for prod
  u_int displayTimeout = 10; // 10 sec
} cfg;

RTC_DATA_ATTR bool oledEnabled = true;
RTC_DATA_ATTR bool hightEndstopPressed = false;
RTC_DATA_ATTR bool lowEndstopPressed = false;

bool isFullOpened = false;
bool isPartiallyOpened = false;

/**
 *  0 - idle, no operations
 *  1 - opening is in progress
 *  2 - closing is in progress
 */
byte servoOperation = 0;
bool isSleepWakeup = false;
bool isButtonWakeup = false;


byte batPers = 0;
float batVoltage = 0;


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
void readTemperature();
void readBattery();
bool isIdleState();
void saveSettings();
void resetSettings();
void manualRunServo();
void defineWndOpenState();
void drawBattery(int16_t x, int16_t y, byte percent/* , byte scale = 1 */);

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Method to print the reason by which ESP32 has been awaken from sleep
void define_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();
  LOG("wake reason: "); LOGN(wakeup_reason);
  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_GPIO: 
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
      LOGN(wakeup_reason);
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

  oled.setRotation(2); // rotate 180deg
  // do not show Adafruit logo
  oled.clearDisplay();
  oled.drawPixel(0, 0, SSD1306_WHITE);
  oled.cp437(true);
  
  // oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text  
  oled.print(millis());
  oled.display();
  // sleep(100);
  // oled.clearDisplay();
  // oled.display();
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
  menu.addItem(PSTR("EKRAN T. (s)"), GM_N_U_INT(1) , &cfg.displayTimeout, GM_N_U_INT(2), &cfg.checkPeriod); // 5
  menu.addItem(PSTR("TEMP.CORR. (C)"), GM_N_FLOAT(0.5) , &cfg.tempCorrection, GM_N_FLOAT(-10), GM_N_FLOAT(10));// 6
  menu.addItem(PSTR("RESET"));                                                                              // 7
  menu.addItem(PSTR("<- M ->"), GM_N_BYTE(1), &rotateDirection, GM_N_BYTE(0), GM_N_BYTE(2));                // 8 
  menu.addItem(PSTR("<<< EXIT"));                                                                           // 9
  #ifdef DEBUG_ENABLE
  menu.addItem(PSTR("-- SET -- "), GM_N_FLOAT(0.1), &cur_t, GM_N_FLOAT(MIN_TEMP), GM_N_FLOAT(MAX_TEMP));    // 10 // just for testing
  menu.addItem(PSTR("-- BAT -- "), GM_N_BYTE(1), &batPers, GM_N_BYTE(0), GM_N_BYTE(100));                   // 11 // just for testing
  #endif

  eb.attach(encoder_cb);
}

void initServo() {
  servo.attach(SERVO_PIN);        // подключить
  // servo.setSpeed(40);    // ограничить скорость
  // servo.setAccel(0.1);   	  // установить ускорение (разгон и торможение)
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
  prefs.putBytes("0", &cfg, sizeof(cfg));
  LOGN("Saved to EEPROM");
}

void resetSettings() {
  Settings def;
  
  cfg.highTemp = def.highTemp;
  cfg.lowTemp = def.lowTemp;
  cfg.checkPeriod = def.checkPeriod;
  cfg.displayTimeout = def.displayTimeout;
  cfg.tempCorrection = def.tempCorrection;

  saveSettings();
}


void encoder_cb() {
  switch (eb.action()) {
    case EB_TURN:
      LOG(F("TURN:")); LOGN(eb.dir());

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
      if (menu.isMenuShowing) {
        menu.toggleChangeSelected();
      }
      else {
        menu.showMenu(true);
      }
      break;
  }

  displayIdleTimer.reset();
  displayIdleTimer.setTimeout(cfg.displayTimeout * 1000);
}

void onMenuItemChange(const int index, const void* val, const byte valType) {
  LOG("valType: ");LOG(valType); LOG(" idx: "); LOGN(index);
  if (valType == VAL_ACTION) {
    
    if (index == 0) {
      openValve();
      toggleMainScreen(true);
    } else if (index == 1) {
      closeValve();
      toggleMainScreen(true);
    }  
    else if (index == 7) {
      resetSettings();
    } 
    else if (index == 9) {
      toggleMainScreen(true);
    }
  } else {
    #ifdef DEBUG_ENABLE
    if (index == 10) {
      LOG("set manual new temp"); LOGN(cur_t);
      checkTemperature();
    } else 
    #endif 
    if (index == 8) {
      manualRunServo();
    } else      
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
  } else if (index == 5) {
    unsigned int minutes = cfg.displayTimeout / 60; // [mm]
    byte seconds = cfg.displayTimeout - (minutes * 60); // [ss]
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
  
  else if (index == 8) {
    char label[10] = "";
    if (rotateDirection == 0)  strcat(label,  " UP " );
    else if (rotateDirection == 2) strcat(label,  "DOwN" );
    else strcat(label,  " O " );

    oled.print(label);
    return true;
  }
  
  return false;
}


/**
 * Draws battery with 12x8
 */
void drawBattery(int16_t x, int16_t y, byte percent/* ,  byte scale = 1 */) {
  oled.drawRect(x, y + 2, 2, 4, WHITE); // пипка

  oled.fillRect(x + 2, y, 12, 8, WHITE); // стенка
  // oled.drawLine(x + 2, y + 2, x+3, y+6, WHITE);
  oled.fillRect(x + 3, y+1, map(100 - batPers, 0, 100, 0, 10), 6, BLACK);
}


void renderMainScreen() {
  LOGN("--- --- ---");
  LOG("render main> enabled: "); LOG(oledEnabled); LOG(" menu is showing: "); LOG(menu.isMenuShowing);
  LOG("exit?: ");LOGN(!oledEnabled || menu.isMenuShowing);
  
  if (!oledEnabled || menu.isMenuShowing) return;

  oled.clearDisplay();   
  oled.setTextWrap(false);
  
  readTemperature();

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

  // #ifndef DEBUG_ENABLE
  
  oled.setTextSize(2);
  oled.setCursor(4, SCREEN_HEIGHT - 18);
  
  if (servoOperation > 0) {
    animationPos += 1;
    if (animationPos >= 5) animationPos = 0;
    oled.setTextSize(2);
    switch (animationPos)
    {
      case 0: oled.print(servoOperation == 2 ? "    " : "    "); break;
      case 1: oled.print(servoOperation == 2 ? "   <" : ">   "); break;
      case 2: oled.print(servoOperation == 2 ? "  <-" : "->  "); break;
      case 3: oled.print(servoOperation == 2 ? " <--" : "--> "); break;
      case 4: oled.print(servoOperation == 2 ? "<---" : "--->"); break;
      default: oled.print(servoOperation == 2 ? animationPos : animationPos); break;
    }
  } else {
    if (isPartiallyOpened) {
      oled.print( "CHASTK.");
    } else {
      oled.print(isFullOpened ? "VIDKR." : "ZAKR.");
    }
    
  }  


  readBattery();
  // draw battery
  oled.setTextSize(1);

  oled.setCursor(SCREEN_WIDTH - 16-26, SCREEN_HEIGHT - 23); 
  char voltage_str[32];
  snprintf(voltage_str, sizeof(voltage_str), "%.1f%V", batVoltage);
  oled.printf(voltage_str);

  if (batPers < 20) {
    oled.setCursor(SCREEN_WIDTH - 16-26-4, SCREEN_HEIGHT - 12); 
    oled.print("!");
  }

  drawBattery(SCREEN_WIDTH - 16-24, SCREEN_HEIGHT - 12, batPers ); 
  oled.setCursor(SCREEN_WIDTH - 24, SCREEN_HEIGHT - 12); // 
  oled.print(batPers); oled.print("%");
  

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
  LOG("Going to sleep now. Would wakeup after "); LOG(cfg.checkPeriod); LOGN(" seconds.");
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
  return !oledEnabled && !servoOperation;
}

void readTemperature() {

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
    LOGN(F("Failed to read from DHT sensor!"));
    return;
  }
   
  // Compute heat index in Celsius (isFahreheit = false)
  // float hic = dht.computeHeatIndex(t, h, false);
  cur_h = h;
  cur_t = t + cfg.tempCorrection;

  LOG(F("Humidity: ")); LOGN(h);
  LOG(F("Temperature: ")); LOG(t);

  // renderMainScreen();
}

void readBattery() {
  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;

  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();
  current_mA = ina219.getCurrent_mA();
  power_mW = ina219.getPower_mW();
  loadvoltage = busvoltage + (shuntvoltage / 1000);

  bool is12vPow = loadvoltage > 4.2 * LION_BATTERIES_COUNT;  

  float minV = is12vPow ? 10.8 : (float)3.2 * LION_BATTERIES_COUNT; 
  float maxV = is12vPow ? 12.6 : (float)4.2 * LION_BATTERIES_COUNT;
  
  LOGN("vMin:"); LOG(minV); LOG(" maxV:"); LOGN(maxV);
  batPers = mapfloat(loadvoltage, minV, maxV, 0, 100);

  if (batPers > 100 ) batPers = 100;
  if (batPers < 0 ) batPers = 0;

  batVoltage = loadvoltage;

  LOGN("----");
  LOG("Bus Voltage:   "); LOG(busvoltage); LOGN(" V");
  LOG("Shunt Voltage: "); LOG(shuntvoltage); LOGN(" mV");
  LOG("Load Voltage:  "); LOG(loadvoltage); LOGN(" V");
  LOG("Current:       "); LOG(current_mA); LOGN(" mA");
  LOG("Power:         "); LOG(power_mW); LOGN(" mW");
  LOG("percents :     "); LOG(batPers); LOGN(" %");
  LOGN("----");
}

void checkTemperature() {

  LOG("display on: "); LOG(oledEnabled);LOG(" opened: "); LOGN(isFullOpened);
  LOG("LOW: "); LOG(cfg.lowTemp); LOG(" CUR: "); LOG(cur_t); LOG(" HI: "); LOGN(cfg.highTemp);
  
  if (cur_t >= cfg.highTemp) {
    openValve();
  } else if (cur_t < cfg.lowTemp) {
    closeValve();
  }

  if (isIdleState()) goToSleep();
}

void openValve() {
  if (servoOperation > 0) return; // action is already in progress;
  if (isFullOpened) {
    if (isPartiallyOpened) {      
      LOGN(">>>> Value is opened already. Noting to do.");
      return;
    } // else need to open fully
  }

  LOG("!!!! Open valve with "); LOGN(ROTATE_UPWARD);

  servoOperation = 1;
  servo.writeMicroseconds(ROTATE_UPWARD);
  delay(KICK_DELAY); // wait few secconds to release endststop switch
}

void closeValve() {
  if (servoOperation > 0) return; // action is already in progress;
  if (!isFullOpened) {
    if (!isPartiallyOpened) {
      LOGN("<<<< Value is closed already. Noting to do.");
      return;
    } // else need to close fully
  }
  LOG("!!!! Close valve with "); LOGN(ROTATE_DOWNWARD);
  servoOperation = 2;
  servo.writeMicroseconds(ROTATE_DOWNWARD);
  delay(KICK_DELAY); // wait few secconds to release endststop switch
  
}


void defineWndOpenState() {
  isPartiallyOpened = !lowEndstopPressed && hightEndstopPressed;
  LOG("is wnd partially opend: "); LOGN(isPartiallyOpened);
  bool _new = !lowEndstopPressed && !hightEndstopPressed;
  if (_new == isFullOpened) return;
  isFullOpened = _new;
  LOG("is wnd opend: "); LOGN(isFullOpened);
  #ifdef DEBUG_ENABLE
  digitalWrite(LED_PIN, !isFullOpened);
  #endif
  
  if (!menu.isMenuShowing) {
    renderMainScreen();
  }
}

void on_hight_endstop_change() {
  bool state = digitalRead(HIGHT_ENDSTOP_PIN);
  LOG("on hight wnd_switch hangler: "); LOG(state); LOG(" current: "); LOGN(hightEndstopPressed);
  if (hightEndstopPressed == state) return;
  hightEndstopPressed = state;
  
  LOG("on hight wnd_switch_change: "); LOGN(hightEndstopPressed);
  defineWndOpenState();
}

void on_low_endstop_change() {
  bool state = digitalRead(LOW_ENDSTOP_PIN);
  LOG("on low wnd_switch hangler: "); LOG(state); LOG(" current: "); LOGN(lowEndstopPressed);
  if (lowEndstopPressed == state) return;
  lowEndstopPressed = state;
  
  LOG("on low wnd_switch_change: "); LOGN(lowEndstopPressed);
  defineWndOpenState();
}

void manualRunServo() {
  LOG("Manual rotate: "); LOGN(rotateDirection);
  switch (rotateDirection)
  {
  case 0:
    // max forward rotate (open valve)
    servo.writeMicroseconds(ROTATE_UPWARD);
    break;
  case 1: 
    // no rotation
    servo.writeMicroseconds(ROTATE_STOP);
    break;
  case 2: 
    // max backward rotatie (close valve)
    servo.writeMicroseconds(ROTATE_DOWNWARD);
    break;
  }
  // servo.setCurrentDeg(rotateDirection);
}

void setup() {
  #ifdef DEBUG_ENABLE
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT); 
  // delay(5000);
  #endif

  prefs.begin("0");
  prefs.getBytes("0", &cfg, sizeof(cfg));
  
  pinMode(HIGHT_ENDSTOP_PIN, INPUT_PULLUP);
  pinMode(LOW_ENDSTOP_PIN, INPUT_PULLUP);
  // hightEndstor.setDebTimeout(255);
  // lowEndstor.setDebTimeout(255);

  attachInterrupt(HIGHT_ENDSTOP_PIN, on_hight_endstop_change, CHANGE);
  attachInterrupt(LOW_ENDSTOP_PIN, on_low_endstop_change, CHANGE);
  // hightEndstor.attach(on_hight_endstop_change);
  // lowEndstor.attach(on_low_endstop_change);
  
  define_wakeup_reason();
  LOG("Is awaked from sleep?: ");LOGN(isSleepWakeup);
  LOG("Is awaked by Btn?: "); LOGN(isButtonWakeup);

  initDisplay();
  initMenu();
  initServo();

  if ( !ina219.begin()) {
    LOGN("Failed to find INA219 chip");
  }

  dht.begin();

  #ifndef ENABLE_SLEEP
  temperatureTimer.setInterval(cfg.checkPeriod * 1000);
  #endif

  if (isButtonWakeup || !isSleepWakeup) {
    wakeDisplayTrigger();
    toggleMainScreen(true);
    displayIdleTimer.setTimeout(cfg.displayTimeout * 1000); 
  }
  
  defineWndOpenState();
  readTemperature();
  renderMainScreen();


  #ifdef ENABLE_SLEEP
  // esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK(ENC_BTN), ESP_EXT1_WAKEUP_ANY_HIGH);
    #ifdef ESP32C3

    gpio_deep_sleep_hold_dis();
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_deep_sleep_enable_gpio_wakeup(bitmask, ESP_GPIO_WAKEUP_GPIO_LOW);
    // esp_err_t errPD = esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    // Serial.print("esp_sleep_pd_config: ");
    // switch(errPD) {
    //   case ESP_OK: Serial.println("ESP_OK"); break;
    //   case ESP_ERR_INVALID_ARG: Serial.println("ESP_ERR_INVALID_ARG"); break;
    //   default: Serial.println("None"); break;
    // }
    // esp_err_t errGPIO = esp_deep_sleep_enable_gpio_wakeup(bitmask, ESP_GPIO_WAKEUP_GPIO_LOW);
    // Serial.print("esp_deep_sleep_enable_gpio_wakeup: ");
    // switch(errGPIO) {
    //   case ESP_OK: Serial.println("ESP_OK"); break;
    //   case ESP_ERR_INVALID_ARG: Serial.println("ESP_ERR_INVALID_ARG"); break;
    //   default: Serial.println("None"); break;
    // }
    gpio_set_direction(WAKEUP_1, GPIO_MODE_INPUT);
    
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
  // servo.tick();
  // hightEndstor.tick();
  // lowEndstor.tick();
  // animTimer.tick();
  if (displayIdleTimer.isReady()) idleDisplayTrigger();
  
  #ifndef ENABLE_SLEEP
  if (temperatureTimer.isReady()) readTemperature();
  #endif

  if (servoOperation > 0) {
    displayIdleTimer.reset();
    LOG("rotation is in progress... operation: "); LOG(servoOperation == 1 ? "Opening ": "Closing "); 
    LOG(" hight pressed: "); LOG(hightEndstopPressed); LOG(" low pressed: "); LOG(lowEndstopPressed); 
    LOG(" full condition: "); LOGN(servoOperation == 1 && !hightEndstopPressed && !lowEndstopPressed);
    if (!animTimer.isEnabled()) {
      animTimer.setInterval(300);
    }

    if (animTimer.isReady()) 
      renderMainScreen();
    
    if (
      (servoOperation == 1 && !hightEndstopPressed && !lowEndstopPressed) || // for opening trigger stop when both endstops is released
      (servoOperation == 2 && hightEndstopPressed && lowEndstopPressed) // for closing trigger stop when both endstops is pressed
    ) {
      servo.writeMicroseconds(ROTATE_STOP);
      servoOperation = 0;
      animTimer.reset();
      renderMainScreen();
    }
  }
}
