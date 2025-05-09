#include <Arduino.h>
#include <GyverOLED.h>
#include <GyverOLEDMenu.h>
#include <EncButton.h>

#ifdef DEBUG_ENABLE
#define LOG(x) Serial.println(x)
#else
#define LOG(x)
#endif


EncButton eb(13, 12, 14, INPUT_PULLUP);
GyverOLED<SSD1306_128x64> oled;
OledMenu<7, GyverOLED<SSD1306_128x64>> menu(&oled);

void toggleMainScreen(bool show);
void renderMainScreen();
void onItemChange(const int index, const void* val, const byte valType);
void encoder_cb();
void openValve();
void closeValve();

float cur_t = 24.8;
float cur_h = 45.9;

boolean reverse = false;
// struct settings {
  float lowTemp = 22;
  float highTemp = 25;
  float turns = 1.2;
// }


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
  menu.onChange(onItemChange, true);
  
  menu.addItem(PSTR("ВIДКРИТИ"));                                                                 // 0
  menu.addItem(PSTR("ЗАКРИТИ"));                                                                  // 1
  menu.addItem(PSTR("ТЕМР. ВIДК."), GM_N_FLOAT(0.5), &highTemp, &lowTemp, GM_N_FLOAT(60));        // 2
  menu.addItem(PSTR("ТЕМР. ЗАКР."), GM_N_FLOAT(0.5), &lowTemp, GM_N_FLOAT(10), &highTemp);        // 3
  menu.addItem(PSTR("К-ТЬ ОБЕРТ."), GM_N_FLOAT(0.01), &turns, GM_N_FLOAT(0.01), GM_N_FLOAT(10));  // 4
  menu.addItem(PSTR("IНВЕРТУВАТИ"), &reverse);                                                    // 5 
  menu.addItem(PSTR("<- ВИХIД"));                                                                 // 6
  //
  toggleMainScreen(true);
  eb.attach(encoder_cb);
}

void loop() {
  eb.tick();
}


void onItemChange(const int index, const void* val, const byte valType) {
  if (valType == VAL_ACTION) {
    if (index == 6) {
      toggleMainScreen(true);
    }
    else if (index == 0) {
      openValve();
    } else if (index == 1) {
      closeValve();
    }
  }
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
  snprintf(temp_str, sizeof(temp_str), "Т: %.2fC%", cur_t);
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
  Serial.println("Valve open");
}

void closeValve() {
  Serial.println("Valve Close");
}

void encoder_cb() {
  if (menu.isMenuShowing) {
    switch (eb.action()) {
      case EB_TURN:
        if (eb.dir() == 1) {
          menu.selectPrev(eb.fast());
        } else {
          menu.selectNext(eb.fast());
        }
  
        break;
  
      case EB_CLICK:
        menu.toggleChangeSelected();
        break;
    }
  }
  else { 
    if (eb.action() == EB_CLICK) {
      menu.showMenu(true);
    }
  }
}
