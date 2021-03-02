#include "LedControl.h"
#include <CapacitiveSensor.h>

#define TOUCH_SENSOR_THRESHOLD 120 // Note: Don't make these values too low! Otherwise, the long wires going from the ring PCB to the MCU interfere with one another. possible improvement: a small controller on the ring pcb does the decoding and sends a value to the MCU.
#define CENTER_BUTTON_THRESHOLD 150
#define FINGER_DETECTION_COOLDOWN 125
#define SECONDS_INCREASE_VALUE 5
#define LEFT_DISPLAY_NUMBER 0
#define RIGHT_DISPLAY_NUMBER 4
#define DEBUG 2
#define DEACTIVATE_AUTO_CALIBRATION 0

LedControl lc = LedControl(7,5,6,1);

CapacitiveSensor cs_1 = CapacitiveSensor(8,9); // first pad in the ring
CapacitiveSensor cs_2 = CapacitiveSensor(6,7);
CapacitiveSensor cs_3 = CapacitiveSensor(4,5);
CapacitiveSensor cs_4 = CapacitiveSensor(2,3); // last pad in the ring
CapacitiveSensor cs_5 = CapacitiveSensor(10,11); // center button

// Change the internal mode of the clock by changing this variable
// 0 ... idle (default)
// 1 ... set minutes
// 2 ... set seconds
// 3 ... countdown
// 4 ... notify
int mode = 0;
int minutes = 0; // Only used when setting the minutes
int seconds = 0; // Only used when setting the seconds
int setTime = 0; // Once the time got set, the Arduino converts the set time (minutes + seconds) to total seconds and stores the result in this variable
long timerStartMillis = 0; // millis() value when the timer started
long lastDirectionDetection = 0;
int active_sensor = -1;
int last_active_sensor = -1;

// Function prototypes
int detectFinger(void);
void printModeSwitchMessage(int, int);

void setup()
{
#if DEACTIVATE_AUTO_CALIBRATION
  cs_1.set_CS_AutocaL_Millis(0xFFFFFFFF);
  cs_2.set_CS_AutocaL_Millis(0xFFFFFFFF);
  cs_3.set_CS_AutocaL_Millis(0xFFFFFFFF);
  cs_4.set_CS_AutocaL_Millis(0xFFFFFFFF);
  cs_5.set_CS_AutocaL_Millis(0xFFFFFFFF);
#endif

#if DEBUG > 0
  Serial.begin(9600);
  Serial.print("Started with debug level ");
  Serial.println(DEBUG);
#endif
   
  lc.shutdown(0,false);
  lc.setIntensity(0,4);
  lc.clearDisplay(0);
}

void loop()
{
  // Determine the action that the user performed.
  // 0  = no action
  // 1  = count up
  // -1 = count down
  // 2  = center button pressed
  // 3  = center button held down
  int action = detectFinger();

  // Shut down the seven segment displays when the device is in its idle mode or
  // in an invalid state
  lc.shutdown(0, (mode < 1 || mode > 4));
  lc.clearDisplay(0);

  // Perform an action depending on the current state of the device
  switch(mode)
  {
    // Set minutes
    case 1:
    {
      if(action < 2)
      {
        minutes += action;
        minutes = (minutes < 0) ? 59 : minutes % 60;

        int leftDisplay = minutes / 10;
        int rightDisplay = minutes - (leftDisplay * 10);

        if(leftDisplay > 0)
          lc.setDigit(0, LEFT_DISPLAY_NUMBER, leftDisplay, false);
        lc.setDigit(0, RIGHT_DISPLAY_NUMBER, rightDisplay, false);

#if DEBUG > 0
        if(action != 0)
        {
          Serial.print("Set minutes to ");
          Serial.println(minutes);
        }
#endif
      }
      else
      {
        mode = 2;
        printModeSwitchMessage(1, mode);
      }
    } break;

    // Set seconds
    case 2:
    {
      if(action < 2)
      {
        seconds += action * SECONDS_INCREASE_VALUE;
        seconds = (seconds < 0) ? (60 - SECONDS_INCREASE_VALUE) : seconds % 60;

        int leftDisplay = seconds / 10;
        int rightDisplay = seconds - (leftDisplay * 10);

        if(leftDisplay > 0)
          lc.setDigit(0, LEFT_DISPLAY_NUMBER, leftDisplay, false);
        lc.setDigit(0, RIGHT_DISPLAY_NUMBER, rightDisplay, false);

#if DEBUG > 0
        if(action != 0)
        {
          Serial.print("Set seconds to ");
          Serial.println(seconds);
        }
#endif
      }
      else
      {
        timerStartMillis = millis();
        setTime = minutes * 60 + seconds;
        mode = 3;
        printModeSwitchMessage(2, mode);
      }
    } break;

// Countdown
    case 3:
    {
      long secondsPassed = (millis() - timerStartMillis) / 1000L;
      long remainingTime = setTime - secondsPassed;
      
      if(remainingTime <= 0)
      {
        mode = 4;
        printModeSwitchMessage(3, mode);
      }
      else
      {
        int remainingMinutes = remainingTime / 60;
        int remainingSeconds = remainingTime - (remainingMinutes * 60);

        // TODO: Display the minutes and the seconds...

#if DEBUG > 0
        Serial.print(remainingMinutes);
        Serial.print(" minutes, ");
        Serial.print(remainingSeconds);
        Serial.println(" seconds");
#endif
      }
    } break;

    // Beeping
    case 4:
    {
      if(action == 2)
      {
        mode = 0;
        printModeSwitchMessage(4, mode);
      }
      else
      {
        // TODO: Implement beeping ...
#if DEBUG > 0
        Serial.println("BEEP!");
#endif
      }
    } break;

    // Idle & Error states
    default:
    {
      if(action == 2)
      {
        mode = 1;
        printModeSwitchMessage(0, mode);
      }
    } break;
  }
}

int detectFinger()
{
  int dir = 0;

  if(millis() - lastDirectionDetection > FINGER_DETECTION_COOLDOWN)
  {
    long total1 = cs_1.capacitiveSensor(30);
    long total2 = cs_2.capacitiveSensor(30);
    long total3 = cs_3.capacitiveSensor(30);
    long total4 = cs_4.capacitiveSensor(30);
    long total5 = cs_5.capacitiveSensor(30);
  
    // The user pressed the center button
    // prioritize this touch button over all other ones on the touch wheel
    // it's extremely unlikely that the user activates this button by accident.
    // it is, however, likely that the user's finger gets close enough to the edge
    // to falsely activate one of the ring buttons when pressing the center button
    if(total5 > CENTER_BUTTON_THRESHOLD)
    {
      // Make sure to only activate the button once (somewhat of a de-bounce measure)
      if(active_sensor != 5)
        dir = 2;
      
      active_sensor = 5;
    }
    else if(total1 > TOUCH_SENSOR_THRESHOLD)
    {
      if(active_sensor == 2 || (active_sensor == -1 && last_active_sensor == 2))
        dir = -1;
  
      if(active_sensor == 4 || (active_sensor == -1 && last_active_sensor == 4))
        dir = 1;
  
      active_sensor = 1;
    }
    else if(total2 > TOUCH_SENSOR_THRESHOLD)
    {
      if(active_sensor == 1 || (active_sensor == -1 && last_active_sensor == 1))
        dir = 1;
  
      if(active_sensor == 3 || (active_sensor == -1 && last_active_sensor == 3))
        dir = -1;
  
      active_sensor = 2;
    }
    else if(total3 > TOUCH_SENSOR_THRESHOLD)
    {
      if(active_sensor == 2 || (active_sensor == -1 && last_active_sensor == 2))
        dir = 1;
  
      if(active_sensor == 4 || (active_sensor == -1 && last_active_sensor == 4))
        dir = -1;
  
      active_sensor = 3;
    }
    else if(total4 > TOUCH_SENSOR_THRESHOLD)
    {
      if(active_sensor == 3 || (active_sensor == -1 && last_active_sensor == 3))
        dir = 1;
  
      if(active_sensor == 1 || (active_sensor == -1 && last_active_sensor == 1))
        dir = -1;
  
      active_sensor = 4;
    }
    else
      active_sensor = -1; // No finger detected

#if DEBUG > 1
    if(last_active_sensor != active_sensor)
    {
      Serial.print("Active sensor = ");
      Serial.println(active_sensor);
    }
#endif

    lastDirectionDetection = millis();
    last_active_sensor = active_sensor;
  }

  return dir;
}

void printModeSwitchMessage(int o, int n)
{
#if DEBUG > 0
  Serial.print("Switched from mode ");
  Serial.print(o);
  Serial.print(" to mode ");
  Serial.println(n);
#endif
}
