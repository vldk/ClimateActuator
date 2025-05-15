#include <Arduino.h>
#include <EncButton.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "GOledMenuAda.h"


#ifdef DEBUG_ENABLE
#define LOG(x) Serial.print(x)
#define LOGN(x) Serial.println(x)
#else
#define LOG(x)
#define LOGN(x)
#endif

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define ENC_L GPIO_NUM_1
#define ENC_R GPIO_NUM_2
#define ENC_BTN GPIO_NUM_3

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C //0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
EncButton eb(ENC_L, ENC_R, ENC_BTN, INPUT_PULLUP);

OledMenu<9, Adafruit_SSD1306> menu(&oled);

#define DISPLAY_TIMEOUT 5000 //5000 // 5sec
#define MAX_TEMP 50.0
#define MIN_TEMP 10.0

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


void encoder_cb() {
  switch (eb.action()) {
    case EB_TURN:
      LOG(F("TURN:")); LOGN(eb.dir());

      // if (menu.isMenuShowing) {
        if (eb.dir() == 1) {
          menu.selectPrev(eb.fast());
        } else {
          menu.selectNext(eb.fast());          
        }
      // }
      // wakeDisplayTrigger();
      break;
    case EB_CLICK:
      LOGN("CLICK");
      
      // wakeDisplayTrigger();
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

  // displayIdleTimer.reset();
  // displayIdleTimer.setTimeout(DISPLAY_TIMEOUT);
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
  oled.drawPixel(10, 10, SSD1306_WHITE);
  oled.display();
  oled.cp437(true);
  
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.setCursor(0,0);             // Start at top-left corner
  oled.println(F("Loaded"));
  oled.display();

  delay(3000);
  // tests
  oled.clearDisplay();

  oled.drawRect(1,1, 127, 8, WHITE);
  oled.setCursor(1, 1);
  oled.setTextColor(BLACK, WHITE);
  oled.print("Some text");
  
  oled.display();

  oled.setTextColor(WHITE, BLACK);
  oled.display();
  // delay(2000); 
  // oled.setTextSize(1);             // Normal 1:1 pixel scale
  // oled.setTextColor(SSD1306_WHITE);        // Draw white text
  // oled.setCursor(0,0);             // Start at top-left corner
  // oled.println(F("Hello, world!"));

  // oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Draw 'inverse' text
  // oled.println(3.141592);

  // oled.setTextSize(2);             // Draw 2X-scale text
  // oled.setTextColor(SSD1306_WHITE);
  // oled.print(F("0x")); oled.println(0xDEADBEEF, HEX);

  // oled.display();
  // delay(2000);
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

void initMenu() {
  // menu init
  // menu.onChange(onMenuItemChange, true);
  menu.onPrintOverride(onMenuItemPrintOverride);
  
  menu.addItem(PSTR("OPEN"));                                                                   // 0
  menu.addItem(PSTR("CLOSE"));                                                                    // 1
  menu.addItem(PSTR("Temp. open"), GM_N_FLOAT(0.5), &highTemp, &lowTemp, GM_N_FLOAT(MAX_TEMP));    // 2
  menu.addItem(PSTR("Temp. close"), GM_N_FLOAT(0.5), &lowTemp, GM_N_FLOAT(MIN_TEMP), &highTemp);    // 3
  menu.addItem(PSTR("PERIOD (s)"),  GM_N_U_INT(10), &checkPeriod, GM_N_U_INT(10), GM_N_U_INT(3600));// 4
  menu.addItem(PSTR("ROTATIONS"), GM_N_FLOAT(0.01), &turns, GM_N_FLOAT(0.01), GM_N_FLOAT(10));    // 5
  menu.addItem(PSTR("Invert"), &isInverted);                                                   // 6
  menu.addItem(PSTR("RESET"));                                                                    // 7
  menu.addItem(PSTR("<<< EXIT"));                                                                   // 8
  menu.addItem(PSTR("-- SET -- "), GM_N_FLOAT(0.1), &cur_t, GM_N_FLOAT(MIN_TEMP), GM_N_FLOAT(MAX_TEMP)); // 9 // just for testing

  
  eb.attach(encoder_cb);
}


void renderMainScreen() {
  LOGN("render main> enabled: "+ String(oledEnabled) + " menu is showing: " /* + String(menu.isMenuShowing) */);
  // LOGN("exit?: " + String(!oledEnabled || menu.isMenuShowing));
  // if (!oledEnabled || menu.isMenuShowing) return;
  // oled.clearDisplay();   
  // oled.display();

  // oled.setTextColor(SSD1306_WHITE); // Draw white text
  // oled.setCursor(0, 0);     // Start at top-left corner
  // oled.cp437(true);         // Use full 256 char 'Code Page 437' font

  // // --------------------------
  // char temp_str[32];
  // snprintf(temp_str, sizeof(temp_str), "T:%.1fC%", cur_t);
  // oled.setCursor(1, 0); 
  // oled.setTextSize(3);
  // oled.print(temp_str);

  // oled.setTextSize(2);
  // oled.setCursor(0, 31);
  // oled.print("VP: "); oled.print(isValveOpened ? "ВIДК" : "ЗАКР");
  

  // char hum_str[32];
  // snprintf(hum_str, sizeof(hum_str), "Вол: %.2f%%", cur_h);
  // oled.setTextSize(1);
  // oled.setCursor(1, 6);
  // oled.print(hum_str);
  // oled.display();
  
}

void setup() {
  #ifdef DEBUG_ENABLE
  Serial.begin(115200);
  delay(5000);
  #endif

  initDisplay();
  initMenu();
  // menu.showMenu(true);
  // renderMainScreen();

}

void loop() {
  // LOGN("Loop tick");
  eb.tick();

  // if (displayIdleTimer.isReady()) idleDisplayTrigger();
}
