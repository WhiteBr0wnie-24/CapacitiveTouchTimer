#include "LedControl.h"
#include <CapacitiveSensor.h>
#include <avr/wdt.h>

#define TOUCH_SENSOR_THRESHOLD 55 // Note: Don't make these values too low! Otherwise, the long wires going from the ring PCB to the MCU interfere with one another.
#define CENTER_BUTTON_THRESHOLD 80
#define FINGER_DETECTION_COOLDOWN 125
#define CENTER_BUTTON_HELD_THRESHOLD 10
#define COUNTDOWN_DISPLAY_TIME 1000
#define COUNTDOWN_DP_TIME 500
#define COUNTDOWN_PAUSE_TIME 500
#define SECONDS_INCREASE_VALUE 5
#define LEFT_DISPLAY_NUMBER 2
#define RIGHT_DISPLAY_NUMBER 3
#define DEBUG 0 // 0 ... deactivate debug output (disables the serial console); 1 ... basic output; 2 ... verbose output (you must debug mode disable in the finished product!)
#define DEMO_MODE 0 // 0 ... regular operation; 1 ... Set the time to 2 minutes and 15 seconds and start in mode 3 (useful for testing the displays)
#define DEACTIVATE_AUTO_CALIBRATION 1 // See the setup() method for details
#define BEEPER_TONE_FREQUENCY 800 // in hertz
#define BEEPER_TONE_DURATION 500 // in milliseconds
#define BEEPER_PIN 4

LedControl lc = LedControl(7,5,6,1);

CapacitiveSensor cs_1 = CapacitiveSensor(13,12); // first pad in the ring
CapacitiveSensor cs_2 = CapacitiveSensor(9,8); 
CapacitiveSensor cs_3 = CapacitiveSensor(1,0);
CapacitiveSensor cs_4 = CapacitiveSensor(2,3);   // last pad in the ring
CapacitiveSensor cs_5 = CapacitiveSensor(11,10); // center button


// Change the internal mode of the clock by changing this variable
// 0 ... idle (default)
// 1 ... set minutes
// 2 ... set seconds
// 3 ... countdown
// 4 ... notify
int mode = 0;
int countDownState = 0; // Used for displaying the minutes and seconds in mode 3
int minutes = 0; // Only used when setting the minutes
int seconds = 0; // Only used when setting the seconds
int setTime = 0; // Once the time got set, the Arduino converts the set time (minutes + seconds) to total seconds and stores the result in this variable
long timerStartMillis = 0; // millis() value when the timer started
long lastDirectionDetection = 0; // millis() value when the detectFinger() method last checked the capacitive pads
long lastCountDownStateSwitch = 0; // millis() value when the last countdown mode switch occured (when displaying the minutes and seconds in mode 3).
long lastBeeperStateSwitch = 0;
int active_sensor = -1; // Currently active capacitive pad
int last_active_sensor = -1; // The last valid entry stored in the active_sensor variable
int center_button_active_for = 0; // A counter that determines how long (how many calls of the detectFinger() function) the user pressed the center button
bool center_button_held_detected = false;
bool beeper_on = false;

#if DEBUG > 1
int lastDir = 0;
#endif

// Function prototypes
int detectFinger(void);
void printModeSwitchMessage(int, int);
void displayValue(int);
void flashDPs(bool, bool);
void displayRemainingTime(int minutes, int seconds);

void setup()
{
#if DEACTIVATE_AUTO_CALIBRATION
  // You're free to deactivate the auto calibration (everything will still work)
  // However, I've found that the detection works more reliably with the auto calibration on
  // (It seems to help with de-bouncing)
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

#if DEMO_MODE
  timerStartMillis = millis();
  setTime = 20;
  mode = 3;
#endif

  // Initialize the beeper output pin
  pinMode(BEEPER_PIN, OUTPUT);
   
  lc.shutdown(0,false);
  lc.setIntensity(0,15);
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
  
  // Shut down the seven segment displays when the device is in idle mode or
  // in an invalid state
  // lc.shutdown(0, (mode < 1 || mode > 4));

  // Go back to the idle state when the user holds down the center button
  // This should happen regardless of the current state of the device
  if(action == 3)
  {
    flashDPs(false, false);
    mode = 0;
    printModeSwitchMessage(3, mode);
  }

  // Perform an action depending on the current state of the device
  switch(mode)
  {
    // Set minutes
    case 1:
    {
      if(action < 2)
      {
        // Count the currently set minutes up or down depending on the detected action
        // and make sure that the minutes stay in a valid range (between 0 and 59).
        minutes += action;
        minutes = (minutes < 0) ? 59 : minutes % 60;

        displayValue(minutes);

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
        // When the user presses the center button for a short time, switch to the next mode
        // Mode 2 = set seconds.
        mode = 2;
        printModeSwitchMessage(1, mode);
      }
    } break;

    // Set seconds
    case 2:
    {
      if(action < 2)
      {
        // Same procedure as with the minutes...
        
        seconds += action * SECONDS_INCREASE_VALUE;
        seconds = (seconds < 0) ? (60 - SECONDS_INCREASE_VALUE) : seconds % 60;

        displayValue(seconds);

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
        // Convert the chosen time to seconds and store the result in a variable
        // before switching to mode three when the user activates the center button
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
        // The time's up...
        mode = 4;
        printModeSwitchMessage(3, mode);
      }
      else
      {
        int remainingMinutes = remainingTime / 60;
        int remainingSeconds = remainingTime - (remainingMinutes * 60);
        
        displayRemainingTime(remainingMinutes, remainingSeconds);
      }
    } break;

    // Beeping
    case 4:
    {
      if(action == 2)
      {
        // Turn off the beeper before switching to another mode
        noTone(BEEPER_PIN);
        beeper_on = false;
        flashDPs(false, false);
        
        mode = 0;
        printModeSwitchMessage(4, mode);
      }
      else
      {
        if(millis() - lastBeeperStateSwitch > BEEPER_TONE_DURATION)
        {
          if(beeper_on)
          {
            flashDPs(true, false);
            noTone(BEEPER_PIN);
            beeper_on = false;
          }
          else
          {
            flashDPs(false, true);
            tone(BEEPER_PIN, BEEPER_TONE_FREQUENCY);
            beeper_on = true;
          }
          
          lastBeeperStateSwitch = millis();
        }
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

  // wdt_reset();
}

int detectFinger()
{
  int dir = 0;

  if(millis() - lastDirectionDetection > FINGER_DETECTION_COOLDOWN)
  {
    long total1 = (mode == 1 || mode == 2) ? cs_1.capacitiveSensor(32) : 0;
    long total2 = (mode == 1 || mode == 2) ? cs_2.capacitiveSensor(32) : 0;
    long total3 = (mode == 1 || mode == 2) ? cs_3.capacitiveSensor(32) : 0;
    long total4 = (mode == 1 || mode == 2) ? cs_4.capacitiveSensor(32) : 0;
    long total5 = cs_5.capacitiveSensor(32);

    // The user pressed the center button
    // prioritize this touch button over all other ones on the touch wheel
    // it's unlikely that the user activates this button by accident.
    // it is, however, likely that the user's finger gets close enough to the ring buttons
    // to falsely activate one of them when pressing the center button
    if(total5 > CENTER_BUTTON_THRESHOLD)
    {
      // This button is now activated once the user's finger moves away from the center button
      // before a certain amount of time has passed. This allows the software to detect both, a
      // short press and the user pressing the center button continuously.
      if(active_sensor != 5)
      {
        // Reset the counter and flag that determine whether the user holds down the button
        center_button_active_for = 0;
        center_button_held_detected = false;
      }
      else
      {
        // Increase a variable that counts how long the user holds down the center button
        // If that value gets greater than a certain threshold,
        // change the direction to three (center button held down).
        // return 0 if the counter is less than the threshold value.
        center_button_active_for += 1;
        dir = (center_button_active_for >= CENTER_BUTTON_HELD_THRESHOLD) ? 3 : 0;
      }
      
      active_sensor = 5;
    }
    // The following else/if blocks detect the other four ring buttons.
    // This is a quick and dirty solution, but it works for now...
    // else if(total1 > TOUCH_SENSOR_THRESHOLD || total2 > TOUCH_SENSOR_THRESHOLD)
    else if(total1 > TOUCH_SENSOR_THRESHOLD)
    {
      dir = 1;
      active_sensor = 1;
    }
    // else if(total3 > TOUCH_SENSOR_THRESHOLD || total4 > TOUCH_SENSOR_THRESHOLD)
    else if(total4 > TOUCH_SENSOR_THRESHOLD)
    {
      dir = -1;
      active_sensor = 3;
    }
    else
    {
      // This else gets activated once the user moves his/her finger away from any button
      // If the finger touched the center button before being moved away and the button
      // was not held down for too long, then return two (center button pressed for a short time).
      if(active_sensor == 5 && !center_button_held_detected)
        dir = 2;

      // No currently active button
      active_sensor = -1;
    }

#if DEBUG > 1
    if(last_active_sensor != active_sensor)
    {
      Serial.print("Active sensor = ");
      Serial.println(active_sensor);
    }
    if(dir == 3 && !center_button_held_detected)
    {
      Serial.println("Center button held down!");
    }
#endif

    // This is a second de-bounce measure. Checking the button state in too short intervals often leads
    // to imprecise measurements and false activations. Therefore, make sure to wait for a few milliseconds
    // before reading the state again. The next two lines set values for that mechanism.
    lastDirectionDetection = millis();
    last_active_sensor = active_sensor;

    // Only send the 'center button held down' return value once. If the direction is currently three and the
    // MCU detected that the user holds down the button, return 0.
    dir = (dir == 3 && center_button_held_detected) ? 0 : dir;

    // Update the center button held down detection flag.
    center_button_held_detected = center_button_held_detected || (dir == 3);

#if DEBUG > 1
  if(lastDir != dir)
  {
    Serial.print("Direction = ");
    Serial.println(dir);
    lastDir = dir;
  }
#endif
  }

  // Finally: return the detected direction value
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

void displayRemainingTime(int remainingMinutes, int remainingSeconds)
{
  // Show the current time on the displays
  // When the remaining minutes are greater then zero, first show the minutes,
  // then flash both DPs, then show the seconds, then flash the right DP, and then
  // wait for a while before repeating this process
  if(remainingMinutes > 0)
  {
    // Display the minutes
    if(countDownState == 0 && (millis() - lastCountDownStateSwitch > COUNTDOWN_PAUSE_TIME))
    {
      displayValue(remainingMinutes);
      lastCountDownStateSwitch = millis();
      countDownState = 1;

#if DEBUG > 0
      Serial.print(remainingMinutes);
#endif
    }
    // Flash both DPs
    else if(countDownState == 1 && (millis() - lastCountDownStateSwitch > COUNTDOWN_DISPLAY_TIME))
    {
      flashDPs(true, true);
      lastCountDownStateSwitch = millis();
      countDownState = 2;

#if DEBUG > 0
    Serial.print("..");
#endif
    }
    // Display the remaining seconds
    else if(countDownState == 2 && (millis() - lastCountDownStateSwitch > COUNTDOWN_DP_TIME))
    {
      displayValue(remainingSeconds);
      lastCountDownStateSwitch = millis();
      countDownState = 3;

#if DEBUG > 0
      Serial.print(remainingSeconds);
#endif
    }
    // Flash the right DP
    else if(countDownState == 3 && (millis() - lastCountDownStateSwitch > COUNTDOWN_DISPLAY_TIME))
    {
      flashDPs(false, true);
      lastCountDownStateSwitch = millis();
      countDownState = 4;
      
#if DEBUG > 0
    Serial.println(".");
#endif
    }
    // Turn the display off for a short time
    else if(countDownState == 4 && (millis() - lastCountDownStateSwitch > COUNTDOWN_DP_TIME))
    {
      countDownState = 0;
    }
  }
  else
  {
    // Constantly display the seconds when the remaining minutes are below zero
    if(millis() - lastCountDownStateSwitch > COUNTDOWN_PAUSE_TIME)
    {
      displayValue(remainingSeconds);
      lastCountDownStateSwitch = millis();
    }
  }
}

void displayValue(int value)
{
  int leftDisplay = value / 10;
  int rightDisplay = value - (leftDisplay * 10);

  if(leftDisplay > 0)
    lc.setDigit(0, LEFT_DISPLAY_NUMBER, leftDisplay, false);
  else
    lc.setChar(0, LEFT_DISPLAY_NUMBER, ' ', false);
    
  lc.setDigit(0, RIGHT_DISPLAY_NUMBER, rightDisplay, false);
}

void flashDPs(bool left, bool right)
{
  lc.setChar(0, LEFT_DISPLAY_NUMBER, ' ', left);
  lc.setChar(0, RIGHT_DISPLAY_NUMBER, ' ', right);
}
