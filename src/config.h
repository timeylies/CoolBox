#include <Arduino.h>
#include <avr/wdt.h>
#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <Tween.h>
#include <Wire.h>
#include <hd44780.h>                       // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header
#include <Pushbutton.h>
#include "commands.h"
#include "buttons.h"

// for resetting
void resetFunc();

// for coms
Cmd cmd;
QueueHandle_t discoveryQueue;    // responsible for switching from startup task to main task
QueueHandle_t mainTaskQueue;     // starts the main task
QueueHandle_t actionUpdateQueue; // for all of the short names
QueueHandle_t responseQueue;     // for the checkmarks
bool isAck = true;               // true if acknowledged, jus setting it true for now for the lights
bool isActionUpdateRunning = false;
String actionNames[30];
int pageNumber = 1;
int maxPageNumber = 9;
int lastButtonPressed;

void startQueues()
{
  // start up all queues here
  discoveryQueue = xQueueCreate(2, 10);
  mainTaskQueue = xQueueCreate(2, 10);
  actionUpdateQueue = xQueueCreate(2, 10);
  responseQueue = xQueueCreate(2, 10);
}

// Lcd
hd44780_I2Cexp lcd;
const int LCD_COLS = 20;
const int LCD_ROWS = 4;

byte checkChar[] = {
    0x00,
    0x00,
    0x01,
    0x03,
    0x16,
    0x1C,
    0x08,
    0x00};

void lcd_init(bool backlightOn)
{ // function to initialize the lcd
  int status = lcd.begin(LCD_COLS, LCD_ROWS);
  if (status) // non zero status means it was unsuccesful
  {
    // begin() failed

    Serial.print("LCD initalization failed: ");
    Serial.println(status);

    // blink error code using the onboard LED if possible
    hd44780::fatalError(status); // does not return
  }
  else
  { // to make sure it doesn't change anything if there's an error
    if (!backlightOn)
    {
      lcd.noBacklight(); // only changes if the boolean is false
      lcd.clear();
      lcd.print("      LCD OFF       ");
    }
    lcd.createChar(0, checkChar);
  }
}

int buzzer_volume = 5;
bool isBuzzerVolumeChangeScreenShown = false;
void buzzerVolumeChange_updateScreen()
{
  lcd.setCursor(0, 0);
  lcd.print("   Buzzer Volume   ");
  lcd.setCursor(0, 3);
  lcd.print("                    ");
  lcd.setCursor(9, 3);
  lcd.print(buzzer_volume);
}

/**Lcd Menu**/

bool isMenuShown = false;

// macro to calculate number of elements in an array
#define NUMELEMENTS(X) (sizeof(X) / sizeof(X[0]))

struct MENUITEM
{
  const char *title;         // item title
  bool (*action)(void *ptr); // function to execute
  void *param;               // parameter for function to execute
  struct MENU *subMenu;      // submenu for this menu item
};

struct MENU
{
  const char *title;       // menu title
  MENUITEM *items;         // menu items
  int numItems;            // number of menu items
  int selected;            // item that is selected
  struct MENU *parentMenu; // parent menu of this menu
};

// prototypes for all functions that can be called from the menus//
bool goToBuzzerVolumeChangeScreen(void *);
bool reset(void *);
bool disconnect(void *);

// Menu prototypes so compiler does not complain that it does not know the menu yet only menus that can be a parent menu need to be here; others are allowed
extern MENU startupMainMenu;
extern MENU mainMenu;

// Actual Menus

MENUITEM startupMainMenuItems[] = {
    {"Buzzer Volume      ", goToBuzzerVolumeChangeScreen, NULL, NULL},
    {"Exit               ", NULL, NULL, NULL},
};

MENUITEM mainMenuItems[] = {
    {"Buzzer Volume      ", goToBuzzerVolumeChangeScreen, NULL, NULL},
    {"Disconnect         ", disconnect, NULL, NULL},
    {"Exit               ", NULL, NULL, NULL},
};

MENU startupMainMenu = {
    "Startup Main menu", startupMainMenuItems, NUMELEMENTS(startupMainMenuItems), 0, NULL};

MENU mainMenu = {
    "Main menu", mainMenuItems, NUMELEMENTS(mainMenuItems), 0, NULL};

// variables to keep track of menu
MENU *currentMenu = &startupMainMenu;    // currently selected menu (by default startup but changes to main when in mainTask)
bool (*currentAction)(void *ptr) = NULL; // function to execute
void *currentParam = NULL;               // parameter for function to execute

void lcd_updateMenu()
{
  // 1. Calculate the 'top' item to display
  // This keeps the selected item within the 4-line window (this is vibecoded btw)
  static int topItemIndex = 0;

  if (currentMenu->selected < topItemIndex)
  {
    topItemIndex = currentMenu->selected; // Scroll up
  }
  else if (currentMenu->selected >= topItemIndex + 4)
  {
    topItemIndex = currentMenu->selected - 3; // Scroll down
  }

  // 2. Draw 4 lines
  for (int i = 0; i < 4; i++)
  {
    int itemToShow = topItemIndex + i;
    lcd.setCursor(0, i); // Move to the start of each row

    if (itemToShow < currentMenu->numItems)
    {
      // Print cursor if this is the selected item
      if (itemToShow == currentMenu->selected)
      {
        lcd.print("~");
      }
      else
      {
        lcd.print(" ");
      }

      // Print the menu title (up to 19 chars to leave room for cursor)
      lcd.print(currentMenu->items[itemToShow].title);

      // Optional: Clear remaining space on the line to prevent ghosting
      int len = strlen(currentMenu->items[itemToShow].title);
      for (int s = 0; s < (19 - len); s++)
        lcd.print(" ");
    }
    else
    {
      // Clear the line if there is no menu item to show
      lcd.print("                    "); // 20 spaces
    }
  }
}

bool goToBuzzerVolumeChangeScreen(void *)
{
  isMenuShown = false;
  lcd.clear();
  isBuzzerVolumeChangeScreenShown = true;
  buzzerVolumeChange_updateScreen();
  return true;
}

bool reset(void *)
{
  // go to idle (or just reset)
  resetFunc();
  return true;
}

bool disconnect(void *)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Requesting PC for ");
  lcd.setCursor(0, 1);
  lcd.print("   Disconnect...   ");
  delay(500);
  // request pc to disconnect, same as pressing the button on the ui on the pc
  byte buffer[4];
  cmd.disconnectCommand(buffer);
  Serial.write(buffer, sizeof(buffer) + 1);
  return true;
}

// buzzer

#define ENABLE_BUZZER

#ifdef ENABLE_BUZZER
#include <toneAC.h>
// there isn't actually 2 pins for the buzzer, we are going to be using toneAC so both pins are needed.
// this library uses pre defined pins so:
// Pins  9 & 10 - ATmega328, ATmega128, ATmega640, ATmega8, Uno, Leonardo, etc.
// Pins 11 & 12 - ATmega2560/2561, ATmega1280/1281, Mega
// Pins 12 & 13 - ATmega1284P, ATmega644
// Pins 14 & 15 - Teensy 2.0
// Pins 25 & 26 - Teensy++ 2.0

void buzzer_playSuccessfulBeepTask(void *pvParam)
{
  toneAC(1400, buzzer_volume, 15, false);
  vTaskDelete(NULL);
}

void buzzer_playSuccessfulBeep()
{
  xTaskCreate(buzzer_playSuccessfulBeepTask, "Buzzer Play Successful Beep Task", 256, NULL, 0, NULL);
}

void buzzer_playUnsuccessfulBeepTask(void *pvParam)
{
  toneAC(1000, buzzer_volume, 15, false);
  vTaskDelete(NULL);
}

void buzzer_playUnsuccessfulBeep()
{
  xTaskCreate(buzzer_playUnsuccessfulBeepTask, "Buzzer Play Unsuccessful Beep Task", 256, NULL, 0, NULL);
}

void buzzer_playStartupTask(void *pvParam)
{
  toneAC(600, buzzer_volume, 20, false);
  toneAC(800, buzzer_volume, 20, false);
  toneAC(1000, buzzer_volume, 20, false);
  toneAC(1200, buzzer_volume, 20, false);
  vTaskDelete(NULL);
}

void buzzer_playStartup()
{
  xTaskCreate(buzzer_playStartupTask, "Buzzer Play Startup Task", 256, NULL, 0, NULL);
}

void buzzer_playConnectedTask(void *pvParam)
{
  toneAC(900, buzzer_volume, 40, false);
  toneAC(1100, buzzer_volume, 40, false);
  vTaskDelete(NULL);
}

void buzzer_playConnected()
{
  xTaskCreate(buzzer_playConnectedTask, "Buzzer Play Connected Task", 256, NULL, 0, NULL);
}

void buzzer_playDisconnectedTask(void *pvParam)
{
  toneAC(1100, buzzer_volume, 40, false);
  toneAC(900, buzzer_volume, 40, false);
  vTaskDelete(NULL);
}

void buzzer_playDisconnected()
{
  xTaskCreate(buzzer_playDisconnectedTask, "Buzzer Play Connected Task", 256, NULL, 0, NULL);
}

void buzzer_playTestBeepTask(void *pvParam)
{
  toneAC(700, buzzer_volume, 50, true);
  vTaskDelete(NULL);
}

void buzzer_playTestBeep()
{
  xTaskCreate(buzzer_playTestBeepTask, "Buzzer Play Test Beep Task", 256, NULL, 0, NULL);
}

#else

void buzzer_playSuccessfulBeep() {};
void buzzer_playUnsuccessfulBeep() {};
void buzzer_playStartup() {};
void buzzer_playConnected() {};
void buzzer_playDisconnected() {};
void buzzer_playTestBeep() {};

#endif