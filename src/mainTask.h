#include "Arduino.h"
#if !defined(MAIN_TASK_H)
#define MAIN_TASK_H

#include "baseTask.h"

// Main Task

class MainTask : public BaseTask
{

public:
  MainTask(uint16_t stackSize, // class BaseTasks's arguments
           UBaseType_t priority,
           const char *taskName)
      : BaseTask{stackSize, priority, taskName}
  {
  }

  void main() override
  {
    // startup
    char buf[10];
    lcd.clear();
    // wait for discovery from serial
    unsigned long prevMillis;
    int pressReadLimit;
    for (;;)
    {
      // do stuff
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
          currentMenu = &mainMenu;
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
            // if everything else fails, switch to main menu
            if (currentMenu == &mainMenu)
            {
              // exit
              isMenuShown = false;
              lcd.clear();
              updatePage();
              mil2 = millis();
            }
            else
            {
              currentMenu = &mainMenu;
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
          if (buzzer_volume < 10)
          {
            buzzer_volume++;
          }
          buzzerVolumeChange_updateScreen();
          buzzer_playTestBeep();
          break;
        case -1:
          if (buzzer_volume > 0)
          {
            buzzer_volume--;
          }
          buzzerVolumeChange_updateScreen();
          buzzer_playTestBeep();
          break;
        }
        if (encoderButton.getSingleDebouncedPress())
        {
          // exit and go back to the menus
          isBuzzerVolumeChangeScreenShown = false;
          isMenuShown = true;
          lcd.clear();
          lcd_updateMenu();
        }
      }
      else
      {
        if (xQueueReceive(actionUpdateQueue, &(buf), 0))
        {
          if (strcmp(buf, "update") == 0)
          {
            updatePage();
            // wake the screen
            mil2 = millis();
          }
        }
        if (xQueueReceive(responseQueue, &(buf), 0))
        {
          // wake the screen
          mil2 = millis();
          lcd.setCursor(0, 2);
          lcd.print("                    ");
          mil = millis();
          if (buf[1] == '1')
          {
            buzzer_playSuccessfulBeep();
          }
          else
          {
            buzzer_playUnsuccessfulBeep();
          }
          switch (buf[0])
          {
          case '1':
            if (buf[1] == '1')
            {
              lcd.setCursor(2, 2);
              lcd.write(0);
            }
            else
            {
              lcd.setCursor(2, 2);
              lcd.print("x");
            }
            break;
          case '2':
            if (buf[1] == '1')
            {
              lcd.setCursor(9, 2);
              lcd.write(0);
            }
            else
            {
              lcd.setCursor(9, 2);
              lcd.print("x");
            }
            break;
          case '3':
            if (buf[1] == '1')
            {
              lcd.setCursor(16, 2);
              lcd.write(0);
            }
            else
            {
              lcd.setCursor(16, 2);
              lcd.print("x");
            }
            break;
          }
        }
        if (millis() - mil >= 1000)
        {
          lcd.setCursor(0, 2);
          lcd.print("                    ");
        }

        if (millis() - mil2 >= 5000)
        {
          lcd.noBacklight();
        }
        else
        {
          lcd.backlight();
        }

        switch ((int)encoder.getDirection())
        {
        case 1:
          if (pageNumber < maxPageNumber)
          {
            pageNumber++;
            updatePage();
          }
          // wake the screen
          mil2 = millis();
          break;
        case -1:
          if (pageNumber > 1)
          {
            pageNumber--;
            updatePage();
          }
          // wake the screen
          mil2 = millis();
          break;
        }
      }
    }
  }

protected:
  unsigned long mil;
  unsigned long mil2;
  //vibecoded code:
  void updatePage() {
    char text[21];
    sprintf(text, "Page %i/%i           ", pageNumber, maxPageNumber);
    lcd.setCursor(0, 0);
    lcd.print(text);

    // Clear the bottom line
    lcd.setCursor(0, 3);
    lcd.print("                    ");

    // Calculate the starting index for the current page
    // Page 1 -> index 0, Page 2 -> index 3, Page 3 -> index 6
    int baseIndex = (pageNumber - 1) * 3;

    // Left item
    lcd.setCursor(0, 3);
    lcd.print(actionNames[baseIndex]);

    // Center item
    int centerIndex = baseIndex + 1;
    int len = actionNames[centerIndex].length();
    lcd.setCursor((20 - len) / 2, 3);
    lcd.print(actionNames[centerIndex]);

    // Right item
    lcd.setCursor(14, 3);
    lcd.print(actionNames[baseIndex + 2]);
}
  void updateDisplay(String text)
  {
    int len = text.length();
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

#endif // MAIN_TASK_H