#include <Arduino.h>
#include <avr/wdt.h>
#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <Tween.h>
#include <Wire.h>
#include <EEPROM.h>
#include <hd44780.h>                       // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header
#include <Pushbutton.h>
#include "commands.h"
#include "buttons.h"

// comment this out if you don't want the mic VU meter shown on the LCD
// (e.g. you're not using Voicemeeter, or just don't want it)
#define ENABLE_VU_METER

// for resetting
void resetFunc();

// for coms
Cmd cmd;
QueueHandle_t discoveryQueue;    // tells startup screen a PC connected
QueueHandle_t actionUpdateQueue; // for all of the short names
QueueHandle_t responseQueue;     // for the checkmarks
bool isAck = true;               // true if acknowledged, jus setting it true for now for the lights
bool isActionUpdateRunning = false;
// fixed-size buffers, not String[]: storage is static (no heap allocation),
// so repeated connect/disconnect cycles can't fragment the heap via this array.
// (memset-ing an array of String objects is also undefined behavior, since it
// overwrites their internal heap pointers without running destructors.)
char actionNames[30][20];
int pageNumber = 1;
int maxPageNumber = 9;
int lastButtonPressed;

// true once a PC has connected and the main page-browsing UI should be shown;
// false when disconnected (startup/idle screen shown instead).
// mainTask/buttonTask/startupTask all stay alive permanently and gate their
// behavior on this flag instead of being deleted/recreated on connect/disconnect,
// since repeated xTaskCreate/vTaskDelete cycles fragment the AVR's tiny heap.
volatile bool mainActive = false;

void startQueues()
{
  // start up all queues here
  discoveryQueue = xQueueCreate(2, 10);
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

#ifdef ENABLE_VU_METER
// VU bar glyphs: each character cell is 5 dots wide, so these give 5 sub-steps
// of horizontal fill resolution per column (slots 1-4 = 1..4 columns filled
// from the left; slot 5 = fully filled, used for whole "lit" columns).
byte barChar1[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
byte barChar2[] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18};
byte barChar3[] = {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C};
byte barChar4[] = {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E};
byte barCharFull[] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
#endif

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
#ifdef ENABLE_VU_METER
    lcd.createChar(1, barChar1);
    lcd.createChar(2, barChar2);
    lcd.createChar(3, barChar3);
    lcd.createChar(4, barChar4);
    lcd.createChar(5, barCharFull);
#endif
  }
}

#ifdef ENABLE_VU_METER
// latest mic L/R levels (0-255) from the PC; updated by communicationTask,
// drawn by mainTask. No locking: single-writer/single-reader of plain bytes.
volatile uint8_t micLevelL = 0;
volatile uint8_t micLevelR = 0;
volatile bool vuDataChanged = false;

const int VU_BAR_WIDTH = 18; // columns available after the 2-char "L:"/"R:" label
const int VU_BAR_STEPS = VU_BAR_WIDTH * 5; // 5 sub-steps per column

void lcd_drawVuBar(int row, const char *label, uint8_t level)
{
  int steps = ((int)level * VU_BAR_STEPS) / 255;
  int fullCols = steps / 5;
  int partial = steps % 5;
  if (fullCols > VU_BAR_WIDTH)
  {
    fullCols = VU_BAR_WIDTH;
    partial = 0;
  }

  lcd.setCursor(0, row);
  lcd.print(label);
  for (int i = 0; i < fullCols; i++)
  {
    lcd.write(5); // full block
  }
  if (fullCols < VU_BAR_WIDTH)
  {
    if (partial > 0)
    {
      lcd.write(partial); // partial-fill glyph (slots 1-4)
      fullCols++;
    }
    for (int i = fullCols; i < VU_BAR_WIDTH; i++)
    {
      lcd.print(" ");
    }
  }
}
#endif

// EEPROM survives both power cycles and firmware reflashes (avrdude doesn't
// touch it), so we use it to persist user settings like buzzer volume.
#define EEPROM_ADDR_BUZZER_VOLUME 0

int buzzer_volume = 5;
bool isBuzzerVolumeChangeScreenShown = false;

void buzzer_loadVolume()
{
  uint8_t stored = EEPROM.read(EEPROM_ADDR_BUZZER_VOLUME);
  if (stored <= 10)
  {
    buzzer_volume = stored;
  }
  // else: EEPROM was never written (reads 0xFF on a fresh chip) or holds a
  // bogus value, keep the default of 5
}

void buzzer_saveVolume()
{
  EEPROM.update(EEPROM_ADDR_BUZZER_VOLUME, (uint8_t)buzzer_volume);
}

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
  byte buffer[5];
  cmd.disconnectCommand(buffer);
  Serial.write(buffer, sizeof(buffer));
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

// these used to each spawn a short-lived FreeRTOS task just to call toneAC()
// and immediately self-delete; on this AVR heap (heap_3, backed by avr-libc
// malloc/free) repeated task create/delete cycles fragment the 8KB heap and
// eventually cause malloc failures, so we just call toneAC() directly instead.
void buzzer_playSuccessfulBeep()
{
  toneAC(1400, buzzer_volume, 15, false);
}

void buzzer_playUnsuccessfulBeep()
{
  toneAC(1000, buzzer_volume, 15, false);
}

void buzzer_playStartup()
{
  toneAC(600, buzzer_volume, 20, false);
  toneAC(800, buzzer_volume, 20, false);
  toneAC(1000, buzzer_volume, 20, false);
  toneAC(1200, buzzer_volume, 20, false);
}

void buzzer_playConnected()
{
  toneAC(900, buzzer_volume, 40, false);
  toneAC(1100, buzzer_volume, 40, false);
}

void buzzer_playDisconnected()
{
  toneAC(1100, buzzer_volume, 40, false);
  toneAC(900, buzzer_volume, 40, false);
}

void buzzer_playTestBeep()
{
  toneAC(700, buzzer_volume, 50, true);
}

#else

void buzzer_playSuccessfulBeep() {};
void buzzer_playUnsuccessfulBeep() {};
void buzzer_playStartup() {};
void buzzer_playConnected() {};
void buzzer_playDisconnected() {};
void buzzer_playTestBeep() {};

#endif