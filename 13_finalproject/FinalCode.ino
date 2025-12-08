/*
  Alex Mullen - PS70 Final Project - Water-Filling Table

  - 4x Hall effect sensors: pins 27, 26, 25, 33
  - 1x Switch on pin 39 (default state = OFF = HIGH)
  - 1x LED on pin 23:
      * When switch is ON and at least one cup is present → LED ON ("armed")
      * When switch is OFF or pouring → LED OFF until switch is turned ON again
  - 1x Relay on pin 22:
      * Controls water pump
      * ON during the pouring sequence (for each cup in the queue)
  - 2x Servos:
      PanServo  -> pin 12, bounds: [left] 40, [right] 135
      TiltServo -> pin 14, bounds: [up] 180, [down] 115
  Hall logic:
    - Cup present when analogRead(hallPin) > 3900
   
*/

#include <ESP32Servo.h>

// ----------------------- PIN CONFIGURATION -----------------------
const int HALL_PINS[4]  = {27, 26, 25, 33};  // Hall sensors
const int SWITCH_PIN    = 39;                // Switch (default OFF = HIGH)
const int LED_PIN       = 23;                // LED pin
const int RELAY_PIN     = 22;                // Relay for pump

const int PAN_SERVO_PIN  = 12;
const int TILT_SERVO_PIN = 14;

// ------------------ GLOBAL VALUES ------------------

// Cup detection: any analog value > 3900 indicates a cup is present
int CUP_DETECT_THRESHOLD = 3900;

// Servo rest positions (idle)
int REST_PAN_ANGLE  = 90;    // Neutral pan
int REST_TILT_ANGLE = 115;   // Between [up=148, down=115]

// Servo positions for each cup (index 0-3 correspond to HALL_PINS[0-3])
int CUP_PAN_ANGLES[4]  = {
  59,   // Cup 0 (hall on pin 27) - far left
  85,   // Cup 1 (hall on pin 26)
  133,  // Cup 2 (hall on pin 25)
  108   // Cup 3 (hall on pin 33) - far right
};

int CUP_TILT_ANGLES[4] = {
  136,  // Cup 0 tilt
  134,  // Cup 1 tilt
  140,  // Cup 2 tilt
  147   // Cup 3 tilt
};

// How long to hold the stream over each cup (milliseconds)
unsigned long POUR_DURATION_MS = 4000;

// How often to print sensor values (ms)
unsigned long SERIAL_PRINT_INTERVAL_MS = 200;

// Relay logic level
const int RELAY_ON_LEVEL  = HIGH;
const int RELAY_OFF_LEVEL = LOW;

// ------------------ INTERNAL STATE VARIABLES ------------------

Servo panServo;
Servo tiltServo;

int  hallValues[4]        = {0, 0, 0, 0};
bool cupPresent[4]        = {false, false, false, false};
bool anyCupPresent        = false;

int  lastSwitchState      = HIGH;   // default (ON)
unsigned long lastPrint   = 0;

// Sequential filling queue
int cupQueue[4]           = { -1, -1, -1, -1 };
int queueLength           = 0;
int currentQueueIndex     = -1;

bool sequencing           = false;  // Currently filling a sequence of cups?
unsigned long pourStartTimeMs = 0;

// ------------------ HELPER FUNCTIONS ------------------

// Edge-detect the switch turning ON (OFF→ON transition, HIGH→LOW)
bool switchTurnedOnEdge() {
  int currentState = digitalRead(SWITCH_PIN);
  bool turnedOn = false;

  // OFF (HIGH) -> ON (LOW)
  if (lastSwitchState == LOW && currentState == HIGH) {
    turnedOn = true;
  }
  // Turned on and left on
  else if(lastSwitchState == HIGH && currentState == HIGH) {
    turnedOn = false;
  }
  // OFF
  else if (currentState == LOW) {
    turnedOn = false;
  }

  lastSwitchState = currentState;
  return turnedOn;
}

// Switch is ON when pin reads HIGH
bool switchIsOn() {
  return (digitalRead(SWITCH_PIN) == HIGH);
}

// Read hall sensors, update cup presence
void readHallSensorsAndDetectCups() {
  anyCupPresent = false;

  for (int i = 0; i < 4; i++) {
    hallValues[i] = analogRead(HALL_PINS[i]);

    // Cup present only if value > CUP_DETECT_THRESHOLD
    if (hallValues[i] > CUP_DETECT_THRESHOLD) {
      cupPresent[i] = true;
      anyCupPresent = true;
    } else {
      cupPresent[i] = false;
    }   
  }
}

// Move servos to rest position
void moveToRestPosition() {
  panServo.write(REST_PAN_ANGLE);
  tiltServo.write(REST_TILT_ANGLE);
}

// Move servos to the position for a specific cup index
void moveToCupPosition(int cupIndex) {
  if (cupIndex < 0 || cupIndex > 3) return;
  panServo.write(CUP_PAN_ANGLES[cupIndex]);
  tiltServo.write(CUP_TILT_ANGLES[cupIndex]);
}

// Build the queue of cups that are currently present (in order 0→3)
void buildCupQueueFromCurrentCups() {
  queueLength       = 0;
  currentQueueIndex = -1;

  for (int i = 0; i < 4; i++) {
    if (cupPresent[i]) {
      cupQueue[queueLength] = i;
      queueLength++;
    }
  }

  // Clear out unused entries
  for (int i = queueLength; i < 4; i++) {
    cupQueue[i] = -1;
  }
}

// Turn pump on/off via relay
void setPump(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

// ------------------ SETUP & LOOP ------------------

void setup() {
  Serial.begin(9600);

  // Pin modes
  for (int i = 0; i < 4; i++) {
    pinMode(HALL_PINS[i], INPUT);
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Switch: default OFF = HIGH.
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  lastSwitchState = digitalRead(SWITCH_PIN);  // should read HIGH at boot

  // Relay
  pinMode(RELAY_PIN, OUTPUT);
  setPump(false);

  // Attach servos
  panServo.attach(PAN_SERVO_PIN);
  tiltServo.attach(TILT_SERVO_PIN);

  moveToRestPosition();

  Serial.println("Alex Mullen - PS70 Final Project - Water-Filling Table");
}

void loop() {
  // 1. Read hall sensors and update cup presence
  readHallSensorsAndDetectCups();

  // 2. Switch + sequence control
  bool swOn = switchIsOn();

  // If switch has just been turned ON (OFF -> ON) and we're not already sequencing:
  //   snapshot all present cups and start filling them in sequence.
  if (!sequencing && switchTurnedOnEdge()) {
    if (anyCupPresent) {
      buildCupQueueFromCurrentCups();

      if (queueLength > 0) {
        sequencing = true;
        currentQueueIndex = 0;
        moveToCupPosition(cupQueue[currentQueueIndex]);
        pourStartTimeMs = millis();
        setPump(true);   // Start pump for the sequence

        Serial.print("Starting sequence for cups: ");
        for (int i = 0; i < queueLength; i++) {
          Serial.print(cupQueue[i]);
          Serial.print(" ");
        }
        Serial.println();
      }
    } else {
      Serial.println("Switch turned ON, but no cups detected.");
    }
  }

  // 3. If sequencing, manage timing for each cup in the queue
  if (sequencing) {
    unsigned long now = millis();
    if (now - pourStartTimeMs >= POUR_DURATION_MS) {
      // Stop pump and move to next cup
      setPump(false);
      delay(20);
      currentQueueIndex++;

      if (currentQueueIndex >= queueLength) {
        // Sequence finished
        sequencing = false;
        queueLength = 0;
        currentQueueIndex = -1;
        moveToRestPosition();

        Serial.println("Sequence complete, returning to rest.");
        swOn = switchTurnedOnEdge();
      } else {
        // Go to the next cup
        moveToCupPosition(cupQueue[currentQueueIndex]);
        setPump(true);
        pourStartTimeMs = now;
        Serial.print("Moving to cup index: ");
        Serial.println(cupQueue[currentQueueIndex]);
      }
    }
  }

  // 4. LED logic:
  //    - When switch is OFF and not currently sequencing:
  //          LED ON if any cup present (arming indicator)
  //    - When switch is ON or sequencing:
  //          LED OFF until switch is turned OFF again
  if (!switchTurnedOnEdge() && !sequencing && anyCupPresent) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  // 5. Periodic serial print of sensor values and state
  unsigned long now = millis();
  if (now - lastPrint >= SERIAL_PRINT_INTERVAL_MS) {
    lastPrint = now;

    Serial.print("Hall: ");
    for (int i = 0; i < 4; i++) {
      Serial.print("H");
      Serial.print(i);
      Serial.print("=");
      Serial.print(hallValues[i]);
      Serial.print("(");
      Serial.print(cupPresent[i] ? "CUP" : "NO");
      Serial.print(")  ");
    }

    Serial.print("| anyCupPresent=");
    Serial.print(anyCupPresent ? "YES" : "NO");

    Serial.print(" | switch=");
    Serial.print(swOn ? "ON" : "OFF");

    Serial.print(" | sequencing=");
    Serial.print(sequencing ? "YES" : "NO");

    if (sequencing) {
      Serial.print(" | currentQueueIndex=");
      Serial.print(currentQueueIndex);
      Serial.print(" of ");
      Serial.print(queueLength);
    }

    Serial.println();
  }

  // Small delay to avoid overworking ESP32
  delay(5);
}