// I used this tutorial for ultrasonic sensor. The video is very helpful for understanding the basics.
// https://howtomechatronics.com/tutorials/arduino/ultrasonic-sensor-hc-sr04/
// However, the code that tutorial uses is inaccurate and more complex.

/*
  Communication with the ultrasonic sensor is done with the NewPing library

  Installation:
    Sketch -> Include Library -> Manage Libraries

  Docs:
    http://playground.arduino.cc/Code/NewPing
*/
#include <NewPing.h>
#define MAX_DISTANCE 1500 // This value is CM. IMPORTANT: If this value is too low, the sensors won't wait
// long enough for the ping to return, and the next iteration may recieve the ping. Ex: There isn't a car
// in the garage and the device starts to see random distances.
#define TRIGGER_PIN_A  12
#define ECHO_PIN_A     11
NewPing sonar_a(TRIGGER_PIN_A, ECHO_PIN_A, MAX_DISTANCE);
#define TRIGGER_PIN_B  10
#define ECHO_PIN_B     9
NewPing sonar_b(TRIGGER_PIN_B, ECHO_PIN_B, MAX_DISTANCE);

/*
  The runningMedian library is used for smoothing. The running median basically an array. When a new
  item is added, the oldest is removed. Methods exist for getting the median, max, min, etc.

  This library is not available in the IDE and has to be added manually. You should google manually
  adding arduino libraries for your OS. Usually it consists of downloading a git repository as a zip
  file and then extracting the folder into an arduino directory, inside the /libraries folder. (ex:
  arduino-1.8.5/libraries/<library name>)

  runningMedian:
    Documentation:  http://playground.arduino.cc/Main/RunningMedian
    Git Repo:       https://github.com/RobTillaart/Arduino/tree/master/libraries/RunningMedian

  Two running medians are used:
  name
    distance_running_median: stores individual sensor reading and products a reading used for
            determining the distance from the sensor.

    distance_history: stores the distances generated by the distance_running_median. Used to determine
            if there has been any recent movement.
*/
#include "RunningMedian.h"
RunningMedian distance_running_median = RunningMedian(15);
RunningMedian distance_history = RunningMedian(13);    // to the median of the long history to check
// for motion.

/*
  EEPROM is used for persistant storage. The Target is stored in EEPROM so
  that it doesn't reset on restart.

  Docs:
    https://www.arduino.cc/en/Reference/EEPROM
    https://www.arduino.cc/en/Tutorial/EEPROMWrite
*/
#include <EEPROM.h>
int target_addr = 0;

/*
  LED settings
*/
int green_led = 5;                      // Pin for the LED
int red_led = 6;                        // Pin for the LED
unsigned long last_blink = millis();
bool last_led_state = false;            // Stores the state of the LEDs pin (output pins cannot be read)
int min_blink_duration = 40;            // During parking,the distance from the target is mapped to min/max
int max_blink_duration = 1000;          // blink durations.
int blink_duration;                     // The determined duration will be stored here.
int target_distance;
int target_dead_zone = 6;

/*
  Set Target Button
*/
int set_target_pin = 3;                 // Pin for switch
int set_target_debounce_time = 1000;    // Debounce time in millis

/*
  Other variables
*/
int max_distance_in = 192;
int cur_distance = 100;
int history_interval = 333;       // invertval in millis in which an entry is added to history
unsigned long last_history_add = millis();
unsigned long last_motion_event = millis();
int motion_threshold_in = 4;      // Threshold for what is considered motion. If the device leaves standby
// too often, increase this.
int standby_after = 20000;        // After this much time with no motion above the threshold, the device will
// enter into standby mode.
int delay_between_pings = 8;      // A delay between pings is important to reduce noise in the sensor readings.
// Noise is likely due to the sensor recieving an echo from a previous ping. One library used 33ms for this
// value, however, I didn't see a difference between a 33 delay or 8ms delay. 5ms showed noise.


void setup() {
  // setup() is run once, at start

  // If the value at the address is 0, (the factory default for that address)
  // then write a default target distance in inches
  if (EEPROM.read(target_addr) == 0) {
    EEPROM.write(target_addr, 36);
  }

  // Read the target from memory
  target_distance = EEPROM.read(target_addr);

  // Setup the output pins and set their default status
  pinMode(green_led, OUTPUT);
  pinMode(red_led, OUTPUT);
  pinMode(set_target_pin, INPUT);
  digitalWrite(green_led, LOW);
  digitalWrite(red_led, LOW);

  Serial.begin(115200);
  Serial.println(target_distance);

}

void loop() {
  // The code here is looped until the device loses power.

  if (digitalRead(set_target_pin) == HIGH) {
    // If a button press is detected, debounce and then write the current distance as the target distance.

    // Uncomment for debugging
    // Serial.println("Button press detected");

    delay(set_target_debounce_time);

    if (digitalRead(set_target_pin) == HIGH) {
      // If its still high, read the current distance and write it to memory

      // Uncomment for debugging
      // Serial.println("Debounced");

      int a = sonar_a.convert_in(sonar_a.ping_median(11));
      delay(delay_between_pings);
      int b = sonar_b.convert_in(sonar_b.ping_median(11));
      cur_distance = simplify_distance(a, b);

      // Set the target to the current distance - 1, so that after the target is set, the light is green,
      // instead of blinking red.
      EEPROM.write(target_addr, cur_distance - 1);
      target_distance = EEPROM.read(target_addr);

      // Uncomment for debugging
      // Serial.print("New target: ");
      // Serial.println(target_distance);

      // Blink light to let user know its been set.
      digitalWrite(green_led, HIGH);
      digitalWrite(red_led, LOW);
      delay(750);
      digitalWrite(green_led, LOW);
      digitalWrite(red_led, HIGH);
      delay(750);
      digitalWrite(green_led, LOW);
      digitalWrite(red_led, LOW);
      delay(2000);
    }

  }

  if (last_motion_event + standby_after < millis()) {

    // Check if there has been motion recently, if there has not, turn off the LEDs and then
    // check for motion once per history inverval and set last_motion_event if there has been
    // motion.

    digitalWrite(green_led, LOW);
    digitalWrite(red_led, LOW);

    // Get sensor data to put into the running median.

    // In this case blocking pings isn't an issue since the LED blinks aren't being set, so we can use
    // sonar's median method. Do three iterations, returning the median then converting to inches.
    int a = sonar_a.convert_in(sonar_a.ping_median(5));
    delay(delay_between_pings);
    int b = sonar_b.convert_in(sonar_b.ping_median(5));
    cur_distance = simplify_distance(a, b);
    distance_history.add(cur_distance);

    // Uncomment for debugging
    // Serial.print("cur_distance:");
    // Serial.print(cur_distance);
    // Serial.print(", a:");
    // Serial.print(a);
    // Serial.print(", b:");
    // Serial.println(b);

    if (distance_history.getHighest() - distance_history.getLowest() > motion_threshold_in) {

      // If there has been a recent event, set the last_motion_variable.
      last_motion_event = millis();

      // Uncomment for debugging
      // Serial.print("Motion sensed, leaving standby");

    }

    // Sleep for a second
    delay(history_interval);

  } else {

    // Get the distance and update the LEDs

    // pinging is a blocking function that can take time especially if the object is far,
    // and device timesout. To keep the LEDs accurate, they must be processed often. A delay
    // between each reading is very important. Without it the sensors are very noisy. This
    // could be because the sensor is recieving an echo of the previous sensor's ping.
    handle_output(cur_distance);
    int a = sonar_a.ping_in();
    delay(delay_between_pings);
    handle_output(cur_distance);
    int b = sonar_b.ping_in();
    delay(delay_between_pings);

    // Add the cur_distance to the running median, then get a distance to send to the output
    cur_distance = simplify_distance(a, b);
    distance_running_median.add(cur_distance);
    int median_distance = distance_running_median.getMedian();
    handle_output(median_distance);

    // Uncomment for debugging
    // Serial.print("median_distance:");
    // Serial.print(median_distance);
    // Serial.print(", a:");
    // Serial.print(a);
    // Serial.print(", b:");
    // Serial.println(b);

    if (last_history_add + history_interval < millis()) {

      // Add an entry to the history on every interval,reset the interval time stamp, and then
      // check if the device should enter standby.
      distance_history.add(median_distance);
      last_history_add = millis();

      if (distance_history.getHighest() - distance_history.getLowest() > motion_threshold_in) {

        // If there has been a recent event, set the last_motion_variable.
        last_motion_event = millis();

        // Uncomment for debugging
        // Serial.print("Motion Sensed: ");
        // Serial.println(distance_history.getHighest() - distance_history.getLowest());

      }

    }

  }

}

int simplify_distance(int a, int b) {
  // Takes two sensor readings and returns a single number.

  int distance;

  if (a != 0 && b != 0) {

    // If the sensor is unable to get a reading (times out) then it returns 0. Since
    // neither are zero, send the closest.
    distance = min(a, b);

  } else {

    // At this point, at least one of the numbers is 0. If one is not zero (ex: one
    // sensor read and the other failed to read), it should be returned. Otherwise, 0
    // should be returned. By returning the max, we accomplish both tasks.
    distance = max(a, b);

  }

  if (distance == 0) {

    // If the distance is 0, the sensor timed out. For our purposes we can set this to
    // the max distance
    distance = max_distance_in;

  }

  return distance;

}

void handle_output(int distance) {
  int led;
  int led_off;

  if (distance <= target_distance) {

    // The target is closer than the target
    led = red_led;
    led_off = green_led;

    // The interval is the same as the duration.
    blink_duration = min_blink_duration;

  } else if (distance <= target_distance + target_dead_zone) {

    // Target is in the dead zone, the light should be solid.
    blink_duration = 0; // a duration of zero is shorthand to keep the LED on, w/o blinking
    led = green_led;
    led_off = red_led;

  } else {

    // The target is farther than the target
    led = green_led;
    led_off = red_led;

    // The interval is a function of the min/max durations and distance to the front edge of
    // the target (target & deadzone)
    // map(value, fromLow, fromHigh, toLow, toHigh)
    blink_duration = map(distance - target_distance - target_dead_zone, 0, max_distance_in, min_blink_duration, max_blink_duration);

  }

  // Now that we know the LEDs and duration, handle the LED outputs.

  // In order to not be blocking, we check the current time against the last
  // blink and interval, then set the LED outputs accordingly.

  if (blink_duration == 0) {

    // a duration of zero is shorthand to keep the LED on, w/o blinking
    digitalWrite(led, HIGH);
    digitalWrite(led_off, LOW);

  } else if (last_blink + blink_duration < millis() ) {

    // The minimum required duration is has been surpassed, toggle the LED
    last_led_state = !last_led_state;
    digitalWrite(led, last_led_state);
    digitalWrite(led_off, LOW);
    last_blink = millis();

  }

}

