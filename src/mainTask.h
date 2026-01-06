#include "Arduino.h"
#if !defined(MAIN_TASK_H)
#define MAIN_TASK_H

#include "baseTask.h"

//Main Task

class MainTask : public BaseTask {

public:
  MainTask(uint16_t stackSize,  // class BaseTasks's arguments
           UBaseType_t priority,
           const char* taskName)
    : BaseTask{ stackSize, priority, taskName } {}

  void main() override {
    //startup
    char buf[10];
    lcd.clear();
    for (;;) {
      // do stuff
      if (xQueueReceive(actionUpdateQueue, &(buf), 0)) {
        if (strcmp(buf, "update") == 0) {
          updatePage();
          //wake the screen
          mil2 = millis();
        }
      }
      if (xQueueReceive(responseQueue, &(buf), 0)) {
        //wake the screen
        mil2 = millis();
        lcd.setCursor(0, 2);
        lcd.print("                    ");
        mil = millis();
        switch (buf[0]) {
          case '1':
            if (buf[1] == '1') {
              lcd.setCursor(2, 2);
              lcd.write(0);
            } else {
              lcd.setCursor(2, 2);
              lcd.print("x");
            }
            break;
          case '2':
            if (buf[1] == '1') {
              lcd.setCursor(9, 2);
              lcd.write(0);
            } else {
              lcd.setCursor(9, 2);
              lcd.print("x");
            }
            break;
          case '3':
            if (buf[1] == '1') {
              lcd.setCursor(16, 2);
              lcd.write(0);
            } else {
              lcd.setCursor(16, 2);
              lcd.print("x");
            }
            break;
        }
      }
      if(millis() - mil >= 1000){
        lcd.setCursor(0, 2);
        lcd.print("                    ");
      }

      if(millis() - mil2 >= 5000){
        lcd.noBacklight();
      } else {
        lcd.backlight();
      }

      switch ((int)encoder.getDirection()) {
        case 1:
          if (pageNumber < maxPageNumber) {
            pageNumber++;
            updatePage();
          }
          //wake the screen
          mil2 = millis();
          break;
        case -1:
          if (pageNumber > 1) {
            pageNumber--;
            updatePage();
          }
          //wake the screen
          mil2 = millis();
          break;
      }
    }
  }

protected:
  unsigned long mil;
  unsigned long mil2;
  void updatePage() {
    //send out page number
    //show page
    char text[21];
    int len;
    sprintf(text, "Page %i/%i           ", pageNumber, maxPageNumber);
    lcd.setCursor(0, 0);
    lcd.print(text);
    //clear out the line
    lcd.setCursor(0, 3);
    lcd.print("                    ");
    //show names
    switch (pageNumber) {
      case 1:
        lcd.setCursor(0, 3);
        lcd.print(actionNames[pageNumber-1]);
        len = actionNames[pageNumber].length();
        lcd.setCursor((20 - len) / 2, 3);
        lcd.print(actionNames[pageNumber]);
        lcd.setCursor(14, 3);
        lcd.print(actionNames[pageNumber+1]);
        break;
      case 2:
        lcd.setCursor(0, 3);
        lcd.print(actionNames[pageNumber+1]);
        len = actionNames[pageNumber+2].length();
        lcd.setCursor((20 - len) / 2, 3);
        lcd.print(actionNames[pageNumber+2]);
        lcd.setCursor(14, 3);
        lcd.print(actionNames[pageNumber+3]);
        break;
      case 3:
        lcd.setCursor(0, 3);
        lcd.print(actionNames[pageNumber+3]);
        len = actionNames[pageNumber+4].length();
        lcd.setCursor((20 - len) / 2, 3);
        lcd.print(actionNames[pageNumber+4]);
        lcd.setCursor(14, 3);
        lcd.print(actionNames[pageNumber+5]);
        break;
    }
  }
  void updateDisplay(String text) {
    int len = text.length();
    if (len < 20) {
      lcd.setCursor(0, 0);
      lcd.print("   COOLBOX - V1.0   ");
      lcd.setCursor(0, 1);
      lcd.print("    by timeylies    ");
      lcd.setCursor(0, 3);
      lcd.print("                    ");
      lcd.setCursor((20 - len) / 2, 3);
      lcd.print(text);
    }
  }
};

#endif  // MAIN_TASK_H