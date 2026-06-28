/*   COOL BOX PROJECT
 * Look in buttons.h to define your pins for your buttons and leds
 * the LCD works through standard I2C so just connect it to the I2C pins on your arduino
 * the buzzer (passive) can be enabled and pins can be defined in config.h
 */

#include "config.h"
#include "startupTask.h"
#include "mainTask.h"

Startup startup(512, 0, "Startup");
MainTask mainTask(512, 0, "Main Task");
TaskHandle_t buttonTaskHandle;

void sendAcknowledge()
{
  // no need for a task (or atleast i think so...)
  byte buffer[5];
  cmd.acknowledgeCommand(buffer);
  Serial.write(buffer, sizeof(buffer));
}

void sendDiscoveryResponse()
{
  // no need for a task (or atleast i think so...)
  byte buffer[5];
  cmd.discoveryResponseCommand(buffer);
  Serial.write(buffer, sizeof(buffer));
}

// permanent task (created once in setup()) that resends an action-update
// command up to 5 times until acknowledged. Used to spawn a fresh short-lived
// task per button press, which fragmented the heap the same way the buzzer
// tasks and connect/disconnect task churn did.
volatile int pendingAction = -1;

void sendActionUpdateTask(void *pvParam)
{
  for (;;)
  {
    if (pendingAction < 0)
    {
      vTaskDelay(5);
      continue;
    }
    int action = pendingAction;
    isAck = false;
    isActionUpdateRunning = true;
    for (int count = 0; count <= 5 && !isAck; count++)
    {
      byte buffer[5];
      cmd.actionUpdateCommand(buffer, action);
      Serial.write(buffer, sizeof(buffer));
      // give the PC real time to run the action and write back an ack before
      // resending; 1ms was far too fast and caused duplicate sends/executions
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    isAck = true; // temporary fix: stop retrying even if never acknowledged
    isActionUpdateRunning = false;
    pendingAction = -1;
  }
}

void sendActionUpdate(int action)
{
  if (!isActionUpdateRunning)
  {
    pendingAction = action;
  }
}

void buttonTask(void *pvParam)
{
  int min_brightness = 5;
  bool wasActive = false;
  for (;;)
  {
    if (!mainActive)
    {
      if (wasActive)
      {
        led_button1 = 0;
        led_button2 = 0;
        led_button3 = 0;
        update_buttonLeds();
        wasActive = false;
      }
      vTaskDelay(50);
      continue;
    }
    if (!wasActive)
    {
      led_button1 = 255;
      led_button2 = 255;
      led_button3 = 255;
      wasActive = true;
    }
    if (isAck)
    {
      bool b1 = button1.getSingleDebouncedPress();
      bool b2 = button2.getSingleDebouncedPress();
      bool b3 = button3.getSingleDebouncedPress();

      if (b1 && !b2 && !b3)
      {
        led_button1 = 255;
        lastButtonPressed = 1;
        sendActionUpdate(0 + (pageNumber - 1) * 3);
      }
      else if (b2 && !b1 && !b3)
      {
        led_button2 = 255;
        lastButtonPressed = 2;
        sendActionUpdate(1 + (pageNumber - 1) * 3);
      }
      else if (b3 && !b1 && !b2)
      {
        led_button3 = 255;
        lastButtonPressed = 3;
        sendActionUpdate(2 + (pageNumber - 1) * 3);
      }
    }
    // do cool stuff with the lights
    if (led_button1 > min_brightness)
    {
      led_button1--;
    }
    if (led_button2 > min_brightness)
    {
      led_button2--;
    }
    if (led_button3 > min_brightness)
    {
      led_button3--;
    }
    update_buttonLeds();
    delay(5);
  }
}

void communicationTask(void *pvParam)
{
  boolean newData = false;
  const byte numBytes = 220;
  byte data[numBytes];
  byte numReceived = 0;

  for (;;)
  {
    // main part
    static boolean recvInProgress = false;
    static byte ndx = 0;
    byte startMarker = cmd.header;
    byte endMarker = cmd.endMarker;
    byte rb;
    int StringCount = 0;

    bool readAny = false;
    while (Serial.available() > 0 && newData == false)
    {
      readAny = true;
      rb = Serial.read();

      if (recvInProgress == true)
      {
        if (rb != endMarker)
        {
          data[ndx] = rb;
          ndx++;
          if (ndx >= numBytes)
          {
            ndx = numBytes - 1;
          }
        }
        else
        {
          data[ndx] = '\0'; // terminate the string
          recvInProgress = false;
          numReceived = ndx; // save the number for use when printing
          ndx = 0;
          newData = true;
        }
      }
      else if (rb == startMarker)
      {
        recvInProgress = true;
        data[ndx] = startMarker;
        ndx++;
      }
    }

    if (newData == true)
    {
      newData = false;
      // process crc
      byte crc = crc8(data, numReceived - 1);
      if (data[numReceived - 1] == crc)
      {
        // crc is correct, continue
        switch (data[1])
        { // should be where the command byte is
        case cmd.commands::discovery:
        {
          // respond
          sendDiscoveryResponse();
          // send queue out
          char buf[10];
          sprintf(buf, "discovery");
          xQueueSend(discoveryQueue, (void *)buf, (TickType_t)0);
          break;
        }
        case cmd.commands::disconnect:
        {
          // go to idle (or just reset)
          buzzer_playDisconnected();
          resetFunc();
          break;
        }
        case cmd.commands::acknowledge:
        {
          // tell code to stop sending commands
          isAck = true;
          char buff[10];
          sprintf(buff, "%i%i", lastButtonPressed, data[2]);
          xQueueSend(responseQueue, (void *)buff, (TickType_t)0);
          break;
        }
        case cmd.commands::vuUpdate:
        {
#ifdef ENABLE_VU_METER
          // fire-and-forget: no ack, mainTask just redraws with whatever's freshest
          if (numReceived >= 4)
          {
            micLevelL = data[2];
            micLevelR = data[3];
            vuDataChanged = true;
          }
#endif
          break;
        }
        case cmd.commands::actionUpdate:
        {
          // payload is data[2 .. numReceived-2]; data[numReceived-1] is the crc byte
          const int maxNameLen = sizeof(actionNames[0]) - 1; // 19, leaves room for '\0'
          for (int i = 0; i < 30; i++) actionNames[i][0] = '\0';
          int tokenStart = 2;
          int payloadEnd = numReceived - 1;
          for (int i = 2; i < payloadEnd && StringCount < 30; i++)
          {
            if (data[i] == ';')
            {
              int len = i - tokenStart;
              if (len > maxNameLen) len = maxNameLen;
              memcpy(actionNames[StringCount], &data[tokenStart], len);
              actionNames[StringCount][len] = '\0';
              StringCount++;
              tokenStart = i + 1;
            }
          }
          if (tokenStart < payloadEnd && StringCount < 30)
          {
            int len = payloadEnd - tokenStart;
            if (len > maxNameLen) len = maxNameLen;
            memcpy(actionNames[StringCount], &data[tokenStart], len);
            actionNames[StringCount][len] = '\0';
            StringCount++;
          }
          sendAcknowledge();
          // send queue out
          char updateBuf[10];
          sprintf(updateBuf, "update");
          xQueueSend(actionUpdateQueue, (void *)updateBuf, (TickType_t)0);
          break;
        }
        }
      }
    }
    if (!readAny)
    {
      vTaskDelay(1);
    }
  }
}

void errorTask(void *param)
{
  const char *error = (const char *)param;
  unsigned long mil;
  bool state = 1;
  noToneAC();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("-------ERROR!-------");
  for (;;)
  {
    if (xTaskGetTickCount() - mil > 2000 / portTICK_PERIOD_MS)
    {
      if (state)
      {
        state = 0;
      }
      else
      {
        state = 1;
      }
      mil = xTaskGetTickCount();
    }
    if (state)
    {
      lcd.setCursor(0, 2);
      lcd.print(error);
      led_button1 = 255;
      led_button2 = 255;
      led_button3 = 255;
    }
    else
    {
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      led_button1 = 0;
      led_button2 = 0;
      led_button3 = 0;
    }
    update_buttonLeds();
  }
}

void vApplicationMallocFailedHook(void)
{
  lcd.clear();
  xTaskCreate(errorTask, "Error", 128, (void *)"    Malloc Fail!    ", 1, NULL);
}
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  Serial.print("Stack overflow in task: ");
  Serial.print(pcTaskName);
  taskDISABLE_INTERRUPTS();
  vTaskSuspendAll();
}

void setup()
{
  Serial.begin(115200);
  initInputs();
  buzzer_loadVolume();
  lcd_init(true);
  buzzer_playStartup();
  startQueues();
  // all long-lived tasks are created exactly once here and stay alive for the
  // lifetime of the program; they gate their behavior on `mainActive` instead
  // of being deleted/recreated on connect/disconnect (see config.h)
  startup.init();
  mainTask.init();
  xTaskCreate(buttonTask, "Button Task", 1048, NULL, 0, &buttonTaskHandle);
  xTaskCreate(communicationTask, "communicationTask", 2048, NULL, 0, NULL);
  xTaskCreate(sendActionUpdateTask, "Send Action Update Task", 256, NULL, 0, NULL);
}

void loop()
{
  // no need to use because of FreeRTOS
}

// for resetting or going back to idle
void resetFunc()
{
  /*
  wdt_disable();
  wdt_enable(WDTO_15MS);
  while(1);
  */
  // trying to full on reset proved to be unreliable so we just go back to idle.
  // mainTask/buttonTask/startupTask stay alive permanently (see config.h); flipping
  // mainActive off hands the screen back to startupTask without deleting/recreating
  // any tasks, avoiding the heap fragmentation that caused malloc failures on reconnect.
  mainActive = false;
  memset(actionNames, 0, sizeof(actionNames)); // clear the saved action names
  xQueueReset(discoveryQueue);                 // reset all queues
  xQueueReset(actionUpdateQueue);
  xQueueReset(responseQueue);
  isMenuShown = false; // make sure menu isn't shown
}
