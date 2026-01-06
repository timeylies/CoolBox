#include <Arduino.h>
#include "crc8.h"

class Cmd {

public:

  const static byte header = 0xCB;
  const static byte endMarker = 0x0A;

  enum commands {
    discovery = 0x00,     //pc sends this, arduino replies back with the same command telling us it's ready
    disconnect = 0x01,    //tell the arduino to stop sending out messages and idle
    acknowledge = 0x02,   //to make sure both pc and arduino recieved commands
    actionUpdate = 0x03,  //pc: sends this out for the names of the actions; arduino: sends this out to say a button has been pressed
  };

  void actionUpdateCommand(byte (&buffdataIn)[4], int actionNumber) {
    buffdataIn[0] = header;
    buffdataIn[1] = commands::actionUpdate;
    buffdataIn[2] = actionNumber;
    buffdataIn[3] = crc8(buffdataIn, 3);
    buffdataIn[4] = endMarker;
  }

  void discoveryResponseCommand(byte (&buffdataIn)[4]) {
    buffdataIn[0] = header;
    buffdataIn[1] = commands::discovery;
    buffdataIn[2] = 0x01; //to say yes
    buffdataIn[3] = crc8(buffdataIn, 3);
    buffdataIn[4] = endMarker;
  }

  void acknowledgeCommand(byte (&buffdataIn)[4]) {
    buffdataIn[0] = header;
    buffdataIn[1] = commands::acknowledge;
    buffdataIn[2] = 0x01;  // to say yes
    buffdataIn[3] = crc8(buffdataIn, 3);
    buffdataIn[4] = endMarker;
  }
  void disconnectCommand(byte (&buffdataIn)[4]) {
    buffdataIn[0] = header;
    buffdataIn[1] = commands::disconnect;
    buffdataIn[2] = 0x01;  // to say disconnect
    buffdataIn[3] = crc8(buffdataIn, 3);
    buffdataIn[4] = endMarker;
  }
};