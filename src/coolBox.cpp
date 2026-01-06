#include "config.h"
#include "startupTask.h"
#include "mainTask.h"

Startup startup(1024, 0, "Startup");
MainTask mainTask(2048, 0, "Main Task");

void sendAcknowledge() {
  //no need for a task (or atleast i think so...)
  byte buffer[4];
  cmd.acknowledgeCommand(buffer);
  Serial.write(buffer, sizeof(buffer) + 1);
}

void sendDiscoveryResponse() {
  //no need for a task (or atleast i think so...)
  byte buffer[4];
  cmd.discoveryResponseCommand(buffer);
  Serial.write(buffer, sizeof(buffer) + 1);
}

int count;
void sendActionUpdateTask(void *pvParam) {
  int action = (int)pvParam;
  isAck = false;
  count = 0;
  for (;;) {
    isActionUpdateRunning = true;
    if (count <= 5 && !isAck) {
      byte buffer[4];
      cmd.actionUpdateCommand(buffer, action);
      Serial.write(buffer, sizeof(buffer) + 1);
      count++;
      vTaskDelay(1 / portTICK_PERIOD_MS);
    } else {
      isActionUpdateRunning = false;
      isAck = true; //temporary fix
      vTaskDelete(NULL);
      //error out cuz count went over 5
    }
  }
}

void sendActionUpdate(int action) {
  if (!isActionUpdateRunning) {
    xTaskCreate(sendActionUpdateTask, "Send Action Update Task", 128, (void *)action, 0, NULL);
  }
}

void buttonTask(void *pvParam) {
  led_button1 = 255;
  led_button2 = 255;
  led_button3 = 255;
  int min_brightness = 5;
  for (;;) {
    if (isAck) {
      if (button1.getSingleDebouncedPress() && !button2.getSingleDebouncedPress() && !button3.getSingleDebouncedPress()) {
        led_button1 = 255;
        lastButtonPressed = 1;
        sendActionUpdate(0 + (pageNumber-1)*3);
      }
      if (button2.getSingleDebouncedPress() && !button1.getSingleDebouncedPress() && !button3.getSingleDebouncedPress()) {
        led_button2 = 255;
        lastButtonPressed = 2;
        sendActionUpdate(1 + (pageNumber-1)*3);
      }
      if (button3.getSingleDebouncedPress() && !button2.getSingleDebouncedPress() && !button1.getSingleDebouncedPress()) {
        led_button3 = 255;
        lastButtonPressed = 3;
        sendActionUpdate(2 + (pageNumber-1)*3);
      }
    }
    //do cool stuff with the lights
    if (led_button1 > min_brightness) {
      led_button1--;
    }
    if (led_button2 > min_brightness) {
      led_button2--;
    }
    if (led_button3 > min_brightness) {
      led_button3--;
    }
    if(led_button1 > 255 || led_button1 < 0){
      led_button1 = min_brightness;
    }
    if(led_button2 > 255 || led_button2 < 0){
      led_button2 = min_brightness;
    }
    if(led_button3 > 255 || led_button3 < 0){
      led_button3 = min_brightness;
    }
    update_buttonLeds();
    delay(3);
  }
}

void communicationTask(void *pvParam) {
  boolean newData = false;
  const byte numBytes = 100;
  byte data[numBytes];
  byte numReceived = 0;
  int StringCount = 0;

  for (;;) {
    //main part
    static boolean recvInProgress = false;
    static byte ndx = 0;
    byte startMarker = cmd.header;
    byte endMarker = cmd.endMarker;
    byte rb;
    int StringCount = 0;

    while (Serial.available() > 0 && newData == false) {
      rb = Serial.read();

      if (recvInProgress == true) {
        if (rb != endMarker) {
          data[ndx] = rb;
          ndx++;
          if (ndx >= numBytes) {
            ndx = numBytes - 1;
          }
        } else {
          data[ndx] = '\0';  // terminate the string
          recvInProgress = false;
          numReceived = ndx;  // save the number for use when printing
          ndx = 0;
          newData = true;
        }
      } else if (rb == startMarker) {
        recvInProgress = true;
        data[ndx] = startMarker;
        ndx++;
      }
    }

    if (newData == true) {
      newData = false;
      //process crc
      byte crc = crc8(data, numReceived - 1);
      if (data[numReceived - 1] == crc) {
        //crc is correct, continue
        switch (data[1]) {  //should be where the command byte is
          case cmd.commands::discovery:
            //respond
            sendDiscoveryResponse();
            //send queue out
            char buf[10];
            sprintf(buf, "discovery");
            xQueueSend(discoveryQueue, (void *)buf, (TickType_t)0);
            break;
          case cmd.commands::disconnect:
            //go to idle (or just reset)
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("    RESETTING...    ");
            resetFunc();
            break;
          case cmd.commands::acknowledge:
            //tell code to stop sending commands
            isAck = true;
            char buff[10];
            sprintf(buff, "%i%i", lastButtonPressed, data[2]);
            xQueueSend(responseQueue, (void *)buff, (TickType_t)0);
            break;
          case cmd.commands::actionUpdate:
            //update saved names
            String str = String((char *)data);
            //very stupid way of removing the first two characters
            char txt[60];
            str.toCharArray(txt, str.length(), 2);
            txt[strlen(txt) - 1] = '\0';
            str = String(txt);
            //Serial.print(str);
            //clear out the array
            memset(actionNames, 0, sizeof(actionNames));
            while (str.length() > 0) {
              int index = str.indexOf(';');
              if (index == -1)  // No space found
              {
                actionNames[StringCount++] = txt;
                break;
              } else {
                actionNames[StringCount++] = str.substring(0, index);
                str = str.substring(index + 1);
              }
            }
            sendAcknowledge();
            //send queue out
            char updateBuf[10];
            sprintf(updateBuf, "update");
            xQueueSend(actionUpdateQueue, (void *)updateBuf, (TickType_t)0);
            break;
        }
      }
    }
  }
}


void mainTaskQueueHandlerTask(void *pvParam) {
  //wait for queue
  char buf[10];
  for (;;) {
    if (xQueueReceive(mainTaskQueue, &(buf), 0)) {
      if (strcmp(buf, "init") == 0) {
        mainTask.init();
        //also start the button task
        xTaskCreate(buttonTask, "Button Task", 1048, NULL, 0, NULL);
        vTaskDelete(NULL);
      }
    }
    vTaskDelay(1);
  }
}


void setup() {
  Serial.begin(115200);
  initInputs();
  lcd_init(true);
  startup.init();
  startQueues();
  //start the main communication task
  xTaskCreate(communicationTask, "communicationTask", 1048, NULL, 0, NULL);
  //start the main task queue handler
  xTaskCreate(mainTaskQueueHandlerTask, "mainTaskQueueHandlerTask", 256, NULL, 0, NULL);
}

void loop() {
  //no need to use because of FreeRTOS
}
