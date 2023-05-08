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
#include <EasyBuzzer.h>
//#include <chainPixels.h>
//#include <neoPixel.h>
#include <Adafruit_NeoPixel.h>
#include <Scheduler.h>

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
ModbusRTUSlave modbus(Serial, buffer, bufferSize, dePin, 20);

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

#define BUZZER_PIN 8

CapacitiveSensor drum[] = {
  CapacitiveSensor(SEND_PIN, RECEIVE_D1_PIN),
  CapacitiveSensor(SEND_PIN, RECEIVE_D2_PIN),
  CapacitiveSensor(SEND_PIN, RECEIVE_D3_PIN),
  CapacitiveSensor(SEND_PIN, RECEIVE_D4_PIN)
};

#define CAPACITANCE_SAMPLES 30 // (higher is better "resolution" but misses quick taps; see lib docs)

#define THRESHOLD_PERCENT_OF_MAX 20
#define INITIAL_ESTIMATED_MAX 500   // value depends on cap samples; stops false trigger before 1st tap

#define FIRST_ROUND_BUZZER_HZ 262 // middle C

#define CHROMATIC_SCALE_TWELFTH_TONE_RATIO 1.0594630943592952645618252949463

#define DEBUG_DRUMS 0

long maxCapacitance[NUM_DRUMS];

//  Serial commnication for Modbus requires disabling the serial communication for
//  debugging capacitative sensing and the drum gameplay.
//  Set MODBUS_DISABLED to 1 stop Modbus communication and allow communication over
//  USB
#define MODBUS_DISABLED 0

// ============== MAIN task =======================================================
void setup()
{
  if (MODBUS_DISABLED) {
    Serial.begin(115200);
    Serial.println("module_drum_light");
    Serial.println("-----------------");
  } else {
    pinMode(A0, OUTPUT);
    Serial.begin(baud, config);
    Scheduler.start(setupModbus, updateModbus);
  }

  Scheduler.start(setupBuzzer, updateBuzzer);
  Scheduler.start(setupLights, updateLights);
  Scheduler.start(setupDrums,  updateDrums);
}

void loop()
{
  yield();
}

// ============== Modbud Task =====================================================
int32_t inputRegisterRead(uint16_t address) {
    if (address < inputRegisters ) {
        return triggeredDrumId;
    } else {
        return -1;
    }
}

void setupModbus()
{
  modbus.begin(id, baud, config);
  modbus.configureInputRegisters(1, inputRegisterRead);
}

void updateModbus()
{
  modbus.poll();
  yield();
}

// ============== Buzzer task =====================================================
void setupBuzzer()
{
  if (MODBUS_DISABLED) Serial.println("Setting buzzer pin...");
  EasyBuzzer.setPin(BUZZER_PIN);
}

void updateBuzzer()
{
  EasyBuzzer.update();
  yield();
}

// ============== Lights task (everything left from old setup/loop) ===============
void setupLights()
{
  if (MODBUS_DISABLED) Serial.println("Initializing NeoPixel rings...");
  for (int r = 0; r < NUM_RINGS; r++)
  {
    ring[r].begin();
    ring[r].setBrightness(30); //adjust brightness here
    ring[r].show(); // Initialize all pixels to 'off'
  }
}

void restartGame()
{
  if(MODBUS_DISABLED) {
    if (!DEBUG_DRUMS) Serial.println("restartGame() called");
  }

  uint32_t white = Adafruit_NeoPixel::Color(255,255,255);
  uint32_t black = Adafruit_NeoPixel::Color(0,0,0);

  // Flash on and off several times
  if(MODBUS_DISABLED) {
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
  if(MODBUS_DISABLED) {
    if (!DEBUG_DRUMS) Serial.println("restartGame() exiting");
  }
}

#define WORST_CASE_SEARCH_CYCLES 100

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

void updateLights()
{
  static int colourId = -1;
  static long roundDurationMs = GAME_ROUND_INITIAL_TIMEOUT_MS;
  static long roundStartMillis = 0;
  static unsigned int buzzerFreq;

  static long curCapacitance[NUM_DRUMS];
  static unsigned long nextDrumMillis = 0;
  static int drumId = 0;

  long current_millis = millis();

  if (colourId < 0 || (current_millis > (roundStartMillis + roundDurationMs))) // && timeoutMillis != 0
  {
    if(MODBUS_DISABLED) {
      if (colourId >= 0) {
        Serial.println("Game timed out!");
      }
    }

    // Block during game start flashing phase
    restartGame();

    // Initialise first round
    if (MODBUS_DISABLED) {
      if (!DEBUG_DRUMS) Serial.println("Initializing first round");
    }
    roundDurationMs = GAME_ROUND_INITIAL_TIMEOUT_MS;
    current_millis = millis();
    roundStartMillis = current_millis;
    colourId = random(NUM_COLOUR_PRESETS);
    drumId = random(NUM_DRUMS);

    if (MODBUS_DISABLED) {
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

    buzzerFreq = FIRST_ROUND_BUZZER_HZ;
  }

  EasyBuzzer.singleBeep(buzzerFreq, 250); // Freq Hz, Duration ms

  // if (!DEBUG_DRUMS) Serial.println("Reading drum sensors");
  for (int d = 0; d < NUM_DRUMS; d++)
  {
    curCapacitance[d] = drum[d].capacitiveSensor(CAPACITANCE_SAMPLES);
  }
  if (MODBUS_DISABLED) {
    if (DEBUG_DRUMS)
    {
      // Print the result of the sensor readings
      // Note that the capacitance value is an arbitrary number
      // See: https://playground.arduino.cc/Main/CapacitiveSensor/ for details
      for (int d = 0; d < NUM_DRUMS; d++)
      {
        Serial.print(curCapacitance[drumId]);
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
    float threshold = maxCapacitance[drumId] * THRESHOLD_PERCENT_OF_MAX / 100;
    float thresholdFactor = curCapacitance[d] / threshold;
    if (thresholdFactor > bestThresholdFactor)
    {
      bestThresholdFactor = thresholdFactor;
      bestDrumId = d;
    }
  }

  // Was at least one drum hit?
  if (bestThresholdFactor > 1.0f)
  {
    // Was it the right one? (ignore time guards against *still* pressing from last round)
    bool outOfIgnoreDrumTime = (millis() - roundStartMillis) > PER_ROUND_INITIAL_IGNORE_DRUM_MS;
    if (bestDrumId != drumId && outOfIgnoreDrumTime)
    {
      // Failure! reinitialize / game start on next loop entry
      colourId = -1;
    }
    else
    {
      // Success! increase speed / pitch and choose next round

      triggeredDrumId = drumId;
      if (MODBUS_DISABLED) {
        if (!DEBUG_DRUMS) Serial.println("Success! Hit correct drum");
      }

      // TODO TODO TODO refactor this repeated code from 1st round into a function
      roundDurationMs = (roundDurationMs * PER_ROUND_TIMEOUT_FACTOR_PERCENT) / 100;
      roundStartMillis = millis();
      colourId = random(NUM_COLOUR_PRESETS);
      drumId = random(NUM_DRUMS);

      if (MODBUS_DISABLED) {
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

      // raise buzzer pitch by one twelfth of an octave
      buzzerFreq *= CHROMATIC_SCALE_TWELFTH_TONE_RATIO;
    }
  }
}

// ============== Drums task ======================================================
void setupDrums()
{
  // Trying a 5sec autocalibration of baseline
      if (MODBUS_DISABLED) Serial.println("Autocalibrating drums...");
  for (int d = 0; d < NUM_DRUMS; d++)
  {
    drum[d].set_CS_AutocaL_Millis(5000);
    maxCapacitance[d] = INITIAL_ESTIMATED_MAX;
  }
  delay(5000);

//   // Disable the automatic re-calibration feature of the
//   // capacitive sensor library
//   sensor.set_CS_AutocaL_Millis(0xFFFFFFFF);

  if (MODBUS_DISABLED) Serial.println("Beginning game!");
}

void updateDrums()
{
  yield();
}
