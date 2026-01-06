#include <Arduino.h> 

#include <RotaryEncoder.h>

/* BUTTONS */


//Buttons
#define PIN_BUTTON1 49
#define PIN_BUTTON1LED 9
#define PIN_BUTTON2 51
#define PIN_BUTTON2LED 10
#define PIN_BUTTON3 53
#define PIN_BUTTON3LED 11

//Integers for the leds
int led_button1 = 0;
int led_button2 = 0;
int led_button3 = 0;

//Pushbutton Objects
Pushbutton button1(PIN_BUTTON1);
Pushbutton button2(PIN_BUTTON2);
Pushbutton button3(PIN_BUTTON3);

void init_buttons() {
  pinMode(PIN_BUTTON1LED, OUTPUT);
  pinMode(PIN_BUTTON2LED, OUTPUT);
  pinMode(PIN_BUTTON3LED, OUTPUT);
}

void update_buttonLeds() {
  analogWrite(PIN_BUTTON1LED, led_button1);
  analogWrite(PIN_BUTTON2LED, led_button2);
  analogWrite(PIN_BUTTON3LED, led_button3);
}

/* ROTARY ENCODER */
#define PIN_ENCODER_CLK 2
#define PIN_ENCODER_DT 3
#define PIN_ENCODER_BUTTON 4

RotaryEncoder encoder(PIN_ENCODER_CLK, PIN_ENCODER_DT, RotaryEncoder::LatchMode::FOUR3);
Pushbutton encoderButton(PIN_ENCODER_BUTTON);

void checkPosition()
{
  encoder.tick(); // just call tick() to check the state.
}

void initInputs() {
  init_buttons();
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_DT), checkPosition, CHANGE);
}