#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/sleep.h>
#include <ds3231.h>
#include <EEPROM.h>

#define DEBUG true

#define DISPLAY_SWITCH  7
#define WAKE_PIN        3
#define MOT_1           5
#define MOT_2           6
#define RTC_PWR         4


#define MAX_PULSE_WIDTH 250
#define MIN_PULSE_WIDTH 150
#define FORWARD         0
#define BACK            1

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define CLK 12
#define DT 11
#define SW 2

int interval = 60; // default 60 minutes
int sleepTime;    // interval in seconds
int eepromIntervalRemainder  = 511;
int eepromIntervalMultiplier = 510;
int eepromInterval = 509;

int mixDuration = 3; // default 3 minutes of mixing
int mixTime; // mix duration in seconds
int eepromMixDuration = 1;

int dxn = 0; // 0 for ccw,1 for cw
int rotationSpeed = 1;
int eepromSpeed = 2;

#define MODE_SPIN     1
#define MODE_CHURN_A  2
#define MODE_CHURN_B  3
int mixMode = MODE_SPIN;
int eepromMixMode = 3;

int currentStateCLK;
int lastStateCLK;
unsigned long lastButtonPress = 0;

uint8_t wake_DAY;
uint8_t wake_HOUR;
uint8_t wake_MINUTE;
uint8_t wake_SECOND;
#define BUFF_MAX 256

char buff[BUFF_MAX];

struct ts t;

#define img_width 16
#define img_height 16

static const unsigned char PROGMEM fish_bmp[] =
{ 0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00111110, 0b00000000,
  0b01111111, 0b00000001,
  0b11111111, 0b10000011,
  0b11111111, 0b11000111,
  0b11011111, 0b11101111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11101111,
  0b11111111, 0b11000111,
  0b11111111, 0b10000011,
  0b01111111, 0b00000001,
  0b00111110, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000
};

// ##################################################################################################

// // For a button interrupt
void wakeUp()
{
  //Handler for pin interrupt
  detachInterrupt(digitalPinToInterrupt(WAKE_PIN));

}

// ##################################################################################################

// Spin the head in one direction
void spinCycle(long seconds)
{
  digitalWrite(MOT_1, LOW);
  digitalWrite(MOT_2, LOW);

  int pwm = ((MAX_PULSE_WIDTH - MIN_PULSE_WIDTH) * (rotationSpeed / 10)) + MIN_PULSE_WIDTH;

  DS3231_get(&t);
  long currentTime = t.sec + t.min * 60 + t.hour * 60 * 60; // get the current time in seconds
  long targetTime  = currentTime + seconds;           // set the terget time in seconds

  // spinning CW
  while (currentTime < targetTime)
  {
    if (millis() % 500 == 0)
    {
      DS3231_get(&t);
      currentTime = t.sec + t.min * 60 + t.hour * 60 * 60;
    }
    {
      digitalWrite(MOT_1, LOW);
      if (millis() % 500)
        analogWrite(MOT_2, MAX_PULSE_WIDTH);
      if (millis() % 750)
        analogWrite(MOT_2, pwm);
    }
  }
  digitalWrite(MOT_1, LOW);
  digitalWrite(MOT_2, LOW);
}

// ##################################################################################################

// Pulse the head back and forth, but still spinning in one direction
void churnCycleA(long seconds)
{
  // counting scale is to 20,000
  int counter = 0;

  digitalWrite(MOT_1, LOW);
  digitalWrite(MOT_2, LOW);
  int pwm = ((MAX_PULSE_WIDTH - MIN_PULSE_WIDTH) * (rotationSpeed / 10)) + MIN_PULSE_WIDTH;

  DS3231_get(&t);
  long currentTime = t.sec + t.min * 60 + t.hour * 60 * 60; // get the current time in seconds
  long targetTime  = currentTime + seconds;           // set the terget time in seconds

  // spinning CW, pulsing CCW periodically
  while (currentTime < targetTime)
  {
    if (millis() % 500 == 0)
    {
      DS3231_get(&t);
      currentTime = t.sec + t.min * 60 + t.hour * 60 * 60;
    }

    if (counter < 15000)
    {
      digitalWrite(MOT_1, LOW);
      analogWrite(MOT_2, pwm);
    }
    else
    {
      analogWrite(MOT_1, MAX_PULSE_WIDTH);
      digitalWrite(MOT_2, LOW);
    }

    if (counter < 20000)
      counter++;
    else
      counter = 0;
  }

  digitalWrite(MOT_1, LOW);
  digitalWrite(MOT_2, LOW);
}

// ##################################################################################################

// Spin the head all the way around, then spin the other way
void churnCycleB(long seconds)
{
  //  if(DEBUG)
  //    Serial.println(seconds*1000);
  digitalWrite(MOT_1, LOW);
  digitalWrite(MOT_2, LOW);

  int pwm = ((MAX_PULSE_WIDTH - MIN_PULSE_WIDTH) * (rotationSpeed / 10)) + MIN_PULSE_WIDTH;
  int currentPwm = pwm;
  int accelerate = true;
  int dxn = FORWARD;

  DS3231_get(&t);
  long currentTime = t.sec + t.min * 60 + t.hour * 60 * 60; // get the current time in seconds
  long targetTime  = currentTime + seconds;           // set the terget time in seconds

  // wait until the target time is reached (in seconds)
  while (currentTime < targetTime)
  {

    if (millis() % 500 == 0)
    {
      DS3231_get(&t);
      currentTime = t.sec + t.min * 60 + t.hour * 60 * 60;
    }

    //    Serial.println(currentTime);
    if (dxn == FORWARD)
    {
      // switch to deccelerating once at pwm
      if (currentPwm >= MAX_PULSE_WIDTH)
        accelerate = false;

      // linearly accelerate to MAX_PULSE_WIDTH
      if (millis() % 200 == 0 && accelerate)
      {
        currentPwm += 2;
        digitalWrite(MOT_1, LOW);
        analogWrite(MOT_2, currentPwm);
      }
      // linearly deccelerate to stop
      else if (millis() % 200 == 0)
      {
        currentPwm -= 2;
        digitalWrite(MOT_1, LOW);
        analogWrite(MOT_2, currentPwm);

        if (currentPwm <= pwm)
        {
          dxn = BACK;
          accelerate = true;
        }
      }
    }


    if (dxn == BACK)
    {
      // switch to deccelerating once at pwm
      if (currentPwm >= MAX_PULSE_WIDTH)
        accelerate = false;

      // linearly accelerate to MAX_PULSE_WIDTH
      if (millis() % 200 == 0 && accelerate)
      {
        currentPwm += 2;
        analogWrite(MOT_1, currentPwm);
        digitalWrite(MOT_2, LOW);
      }
      // linearly deccelerate to stop
      else if (millis() % 200 == 0)
      {
        currentPwm -= 2;
        analogWrite(MOT_1, currentPwm);
        digitalWrite(MOT_2, LOW);

        if (currentPwm <= pwm)
        {
          dxn = FORWARD;
          accelerate = true;
        }
      }
    }
  }
  digitalWrite(MOT_1, LOW);
  digitalWrite(MOT_2, LOW);
}

void draw_fish() {
  display.clearDisplay();

  display.drawBitmap(
    56, 8, fish_bmp, img_width, img_height, 1);
  display.display();
  delay(1000);
}

// ##################################################################################################

void setMixtime()
{
  // READ ONLY ONCE
  mixDuration = EEPROM.read(eepromMixDuration);
  
  delay(1000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 8);
  display.print("Adjust Mix ->");
  display.display();
  boolean selection = true;

  while (selection)
  {
    // Read the current state of CLK
    currentStateCLK = digitalRead(CLK);

    // If last and current state of CLK are different, then pulse occurred
    // React to only 1 state change to avoid double count
    if (currentStateCLK != lastStateCLK  && currentStateCLK == 1) {

      // If the DT state is different than the CLK state then
      // the encoder is rotating CCW so decrement
      if (digitalRead(DT) != currentStateCLK) {
        if (mixDuration > 0) // handling negative values
        {
          mixDuration--;
        }
        else
        {
          mixDuration = 0;
        }

      } else {
        // Encoder is rotating CW so increment
        mixDuration ++;
      }

      // display the current time setting
      display.clearDisplay();
      display.setCursor(10, 2);
      display.setTextSize(1);
      display.print("Mix Interval: ");
      display.setCursor(0, 15);
      display.setTextSize(2);
      display.print(mixDuration);
      display.print("m");
      display.display();
    }


    // Remember last CLK state for encoder
    lastStateCLK = currentStateCLK;

    // Read the button state
    int btnState = digitalRead(SW);

    //If we detect LOW signal, button is pressed
    if (btnState == LOW) {
      //if 50ms have passed since last LOW pulse, it means that the
      //button has been pressed, released and pressed again
      if (millis() - lastButtonPress > 50) {

        //close the time selection loop
        selection = false;

        display.clearDisplay();
        display.setCursor(0, 8);
        display.setTextSize(2);
        display.print("Mix Set!");
        display.display();
        delay(500);
      }

      // Remember last button press event
      lastButtonPress = millis();
    }

    // Put in a slight delay to help debounce the reading
    delay(1);
  }

  // if changed, save it 
  if(mixDuration != eepromMixDuration)
  {
    EEPROM.write(eepromMixDuration, mixDuration);
  }
}

// ##################################################################################################

void setMixSpeed()
{
  rotationSpeed = EEPROM.read(eepromSpeed);

  delay(1000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 8);
  display.print("Adjust Speed ->");
  display.display();
  boolean selection = true;

  while (selection)
  {
    // Read the current state of CLK
    currentStateCLK = digitalRead(CLK);

    // If last and current state of CLK are different, then pulse occurred
    // React to only 1 state change to avoid double count
    if (currentStateCLK != lastStateCLK  && currentStateCLK == 1) {

      // If the DT state is different than the CLK state then
      // the encoder is rotating CCW so decrement
      if (digitalRead(DT) != currentStateCLK) {
        if (rotationSpeed > 0) // handling negative values
        {
          rotationSpeed--;
        }
        else
        {
          rotationSpeed = 1;
        }

      } else {
        if (rotationSpeed < 10)
        {
          rotationSpeed++;
        }
        else {
          rotationSpeed = 10;
        }
      }

      // display the current time setting
      display.clearDisplay();
      display.setCursor(10, 2);
      display.setTextSize(1);
      display.print("Mix Speed: ");
      display.setCursor(0, 15);
      display.setTextSize(2);
      display.print(rotationSpeed);
      //      display.print("m");
      display.display();
    }


    // Remember last CLK state for encoder
    lastStateCLK = currentStateCLK;

    // Read the button state
    int btnState = digitalRead(SW);

    //If we detect LOW signal, button is pressed
    if (btnState == LOW) {
      //if 50ms have passed since last LOW pulse, it means that the
      //button has been pressed, released and pressed again
      if (millis() - lastButtonPress > 50) {

        //close the time selection loop
        selection = false;

        display.clearDisplay();
        display.setCursor(0, 8);
        display.setTextSize(2);
        display.print("Speed Set!");
        display.display();
        delay(500);
      }

      // Remember last button press event
      lastButtonPress = millis();
    }

    // Put in a slight delay to help debounce the reading
    delay(1);
  }

  // if changed, save it
  if(rotationSpeed != eepromSpeed)
  {
    EEPROM.write(eepromSpeed, rotationSpeed);
  }
}

// ##################################################################################################

void setMixMode()
{
  mixMode = EEPROM.read(eepromMixMode);
  
  delay(1000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 8);
  display.print("Mix Mode ->");
  display.display();
  boolean selection = true;

  while (selection)
  {
    // Read the current state of CLK
    currentStateCLK = digitalRead(CLK);

    // If last and current state of CLK are different, then pulse occurred
    // React to only 1 state change to avoid double count
    if (currentStateCLK != lastStateCLK  && currentStateCLK == 1) {

      // If the DT state is different than the CLK state then
      // the encoder is rotating CCW so decrement
      if (digitalRead(DT) != currentStateCLK) {
        if (mixMode > 1)
        {
          mixMode--;
        }
        else
        {
          mixMode = 1;
        }

      } else {
        if (mixMode < 3)
        {
          mixMode++;
        }
        else {
          mixMode = 3;
        }
      }

      // display the current time setting
      display.clearDisplay();
      display.setCursor(10, 2);
      display.setTextSize(1);
      display.print("Mix Mode: ");
      display.setCursor(0, 15);
      display.setTextSize(2);

      if (mixMode == MODE_SPIN)
        display.print("SPIN");
      if (mixMode == MODE_CHURN_A)
        display.print("CHURN 1");
      if (mixMode == MODE_CHURN_B)
        display.print("CHURN 2");

      display.display();
    }


    // Remember last CLK state for encoder
    lastStateCLK = currentStateCLK;

    // Read the button state
    int btnState = digitalRead(SW);

    //If we detect LOW signal, button is pressed
    if (btnState == LOW) {
      //if 50ms have passed since last LOW pulse, it means that the
      //button has been pressed, released and pressed again
      if (millis() - lastButtonPress > 50) {

        //close the time selection loop
        selection = false;

        display.clearDisplay();
        display.setCursor(0, 8);
        display.setTextSize(2);
        display.print("Mode Set!");
        display.display();
        delay(500);
      }

      // Remember last button press event
      lastButtonPress = millis();
    }

    // Put in a slight delay to help debounce the reading
    delay(1);
  }
  
  // if changed, save it
  if(mixMode != eepromMixMode)
  {
    EEPROM.write(eepromMixMode, mixMode);
  }
}

// ##################################################################################################

void setInterval()
{
  int tempInterval = 1;
  int remainder  = 0;
  int multiplier = 1;
//  int multiplierB = 1;
  interval = (EEPROM.read(eepromInterval) * EEPROM.read(eepromIntervalMultiplier)) 
            + EEPROM.read(eepromIntervalRemainder);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 8);
  display.print("Adjust Sleep ->");
  display.display();
  boolean selection = true;

  while (selection)
  {
    // Read the current state of CLK
    currentStateCLK = digitalRead(CLK);

    // If last and current state of CLK are different, then pulse occurred
    // React to only 1 state change to avoid double count
    if (currentStateCLK != lastStateCLK  && currentStateCLK == 1) {

      // If the DT state is different than the CLK state then
      // the encoder is rotating CCW so decrement
      if (digitalRead(DT) != currentStateCLK) {
        if (interval > 0) // handling negative values
        {
          interval --;
        }
        else
        {
          interval = 0;
        }

      } else {
        // Encoder is rotating CW so increment
        interval ++;
      }

      // display the current time setting
      display.clearDisplay();
      display.setCursor(10, 2);
      display.setTextSize(1);
      display.print("Time Interval: ");
      display.setCursor(0, 15);
      display.setTextSize(2);
      display.print(interval);
      display.print("m");
      display.display();
    }


    // Remember last CLK state for encoder
    lastStateCLK = currentStateCLK;

    // Read the button state
    int btnState = digitalRead(SW);

    //If we detect LOW signal, button is pressed
    if (btnState == LOW) {
      //if 50ms have passed since last LOW pulse, it means that the
      //button has been pressed, released and pressed again
      if (millis() - lastButtonPress > 50) {

        //close the time selection loop
        selection = false;

        display.clearDisplay();
        display.setCursor(0, 8);
        display.setTextSize(2);
        display.print("Sleep Set!");
        display.display();
        delay(500);

      }

      // Remember last button press event
      lastButtonPress = millis();
    }

    // Put in a slight delay to help debounce the reading
    delay(1);
  }

  tempInterval = interval;
  
  remainder = tempInterval % 255;
  tempInterval -= remainder;

  multiplier = (tempInterval / 255);

  if(multiplier > 255)
  {
    tempInterval = multiplier /255;
    if(tempInterval > 255)
      tempInterval = 255; // cap it at 255, would already be over 190 days interval at this point

    multiplier = 255;
  }

  tempInterval = tempInterval/multiplier;

  // only write if the value is different to save on cycles
  if(EEPROM.read(eepromIntervalRemainder) != remainder)
    EEPROM.write(eepromIntervalRemainder, remainder);
  if(EEPROM.read(eepromIntervalMultiplier) != multiplier)
    EEPROM.write(eepromIntervalMultiplier, multiplier);
  if(EEPROM.read(eepromInterval) != tempInterval)
    EEPROM.write(eepromInterval, tempInterval);


}

// ##################################################################################################

void deviceSleep()
{
  display.clearDisplay();

  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("Sleep Time: ");
  display.print(interval);
  display.print("m");

  display.setCursor(0, 8);
  display.print("Mix Time: ");
  display.print(mixDuration);
  display.print("m");

  display.setCursor(0, 16);
  display.print("Mix Speed: ");
  display.print(((MAX_PULSE_WIDTH - MIN_PULSE_WIDTH) * (rotationSpeed / 10) + MIN_PULSE_WIDTH));

  display.setCursor(0, 24);
  display.print("Mix Mode: ");
  if (mixMode == MODE_SPIN)
    display.print("SPIN");
  if (mixMode == MODE_CHURN_A)
    display.print("CHURN 1");
  if (mixMode == MODE_CHURN_B)
    display.print("CHURN 2");

  display.display();
  delay(4000);
}

// ##################################################################################################

// Set the next alarm
void setNextAlarm(int seconds)
{
  // flags define what calendar component to be checked against the current time in order
  // to trigger the alarm - see datasheet
  // A1M1 (seconds) (0 to enable, 1 to disable)
  // A1M2 (minutes) (0 to enable, 1 to disable)
  // A1M3 (hour)    (0 to enable, 1 to disable)
  // A1M4 (day)     (0 to enable, 1 to disable)
  // DY/DT          (dayofweek == 1/dayofmonth == 0)
  uint8_t flags[5] = { 0, 0, 0, 1, 1 };

  // get current time so we can calc the next alarm
  DS3231_get(&t);

  wake_SECOND = t.sec;
  wake_MINUTE = t.min;
  wake_HOUR = t.hour;

  if (DEBUG)
  {
    Serial.print("Wake Time: ");
    Serial.print(wake_HOUR);
    Serial.print(":");
    Serial.print(wake_MINUTE);
    Serial.print(":");
    Serial.println(wake_SECOND);
  }

  // Add a some seconds to current time. If overflow increment minutes etc.
  wake_SECOND = wake_SECOND + seconds;

  while (wake_SECOND >= 60)
  {
    wake_MINUTE++;
    wake_SECOND = wake_SECOND - 60;

    if (wake_MINUTE > 59)
    {
      wake_HOUR++;
      wake_MINUTE -= 60;
    }
  }

  if (DEBUG)
  {
    Serial.print("Wake Time: ");
    Serial.print(wake_HOUR);
    Serial.print(":");
    Serial.print(wake_MINUTE);
    Serial.print(":");
    Serial.println(wake_SECOND);


  }

  // Set the alarm time (but not yet activated)
  DS3231_set_a1(wake_SECOND, wake_MINUTE, wake_HOUR, 0, flags);

  // Turn the alarm on
  DS3231_set_creg(DS3231_CONTROL_INTCN | DS3231_CONTROL_A1IE);
}

// ##################################################################################################

void setup() {
  Serial.begin(9600);

  pinMode(DISPLAY_SWITCH, OUTPUT);
  pinMode(MOT_1, OUTPUT);
  pinMode(MOT_2, OUTPUT);
  pinMode(RTC_PWR, OUTPUT);

  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);
  pinMode(WAKE_PIN, INPUT_PULLUP);

  digitalWrite(DISPLAY_SWITCH, HIGH);
  digitalWrite(RTC_PWR, HIGH);

  // Read the initial state of CLK of encoder
  lastStateCLK = digitalRead(CLK);

  // setup the RTC, reset the alarm clock from previous session
  Wire.begin();
  DS3231_init(DS3231_CONTROL_INTCN);
  DS3231_clear_a1f();
  if (DEBUG)
  {
    if (digitalRead(WAKE_PIN) == HIGH)
      Serial.println("WAKE PIN HIGH");
    else
      Serial.println("WAKE PIN LOW");
    //    delay(2000);
  }

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    //Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }

  //rotate the screen since orienteation in the enclosure is upside down
    display.setRotation(2);

  // clear the buffer, setup the first text
  display.clearDisplay();
  display.display();
  delay(50);

  if (DEBUG)
    Serial.println("Boot Complete");
  delay(10);

  draw_fish();

  // time setting function
  setInterval();
  sleepTime = interval * 60;

  // mix duration setting function
  setMixtime();
  mixTime = mixDuration * 60;

  // mixing speed
  setMixSpeed();

  // mixing mode
  setMixMode();

  // sleeping time display output
  deviceSleep();



  // begin going into sleep mode
  //  digitalWrite(DISPLAY_SWITCH, LOW);
  display.clearDisplay();
  display.display();

  if (DEBUG)
    Serial.println("Setup complete!");
  delay(250);

}

// ##################################################################################################

void loop() {

  /*
     Device operation loop

     Once setup is completed, it will first set a timer for the mixing
     To stop the mixing, the RTC will interrupt
     It will then set a timer for sleep, waking up on the next interrupt
  */


  static uint8_t oldsec = 99;
  char buff[BUFF_MAX];

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 8);
  display.print("Mixing ...");
  display.display();

  // print out the mixtime in seconds
  if (DEBUG)
  {
    Serial.print("Mix Time (s): ");
    Serial.println(mixTime);
  }

  //  delay(1000);

  DS3231_get(&t);
  Serial.println("Got Time!");

  if (mixMode == MODE_SPIN)
    spinCycle(mixTime);
  else if (mixMode == MODE_CHURN_A)
    churnCycleA(mixTime);
  else
    churnCycleB(mixTime);

  display.clearDisplay();
  display.display();

  DS3231_clear_a1f();
  if (DEBUG)
  {
    snprintf(buff, BUFF_MAX, "%d.%02d.%02d %02d:%02d:%02d\n", t.year,
             t.mon, t.mday, t.hour, t.min, t.sec);
  }


  //  delay(1000);

  // Send a message just to show we are about to sleep
  if (DEBUG)
  {
    Serial.println("Spin Complete!");
    Serial.println("Good night!");
  }
  // set the sleep time

  // print out the sleep time in seconds
  if (DEBUG)
  {
    Serial.print("Sleep Time (s): ");
    Serial.println(sleepTime);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 8);
  display.print("Sleeping -.-");
  display.display();

  setNextAlarm(sleepTime);

  static byte prevADCSRA = ADCSRA;
  ADCSRA = 0;
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  MCUCR = bit (BODS) | bit(BODSE);
  MCUCR = bit(BODS);

  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(WAKE_PIN), wakeUp, LOW);
  Serial.flush();
  interrupts();

  sleep_cpu();

  if (DEBUG)
    Serial.println("I'm awake!");

  // Clear existing alarm so int pin goes high again
  DS3231_clear_a1f();
  // wake up the ADC
  ADCSRA = prevADCSRA;

  digitalWrite(DISPLAY_SWITCH, HIGH); // make sure screen is turned on so that I2C doesn't freak out
  DS3231_get(&t);

  if (DEBUG)
  {
    snprintf(buff, BUFF_MAX, "%d.%02d.%02d %02d:%02d:%02d\n", t.year,
             t.mon, t.mday, t.hour, t.min, t.sec);
    Serial.println("Operation Cycle End");
    Serial.println(buff);
  }
}
