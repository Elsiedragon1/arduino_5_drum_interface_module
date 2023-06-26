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

#include <CapacitiveSensor.h>
#include <Adafruit_NeoPixel.h>

//  Modbus Setup
//  From https://github.com/CMB27/ModbusRTUSlave/
#include <ModbusRTUSlave.h>

const uint16_t id = 1;

const uint32_t baud = 115200;
const uint8_t config = SERIAL_8E1;
const uint16_t bufferSize = 256;
const uint8_t dePin = A0;

const uint8_t inputRegisters = 2;

uint16_t resendBuffer;

uint8_t buffer[bufferSize];
ModbusRTUSlave modbus(Serial, buffer, bufferSize, dePin);

int32_t triggered = false;
int32_t triggeredDrumId = 0;
uint8_t bigFlameScore = 5;

#define NEOPIXEL_RING1_PIN 3
#define NEOPIXEL_RING2_PIN 4
#define NEOPIXEL_RING3_PIN 5
#define NEOPIXEL_RING4_PIN 6
#define NEOPIXEL_RING5_PIN 7

const uint8_t NUM_RINGS = 5;

Adafruit_NeoPixel ring[] = {
    Adafruit_NeoPixel(16, NEOPIXEL_RING1_PIN, NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(16, NEOPIXEL_RING2_PIN, NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(16, NEOPIXEL_RING3_PIN, NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(16, NEOPIXEL_RING4_PIN, NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(16, NEOPIXEL_RING5_PIN, NEO_GRB + NEO_KHZ800)
};

#define GAME_ROUND_INITIAL_TIMEOUT_MS 4000

#define NUM_COLOUR_PRESETS 4 //6

uint32_t colourPreset[] = {
    Adafruit_NeoPixel::Color(0,   0,   255), //blue
    Adafruit_NeoPixel::Color(0,   255, 0),   //green
    Adafruit_NeoPixel::Color(255, 0,   0),   //red
    Adafruit_NeoPixel::Color(255, 255, 0),   //yellow
};

uint32_t white = Adafruit_NeoPixel::Color(255,255,255);
uint32_t black = Adafruit_NeoPixel::Color(0,0,0);

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

long maxCapacitance[NUM_DRUMS];

//  Serial commnication for Modbus requires disabling the serial communication for
//  debugging capacitative sensing and the drum gameplay.
//  Set MODBUS_DISABLED to 1 stop Modbus communication and allow communication over
//  USB
bool enable_serial_debug = false;
bool enable_drum_debug = true; // Requires enable serial debug to be true also!

// Declarations!
void setupDrums();
void setupLights();
void modbusSetup();
void modbusUpdate();
void updateLights();
void updateGame();

// ============== MAIN task =======================================================
// TODO Make the game non-blocking so that we can communicate with the modbus whilst
// the game is in progress
void setup()
{
    if (enable_serial_debug)
    {
        // Start serial for debug purposes ...
        Serial.begin(115200);
        Serial.println("module_drum_light");
        Serial.println("-----------------");
    }
    else
    {        // ... or use serial for RS485
        modbusSetup();
    }
    setupDrums();
    setupLights();
}

uint32_t currentTick = 0;

void loop()
{
    currentTick = millis();
    //  Check for modbus as fast as we can!
    modbusUpdate();
    updateGame();
}

// ============== Modbud ========================================================
// TODO: Add a function to resend last transmission if the CRC check doesn't pass
// TODO: Keep track of score here, as well as the RPi, to keep the game in sync.
int32_t inputRegisterRead(uint16_t address)
{
    if (address < inputRegisters )
    {
        switch (address)
        {
        case 0:
            if (triggered)
            {
                triggered = false;
                resendBuffer = triggeredDrumId + 1;
                if (score % bigFlameScore == 0 )
                {
                    return 5; // 5 is the trigger for the larger flame!
                }
                return triggeredDrumId + 1; // -1 is reserved for errors, 0 for no trigger, so drumId has to be sent starting at 1
            }
            else
            {
                return 0; // No drum touched!
            }
            break;
        case 1:
            return resendBuffer;
        default:
            return -1;
        }
    }
    else
    {
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
    //  Update as fast as possible ...
    modbus.poll();
}

// ============== Lights task (everything left from old setup/loop) ===============
// QUESTION: Instead of the game timing out repeatedly, if not being played, it could start a new level,
// and maybe give a blast of the fire as a busking / attract feature.
// QUESTION: The game increases the speed every correct touch. Should it really do that once every X touches,
// to co-inside with the big blasts?
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

int8_t getTriggeredDrum()
{
    static long curCapacitance[NUM_DRUMS];
    static uint8_t lastBestDrumId = -1;

    for (int d = 0; d < NUM_DRUMS; d++)
    {
        curCapacitance[d] = drum[d].capacitiveSensor(CAPACITANCE_SAMPLES);
    }
    if (enable_serial_debug)
    {
        if (enable_drum_debug)
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
        if (curCapacitance[d] > maxCapacitance[d])
        {
            maxCapacitance[d] = curCapacitance[d];
        }
        float threshold = maxCapacitance[d] * THRESHOLD_PERCENT_OF_MAX / 100;
        float thresholdFactor = curCapacitance[d] / threshold;
        if (thresholdFactor > bestThresholdFactor)
        {
            bestThresholdFactor = thresholdFactor;
            bestDrumId = d;
        }
    }

    if (bestThresholdFactor > 1.0f)
    {
        if (bestDrumId != lastBestDrumId) // Protect against still holding the drum down!
        {
            lastBestDrumId = bestDrumId;
            return bestDrumId;
        }
        else
        {
            lastBestDrumId = bestDrumId;
            return -1;
        }
    }
    else
    {
        lastBestDrumId = -1;
        return -1;
    }
}

enum MODE {
  IDLE = 0,
  BUSK = 1,
  GAME = 2,
  FAIL = 3
};

uint8_t mode = IDLE;
uint8_t lastMode = IDLE;

uint16_t score = 0;
uint16_t sessionHighscore = 0;
uint16_t alltimeHighscore = 0;

uint16_t stage = 0;

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
    //   drum[0].set_CS_AutocaL_Millis(0xFFFFFFFF);

    if (enable_serial_debug) Serial.println("Beginning game!");
}

//  ============= GAMESTATES ====================================================

//  ============= RESET STATE ===================================================
uint32_t resetStateTick = 0;
uint32_t resetStateDuration = 4000;
uint32_t resetAnimationTick = 0;
uint32_t resetAnimationInterval = 500; // This is the update frequency of the black and white colours

bool resetAnimationState = true;

void initResetState()
{   
    if (enable_serial_debug) Serial.println("INIT RESET STATE");
    resetStateTick = currentTick;
    resetAnimationState = true;
}

void updateResetState()
{   
    if (currentTick - resetStateTick >= resetStateDuration)
    {
        mode = IDLE;
        return;
    }
    else
    {
        if (currentTick - resetAnimationTick >= resetAnimationInterval )
        {
            if (resetAnimationState)
            {
                for (uint8_t i = 0; i < NUM_RINGS; i++)
                {
                    ring[i].fill(white);
                    ring[i].show();
                }
                resetAnimationState = false;
            } else {
                for (uint8_t i = 0; i < NUM_RINGS; i++)
                {
                    ring[i].fill(black);
                    ring[i].show();
                }
                resetAnimationState = true;
            }
            resetAnimationTick = currentTick;
        }
    }
}

//  =========== GAME STATE ======================================================

uint8_t targetDrum = 0;
uint8_t drumColour[NUM_DRUMS] = { 0, 1, 2, 3 };

// Uses a Fisher-Yates shuffle to change the drum colours
void permutateColours()
{
    for (uint8_t n = NUM_DRUMS; n > 1; n-- ) {
        uint8_t r = random(n);
        if (r != n) {
            uint8_t temp = drumColour[n-1];
            drumColour[n-1] = drumColour[r];
            drumColour[r] = temp;
        }
    }
}

uint32_t gameStateTick = 0;
uint32_t gameStateInterval = 1000/30; // 30FPS
uint32_t roundDuration = GAME_ROUND_INITIAL_TIMEOUT_MS;  // In milliseconds
uint32_t roundStartTick = 0;

// Updates all the lights to their corresponding colours!
void updateAllLights()
{
    for (int r = 0; r < NUM_RINGS - 1; r++)
    {
        ring[r].fill(colourPreset[drumColour[r]]);
        ring[r].show();
    }
    ring[NUM_RINGS-1].fill(colourPreset[drumColour[targetDrum]]);
    ring[NUM_RINGS-1].show();
}

void updateTargetLight()
{
    uint32_t progress = 16 * (currentTick - roundStartTick) / roundDuration;

    for (uint8_t i = 0; i < 16; i++)
    {
        if ( i > progress)
        {
            ring[NUM_RINGS-1].setPixelColor(i, colourPreset[drumColour[targetDrum]]);
        }
        else
        {
            ring[NUM_RINGS-1].setPixelColor(i, black);
        }
    }
    ring[NUM_RINGS-1].show();
}

void newRound()
{
    if (enable_serial_debug) Serial.println("NEW ROUND!");
    targetDrum = random(NUM_DRUMS);
    permutateColours();
    roundDuration = roundDuration - 100;
    roundStartTick = currentTick;

    updateAllLights();
}

void initGameState()
{
    if (enable_serial_debug) Serial.println("INIT GAME STATE");
    roundDuration = GAME_ROUND_INITIAL_TIMEOUT_MS + 100;
    newRound();
}

void updateGameState()
{
    if ( currentTick - gameStateTick >= gameStateInterval )
    {
        if (currentTick - roundStartTick >= roundDuration)
        {
            //  Round timeout! You have lost!
            mode = FAIL;
            return;
        }
        else
        {
            // Round hasn't timed out...
            // Check if a drum has been hit!
            int8_t triggeredDrum = getTriggeredDrum(); // Not unsigned as -1 is required for no drum touch!

            if (enable_serial_debug)
            {
                Serial.print(triggeredDrum);
                Serial.print(" ");
                Serial.print(targetDrum);
                Serial.println();
            }

            if (triggeredDrum >= 0)
            {
                // Drum strike!
                if (triggeredDrum == targetDrum)
                {
                    score += 1;
                    triggered = true;                   // For Modbus communication
                    triggeredDrumId = triggeredDrum;    // For Modbus communication
                    newRound();
                } else {
                    //  You touched the wrong drum!
                    mode = FAIL;
                }
            }
            else
            {
                updateTargetLight();
            }
        }
        gameStateTick = currentTick;
    }
}

//  ================== IDLE STATE ===============================================

// For now ... just timeout after a couple seconds and start a new game!

uint32_t initStartTick = 0;
uint32_t initStateInterval = 5000;

void initIdleState()
{
    if (enable_serial_debug) Serial.println("INIT IDLE STATE");
    initStartTick = currentTick;
}

void updateIdleState()
{
    if ( currentTick - initStartTick > initStateInterval )
    {
        mode = GAME;
    }
}

//  ================== MAIN GAME LOOP ===========================================

void updateGame()
{
    if ( mode != lastMode )
    {        
        // Initialise next mode
        switch (mode)
        {
        case IDLE:
            initIdleState();
            break;
        case BUSK:
            /* code */
            break;
        case GAME:
            initGameState();
            break;
        case FAIL:
            initResetState();
            break;
        default:
            break;
        }

        lastMode = mode;
    }

    // Update current mode
    switch (mode)
    {
        case IDLE:
            updateIdleState();
            break;
        case BUSK:
            /* code */
            break;
        case GAME:
            updateGameState();
            break;
        case FAIL:
            updateResetState();
            break;
        default:
            break;
    }
}