#include <Arduino.h>
#include <GyverOLED.h>
#include <GyverOLEDMenu.h>
#include <EncButton.h>
#include <GyverTimer.h>

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
#define DISPLAY_TIMEOUT 5000 // 5sec

GTimer displayIdleTimer(MS);
EncButton eb(12, 13, 14, INPUT_PULLUP);
GyverOLED<SSD1306_128x64> oled;
OledMenu<8, GyverOLED<SSD1306_128x64>> menu(&oled);

void toggleMainScreen(bool show);
void renderMainScreen();
void onMenuItemChange(const int index, const void* val, const byte valType);
bool onMenuItemPrintOverride(const int index, const void* val, const byte valType);
void encoder_cb();
void openValve();
void closeValve();
void idleTrigger();


float cur_t = 24.8;
float cur_h = 45.9;

bool isInverted = false;
// struct settings {
  float lowTemp = 22;
  float highTemp = 25;
  float turns = 1.2;
  u_int checkPeriod = 60; // 60s 
// }

bool oledEnabled = true;

void setup() {
#ifdef DEBUG_ENABLE
  Serial.begin(115200);
#endif
  // display init
  oled.init();
  Wire.setClock(400000L);
  oled.clear();
  oled.update();

  // menu init
  menu.onChange(onMenuItemChange, true);
  menu.onPrintOverride(onMenuItemPrintOverride);
  
  menu.addItem(PSTR("ВIДКРИТИ"));                                                                   // 0
  menu.addItem(PSTR("ЗАКРИТИ"));                                                                    // 1
  menu.addItem(PSTR("ТЕМР. ВIДК."), GM_N_FLOAT(0.5), &highTemp, &lowTemp, GM_N_FLOAT(60));          // 2
  menu.addItem(PSTR("ТЕМР. ЗАКР."), GM_N_FLOAT(0.5), &lowTemp, GM_N_FLOAT(10), &highTemp);          // 3
  menu.addItem(PSTR("ПЕРІОД (c)"),  GM_N_U_INT(10), &checkPeriod, GM_N_U_INT(10), GM_N_U_INT(3600));// 4
  menu.addItem(PSTR("К-ТЬ ОБЕРТ."), GM_N_FLOAT(0.01), &turns, GM_N_FLOAT(0.01), GM_N_FLOAT(10));    // 5
  menu.addItem(PSTR("IНВЕРТУВАТИ"), &isInverted);                                                   // 6
  menu.addItem(PSTR("<- ВИХIД"));                                                                   // 7
  //
  toggleMainScreen(true);
  eb.attach(encoder_cb);
  displayIdleTimer.setTimeout(DISPLAY_TIMEOUT);
}

void loop() {
  eb.tick();

  if (displayIdleTimer.isReady()) {
    idleTrigger();
  }
}

void idleTrigger(){
  oled.clear();
  menu.showMenu(false, false);
  oled.setPower(false);
  oledEnabled = false;
}

void onMenuItemChange(const int index, const void* val, const byte valType) {
  if (valType == VAL_ACTION) {
    if (index == 7) {
      toggleMainScreen(true);
    }
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
  oled.clear();   
  oled.update();

  // --------------------------
  char temp_str[32];
  snprintf(temp_str, sizeof(temp_str), "Т:%.2fC%", cur_t);
  oled.setCursor(1, 0); 
  oled.setScale(3);
  oled.print(temp_str);

  // oled.setScale(1);
  // oled.println();
  oled.update();
  char hum_str[32];
  snprintf(hum_str, sizeof(hum_str), "Вол: %.2f%%", cur_h);
  oled.setScale(1);
  oled.setCursor(1, 6);
  oled.print(hum_str);
  oled.update();
  
}

void openValve() {
  LOG("Open valve open with ");
  LOG( (360 * turns) * (isInverted? -1 : 1) );
  LOGN(" deg");
}

void closeValve() {
  LOG("Close valve with ");
  LOG( (360 * turns) * (isInverted? 1 : -1) );
  LOGN(" deg");
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
      if (!oledEnabled) {
        oled.setPower(true);
        oledEnabled = true;
      }
      break;
    case EB_CLICK:
      if (!oledEnabled) {
        oled.setPower(true);
        oledEnabled = true;
        return;
      }
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
