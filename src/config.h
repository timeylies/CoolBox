#include <Arduino.h>
#include <avr/wdt.h>
#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <Tween.h>
#include <Wire.h>
#include <hd44780.h>                        // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h>  // i2c expander i/o class header
#include <Pushbutton.h>
#include "commands.h"
#include "buttons.h"

//for resetting
void resetFunc() {
  wdt_disable();
  wdt_enable(WDTO_15MS);
  while (1) {}
}

//for coms
Cmd cmd;
QueueHandle_t discoveryQueue; //responsible for switching from startup task to main task
QueueHandle_t mainTaskQueue; //starts the main task
QueueHandle_t actionUpdateQueue; //for all of the short names
QueueHandle_t responseQueue; //for the checkmarks
bool isAck = true; //true if acknowledged, jus setting it true for now for the lights
bool isActionUpdateRunning = false;
String actionNames[10];
int pageNumber = 1;
int maxPageNumber = 3;
int lastButtonPressed;

void startQueues(){
  //start up all queues here
  discoveryQueue = xQueueCreate(2, 10);
  mainTaskQueue = xQueueCreate(2, 10);
  actionUpdateQueue = xQueueCreate(2, 10);
  responseQueue = xQueueCreate(2, 10);
}

//Lcd
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
  0x00
};

void lcd_init(bool backlightOn) {  //function to initialize the lcd
  int status = lcd.begin(LCD_COLS, LCD_ROWS);
  if (status)  // non zero status means it was unsuccesful
  {
    // begin() failed

    Serial.print("LCD initalization failed: ");
    Serial.println(status);

    // blink error code using the onboard LED if possible
    hd44780::fatalError(status);  // does not return
  } else {                        //to make sure it doesn't change anything if there's an error
    if (!backlightOn) {
      lcd.noBacklight();  //only changes if the boolean is false
      lcd.clear();
      lcd.print("      LCD OFF       ");
    }
    lcd.createChar(0, checkChar);
  }
}