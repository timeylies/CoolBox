#if !defined(STARTUP_TASK_H)
#define STARTUP_TASK_H

#include "baseTask.h"

class Startup : public BaseTask
{

public:
  Startup(uint16_t stackSize, // class BaseTasks's arguments
          UBaseType_t priority,
          const char *taskName)
      : BaseTask{stackSize, priority, taskName}
  {
  }

  void main() override
  {
    updateDisplay("Disconnected...");
    update_buttons(150);
    vTaskDelay(1000/portTICK_PERIOD_MS);
    update_buttons(0);
    lcd.noBacklight();
    // wait for discovery from serial
    unsigned long prevMillis;
    int pressReadLimit;

    char buf[10];
    for (;;)
    {
      // updateDisplay("Waiting for PC...");
      // switch over to the main task once done
      // do menu stuff
      uint32_t duration = 0;
      if (encoderButton.isPressed())
      {
        if (pressReadLimit == 0)
        {
          prevMillis = millis();
          pressReadLimit = 1;
        }
        duration = millis() - prevMillis;
        if (duration >= 1000)
        {
          lcd.clear();
          currentMenu = &startupMainMenu;
          currentMenu->selected = 0;
          isMenuShown = true;
          lcd_updateMenu();
          lcd.backlight();
          duration = 0;
          pressReadLimit = 0;
        }
      }
      else
      {
        duration = 0;
        pressReadLimit = 0;
      }
      if (isMenuShown)
      {
        switch ((int)encoder.getDirection())
        {
        case 1:
          currentMenu->selected++;
          if (currentMenu->selected >= currentMenu->numItems)
          {
            currentMenu->selected = currentMenu->numItems - 1;
          }
          lcd_updateMenu();
          break;
        case -1:
          if (currentMenu->selected > 0)
          {
            currentMenu->selected--;
          }
          lcd_updateMenu();
          break;
        }
        if (encoderButton.getSingleDebouncedPress())
        {

          if (currentMenu->items[currentMenu->selected].subMenu != NULL)
          {
            // if there is a submenu, change to submenu
            currentMenu = currentMenu->items[currentMenu->selected].subMenu;
            lcd_updateMenu();
          }
          else if (currentMenu->items[currentMenu->selected].action != NULL)
          {
            // if there is an action, setup current action and current parameter
            // currentMenu->items[currentMenu->selected].action(currentMenu->items[currentMenu->selected].param);
            currentAction = currentMenu->items[currentMenu->selected].action;
            currentParam = currentMenu->items[currentMenu->selected].param;
          }
          else if (currentMenu->parentMenu != NULL)
          {
            // if there is a paremnt menu, change to the parent menu
            currentMenu = currentMenu->parentMenu;
            lcd_updateMenu();
          }
          else
          {
            if (currentMenu == &startupMainMenu)
            {
              // exit
              isMenuShown = false;
              lcd.clear();
            }
            else
            {
              currentMenu = &startupMainMenu;
              lcd_updateMenu();
            }
          }
        }
        if (currentAction != NULL)
        {
          if (currentAction(currentParam) == true)
          {
            currentAction = NULL;
            currentParam = NULL;
          }
        }
      }
      else if (isBuzzerVolumeChangeScreenShown)
      {
        switch ((int)encoder.getDirection())
        {
        case 1:
          if(buzzer_volume < 10){
            buzzer_volume++;
          }
          buzzerVolumeChange_updateScreen();
          buzzer_playTestBeep();
          break;
        case -1:
        if(buzzer_volume > 0){
            buzzer_volume--;
          }
          buzzerVolumeChange_updateScreen();
          buzzer_playTestBeep();
          break;
        }
        if (encoderButton.getSingleDebouncedPress())
        {
          //exit and go back to the menus
          isBuzzerVolumeChangeScreenShown = false;
          isMenuShown = true;
          lcd.clear();
          lcd_updateMenu();
        }
      }
      else
      {
        updateDisplay("Idle...");
        lcd.noBacklight();
      }
      
      if (xQueueReceive(discoveryQueue, &(buf), 0))
      {
        if (strcmp(buf, "discovery") == 0)
        {
          lcd.backlight();
          updateDisplay("Loading...");
          buzzer_playConnected();
          delay(500);
          char buf[10];
          sprintf(buf, "init");
          xQueueSend(mainTaskQueue, (void *)buf, (TickType_t)0);
          vTaskDelete(NULL);
        }
      }
      vTaskDelay(1);
    }
  }

protected:
  void update_buttons(int value)
  {
    led_button1 = value;
    led_button2 = value;
    led_button3 = value;
    update_buttonLeds();
  }

  void updateDisplay(const char *text)
  {
    int len = strlen(text);
    if (len < 20)
    {
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

#endif // STARTUP_TASK_H