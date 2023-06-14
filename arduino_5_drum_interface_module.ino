// Based on example code from here: (sent by Nick Shanks 26/3/22 to Paka; shared with Adam Feb'23)
// https://www.digikey.co.uk/en/maker/blogs/2021/how-to-add-capacitive-sensing-to-any-arduino-project
//
// From CapacitiveSensor library example code comments + online docs:
// https://playground.arduino.cc/Main/CapacitiveSensor/
//
// * Uses a high value resistor e.g. 10M between send pin and receive pin
// * Resistor effects sensitivity, experiment with values, 50K - 50M. Larger resistor values yield larger sensor values.
// * Receive pin is the sensor pin - try different amounts of foil/metal on this pin
// [...]
// Note that the hardware can be set up with one sPin and several resistors
// and rPin's for calls to various capacitive sensors. See the example sketch.

/*
- BusIO 1.14.1
- DS3502 1.0.1
- NeoPixel 1.10.7 (1.11.0 avail Mar'23)
- PWM Servo Dr.Lib. 2.4.0 (2.4.1 avail Mar'23)
- (LC_baseTools 1.5.0; not needed?)
- (LC_neoPixel 1.2.0; not needed?)
- Capacitive Sensing Library 0.5.0 ("Stable release")
- EasyBuzzer 1.0.4
- Arduino-Scheduler 1.2.4 (unofficial / Mikael Patel)
*/

#include <CapacitiveSensor.h>
//#include <EasyBuzzer.h>
//#include <chainPixels.h>
//#include <neoPixel.h>
#include <Adafruit_NeoPixel.h>
//#include <Scheduler.h>

//  Modbus Setup
//  From https://github.com/CMB27/ModbusRTUSlave/
#include <ModbusRTUSlave.h>

const uint16_t id = 1;

const uint32_t baud = 9600;
const uint8_t config = SERIAL_8E1;
const uint16_t bufferSize = 256;
const uint8_t dePin = A0;

const uint8_t inputRegisters = 1;

uint8_t buffer[bufferSize];
ModbusRTUSlave modbus(Serial, buffer, bufferSize, dePin);

int32_t triggered = false;
int32_t triggeredDrumId = 0;

#define NEOPIXEL_RING1_PIN 3
#define NEOPIXEL_RING2_PIN 4
#define NEOPIXEL_RING3_PIN 5
#define NEOPIXEL_RING4_PIN 6
#define NEOPIXEL_RING5_PIN 7

#define NUM_RINGS 5

Adafruit_NeoPixel ring[] = {
  Adafruit_NeoPixel(16, NEOPIXEL_RING1_PIN, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(16, NEOPIXEL_RING2_PIN, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(16, NEOPIXEL_RING3_PIN, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(16, NEOPIXEL_RING4_PIN, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(16, NEOPIXEL_RING5_PIN, NEO_GRB + NEO_KHZ800)
};

#define GAME_START_FLASH_CYCLES 3
#define GAME_START_FLASH_HALFCYLCE_MS 500
#define GAME_ROUND_INITIAL_TIMEOUT_MS 4000
#define PER_ROUND_TIMEOUT_FACTOR_PERCENT 100//90
#define PER_ROUND_INITIAL_IGNORE_DRUM_MS 250

#define NUM_COLOUR_PRESETS 4 //6

uint32_t colour_preset[] = {
  Adafruit_NeoPixel::Color(0,   0,   255), //blue
  Adafruit_NeoPixel::Color(0,   255, 0),   //green
  Adafruit_NeoPixel::Color(255, 0,   0),   //red
  Adafruit_NeoPixel::Color(255, 255, 0),   //yellow

  // Adafruit_NeoPixel::Color(255, 0,   255), //magenta
  // Adafruit_NeoPixel::Color(0,   255, 255), //cyan
};

#define NUM_DRUMS 4

#define SEND_PIN 2
#define RECEIVE_D1_PIN 9
#define RECEIVE_D2_PIN 10
#define RECEIVE_D3_PIN 11
#define RECEIVE_D4_PIN 12

CapacitiveSensor drum[] = {
  CapacitiveSensor(SEND_PIN, RECEIVE_D1_PIN),
  CapacitiveSensor(SEND_PIN, RECEIVE_D2_PIN),
  CapacitiveSensor(SEND_PIN, RECEIVE_D3_PIN),
  CapacitiveSensor(SEND_PIN, RECEIVE_D4_PIN)
};

#define CAPACITANCE_SAMPLES 30 // (higher is better "resolution" but misses quick taps; see lib docs)

#define THRESHOLD_PERCENT_OF_MAX 20
#define INITIAL_ESTIMATED_MAX 500   // value depends on cap samples; stops false trigger before 1st tap

#define DEBUG_DRUMS 0

long maxCapacitance[NUM_DRUMS];

//  Serial commnication for Modbus requires disabling the serial communication for
//  debugging capacitative sensing and the drum gameplay.
//  Set MODBUS_DISABLED to 1 stop Modbus communication and allow communication over
//  USB
bool enable_serial_debug = false;

// Declarations!
void setupDrums();
void setupLights();
void modbusSetup();
void modbusUpdate();
void updateLights();

// ============== MAIN task =======================================================
// TODO Make the game non-blocking so that we can communicate with the modbus whilst
// the game is in progress
void setup()
{
  if (enable_serial_debug) {
    // Start serial for debug purposes ...
    Serial.begin(115200);
    Serial.println("module_drum_light");
    Serial.println("-----------------");
  } else {
    // ... or use serial for RS485
    modbusSetup();
  }
  setupDrums();
  setupLights();
}

void loop()
{
  //  Check for modbus as fast as we can!
  modbusUpdate();
  updateLights();
}

// ============== Modbud ========================================================
// TODO: Add a function to resend last transmission if the CRC check doesn't pass
// TODO: Keep track of score here, as well as the RPi, to keep the game in sync.
// QUESTION: It might make more sense for the arduino to be controlling the RPi!
int32_t inputRegisterRead(uint16_t address) {
    if (address < inputRegisters ) {
      if (triggered) {
        return triggeredDrumId + 1;
        triggered = false;
      } else {
        return 0;
      }
    } else {
        return -1;
    }
}

void modbusSetup()
{
  pinMode(A0, OUTPUT);
  digitalWrite(A0, LOW);

  Serial.begin(baud, config);

  modbus.begin(id, baud, config);
  modbus.configureInputRegisters(1, inputRegisterRead);
}

void modbusUpdate()
{
  modbus.poll();
}

// ============== Lights task (everything left from old setup/loop) ===============
// TODO: Refactor into a state-machine?
// This would remove all the delay() functions and allow modbus communication while the game is being reset
// QUESTION: Instead of the game timing out repeatedly, if not being played, it could start a new level,
// and maybe give a blast of the fire as a busking / attract feature.
// QUESTION: Should there be some indication of the amount of time left to touch the drum?
// Prehaps on the main drum selection it could.
// QUESTION: The game increases the speed every correct touch. It should really do that once every X touches,
// to co-inside with the 
void setupLights()
{
  if (enable_serial_debug) Serial.println("Initializing NeoPixel rings...");
  for (int r = 0; r < NUM_RINGS; r++)
  {
    ring[r].begin();
    ring[r].setBrightness(30); //adjust brightness here
    ring[r].show(); // Initialize all pixels to 'off'
  }
}

void restartGame()
{
  if(enable_serial_debug) {
    if (!DEBUG_DRUMS) Serial.println("restartGame() called");
  }

  uint32_t white = Adafruit_NeoPixel::Color(255,255,255);
  uint32_t black = Adafruit_NeoPixel::Color(0,0,0);

  // Flash on and off several times
  if(enable_serial_debug) {
    if (!DEBUG_DRUMS) Serial.println("Flashing all rings on/off a few times...");
  }
  for (int i = 0; i < GAME_START_FLASH_CYCLES; i++)
  {
    // if (!DEBUG_DRUMS) Serial.println("Filling rings: white");
    for (int r = 0; r < NUM_RINGS; r++)
    {
      ring[r].fill(white);
      ring[r].show();
    }
    delay(GAME_START_FLASH_HALFCYLCE_MS);
    // if (!DEBUG_DRUMS) Serial.println("Filling rings: black");
    for (int r = 0; r < NUM_RINGS; r++)
    {
      ring[r].fill(black);
      ring[r].show();
    }
    delay(GAME_START_FLASH_HALFCYLCE_MS);
  }
  if(enable_serial_debug) {
    if (!DEBUG_DRUMS) Serial.println("restartGame() exiting");
  }
}

#define WORST_CASE_SEARCH_CYCLES 100

// TODO Remove loop:
// Psuedo code
// Random number - 1
// if random >= avoidId
//  return id = random + 1;

// QUESTION: This can return multiple of the same colour. Is this desired?
// If we instead want to shuffle each colour we can use a Fisher-Yates shuffle with an array
int randomIdNot(int avoidId)
{
  int resultId = -1;
  for (int i = 0; i < WORST_CASE_SEARCH_CYCLES; i++)
  {
    resultId = random(NUM_COLOUR_PRESETS);
    if (resultId != avoidId) return resultId;
  }
  // ERROR : bailout
  return avoidId;
}

int8_t getTriggeredDrum() {

  static long curCapacitance[NUM_DRUMS];

  // if (!DEBUG_DRUMS) Serial.println("Reading drum sensors");
  for (int d = 0; d < NUM_DRUMS; d++)
  {
    curCapacitance[d] = drum[d].capacitiveSensor(CAPACITANCE_SAMPLES);
  }
  if (enable_serial_debug) {
    if (DEBUG_DRUMS)
    {
      // Print the result of the sensor readings
      // Note that the capacitance value is an arbitrary number
      // See: https://playground.arduino.cc/Main/CapacitiveSensor/ for details
      for (int d = 0; d < NUM_DRUMS; d++)
      {
        Serial.print(curCapacitance[d]);
        Serial.print(" ");
      }
      Serial.println("");
    }
  }

  // Threshold detection
  float bestThresholdFactor = 0;
  int bestDrumId = -1;

  for (int d = 0; d < NUM_DRUMS; d++)
  {
    if (curCapacitance[d] > maxCapacitance[d]) { maxCapacitance[d] = curCapacitance[d]; }
    float threshold = maxCapacitance[d] * THRESHOLD_PERCENT_OF_MAX / 100;
    float thresholdFactor = curCapacitance[d] / threshold;
    if (thresholdFactor > bestThresholdFactor)
    {
      bestThresholdFactor = thresholdFactor;
      bestDrumId = d;
    }
  }

  if (bestThresholdFactor > 1.0f) {
    return bestDrumId;
  } else {
    return -1;
  }

}

void updateGame() {
  ;
}

void updateLights()
{
  static int colourId = -1;
  static long roundDurationMs = GAME_ROUND_INITIAL_TIMEOUT_MS;
  static long roundStartMillis = 0;
  static int drumId = 0;

  long current_millis = millis();

  if (colourId < 0 || (current_millis > (roundStartMillis + roundDurationMs))) // && timeoutMillis != 0
  {
    if(enable_serial_debug) {
      if (colourId >= 0) {
        Serial.println("Game timed out!");
      }
    }

    // Block during game start flashing phase
    // TODO: Rewrite to remove blocking behaviour
    restartGame();

    // Initialise first round
    if (enable_serial_debug) {
      if (!DEBUG_DRUMS) Serial.println("Initializing first round");
    }
    roundDurationMs = GAME_ROUND_INITIAL_TIMEOUT_MS;
    current_millis = millis();
    roundStartMillis = current_millis;
    colourId = random(NUM_COLOUR_PRESETS);
    drumId = random(NUM_DRUMS);

    if (enable_serial_debug) {
      if (!DEBUG_DRUMS) Serial.println("Filling chosen drumId + Master with colour_preset (and the rest black)");
    }
    uint32_t black = Adafruit_NeoPixel::Color(0,0,0);
    for (int r = 0; r < NUM_RINGS - 1; r++)
    {
      ring[r].fill(r == drumId ? colour_preset[colourId] : colour_preset[randomIdNot(colourId)]);
      ring[r].show();
    }
    ring[NUM_RINGS - 1].fill(colour_preset[colourId]);
    ring[NUM_RINGS - 1].show();

  }

  triggeredDrumId = getTriggeredDrum();

  // Was at least one drum hit?
  if (triggeredDrumId >= 0)
  {
    triggered = false;
    // Was it the right one? (ignore time guards against *still* pressing from last round)
    bool outOfIgnoreDrumTime = (millis() - roundStartMillis) > PER_ROUND_INITIAL_IGNORE_DRUM_MS;
    if (triggeredDrumId != drumId && outOfIgnoreDrumTime)
    {
      // Failure! reinitialize / game start on next loop entry
      colourId = -1;
    }
    else
    {
      // Success! increase speed / pitch and choose next round
      triggeredDrumId = drumId;
      triggered = true;

      if (enable_serial_debug) {
        if (!DEBUG_DRUMS) Serial.println("Success! Hit correct drum");
      }

      // TODO TODO TODO refactor this repeated code from 1st round into a function
      roundDurationMs = (roundDurationMs * PER_ROUND_TIMEOUT_FACTOR_PERCENT) / 100;
      roundStartMillis = millis();
      colourId = random(NUM_COLOUR_PRESETS);
      drumId = random(NUM_DRUMS);

      if (enable_serial_debug) {
        if (!DEBUG_DRUMS) Serial.println("Filling chosen drumId with colour_preset (and the rest black)");
      }
      uint32_t black = Adafruit_NeoPixel::Color(0,0,0);
      for (int r = 0; r < NUM_RINGS - 1; r++)
      {
        ring[r].fill(r == drumId ? colour_preset[colourId] : colour_preset[randomIdNot(colourId)]);
        ring[r].show();
      }
      ring[NUM_RINGS - 1].fill(colour_preset[colourId]);
      ring[NUM_RINGS - 1].show();
    }
  }
  yield();
}

// ============== Drums task ======================================================
void setupDrums()
{
  // Trying a 5sec autocalibration of baseline
  if (enable_serial_debug) Serial.println("Autocalibrating drums...");
  for (int d = 0; d < NUM_DRUMS; d++)
  {
    drum[d].set_CS_AutocaL_Millis(5000);
    maxCapacitance[d] = INITIAL_ESTIMATED_MAX;
  }
  delay(5000);

//   // Disable the automatic re-calibration feature of the
//   // capacitive sensor library
//   sensor.set_CS_AutocaL_Millis(0xFFFFFFFF);

  if (enable_serial_debug) Serial.println("Beginning game!");
}
