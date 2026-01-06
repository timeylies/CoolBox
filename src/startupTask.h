#if !defined(STARTUP_TASK_H)
#define STARTUP_TASK_H

#include "baseTask.h"

class Startup : public BaseTask {

public:
  Startup(uint16_t stackSize,  // class BaseTasks's arguments
          UBaseType_t priority,
          const char* taskName)
    : BaseTask{ stackSize, priority, taskName } {}

  void main() override {
    //the unnecessary startup
    updateDisplay("Starting Up...");
    update_buttons(150);
    delay(1000);
    updateDisplay("Idle...");
    delay(300);
    update_buttons(0);
    lcd.noBacklight();
    //wait for discovery from serial
    char buf[10];
    for (;;) {
      //updateDisplay("Waiting for PC...");
      //switch over to the main task once done
      if (xQueueReceive(discoveryQueue, &(buf), 0)) {
        if (strcmp(buf, "discovery") == 0) {
          lcd.backlight();
          updateDisplay("Loading...");
          delay(500);
          char buf[10];
          sprintf(buf, "init");
          xQueueSend(mainTaskQueue, (void*)buf, (TickType_t)0);
          vTaskDelete(NULL);
        }
      }
      vTaskDelay(1);
    }
  }

protected:

  void update_buttons(int value) {
    led_button1 = value;
    led_button2 = value;
    led_button3 = value;
    update_buttonLeds();
  }

  void updateDisplay(const char* text) {
    int len = strlen(text);
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

#endif  // STARTUP_TASK_H