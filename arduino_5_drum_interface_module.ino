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
//  Modified library to allow for multiple slaves!
#include <ModbusMaster.h>

const uint16_t id = 5;

const uint32_t baud = 115200;
const uint8_t config = SERIAL_8E1;
const uint16_t bufferSize = 256;
const uint8_t dePin = A0;

//  Modbus master functions
ModbusMaster node;

// idle callback function; gets called during idle time between TX and RX
void idle()
{
    delay(2);
}
// preTransmission callback function; gets called before writing a Modbus message
void preTransmission()
{
    // Figure out what this should be for a given baud!
    delay(2);
    digitalWrite(dePin, HIGH);
}
// postTransmission callback function; gets called after a Modbus message has been sent
void postTransmission()
{
    digitalWrite(dePin, LOW);
}

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

#define NUM_COLOUR_PRESETS 4

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

// Stuff that needs to be sent over modbus ... 
enum MODE {
  IDLE = 0,
  BUSK = 1,
  GAME = 2,
  FAIL = 3
};

int16_t setMode = IDLE;
uint32_t mode = IDLE;
uint32_t lastMode = IDLE;

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
    updateGame();
}

// ============== Modbus ========================================================

//  IDs for the Nodes on the system:
#define SNAKE_HEAD  1
#define SNAKE_BODY  2
#define SAXAPHONES  3
#define SCISSOR     4
//#define DRUM_MODULE 5
#define RPI         6



void modbusSetup()
{
    pinMode(dePin, OUTPUT);
    digitalWrite(dePin, LOW);

    Serial.begin(baud, config);

    node.begin(Serial);

    node.idle(idle);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);
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

// ============== Drums task ======================================================
void setupDrums()
{
    // Trying a 5sec autocalibration of baseline
    if (enable_serial_debug) Serial.println("Autocalibrating drums...");
    for (int d = 0; d < NUM_DRUMS; d++)
    {
        drum[d].set_CS_AutocaL_Millis(5000);
        // drum[d].set_CS_AutocaL_Millis(0xFFFFFFFF); // Disable the automatic re-calibration feature
        maxCapacitance[d] = INITIAL_ESTIMATED_MAX;
    }
    delay(5000);

    if (enable_serial_debug) Serial.println("Beginning game!");
}

//  ============= GAMESTATES ====================================================

//  ============= GAME VARIABLES ================================================
#define GAME_ROUND_INITIAL_TIMEOUT_MS 4000
uint8_t transitionScore = 10;
uint16_t score = 0;
uint16_t highScore = 0;
uint16_t lastScore = 0;

//  ============= RESET STATE ===================================================
uint32_t resetStateTick = 0;
uint32_t resetStateDuration = 4000;
uint32_t resetAnimationTick = 0;
uint32_t resetAnimationInterval = 500; // This is the update frequency of the black and white colours

bool resetAnimationState = true;

//bool scissorResetStatusCheck = false;

void initResetState()
{   
    if (enable_serial_debug) Serial.println("INIT RESET STATE");
    resetStateTick = currentTick;
    resetAnimationState = true;

    //  Start the process of resetting the scissor lift and snake bodies etc ...
//    uint8_t result = node.writeRegister(0, 0, SCISSOR);  //  LOWER scissor lift!

//    while (result != 0)
//    {
//        result = node.writeRegister(0, 0, SCISSOR);   //  Absolutely make sure this message has been sent!
//    }
}

void updateResetState()
{   
    if (currentTick - resetStateTick >= resetStateDuration)
    {

        //  Make sure the Scissor lift has lowered!
//        uint8_t result = node.readHoldingRegisters(0,1,SCISSOR);

//       if (result == 0)
//        {
//            if (node.getRespomseBuffer(0x00) == 0)
//            {
                //  The scissor lift has been lowered!
//                scissorResetStatusCheck = true;
//            }
//        }

//        if (scissorResetStatusCheck /* && snakeBodyResetStatusCheck etc ... */)
//        {
            //  All checks passed! Set to IDLE and reset checks for next time!
//            mode = IDLE;
//            scissorResetStatusCheck = false;
//        }
        mode = IDLE;
        return;
    }
    else
    {   
        //  This is the blinkey light animation!
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

// For debugging on  my stripped down system!
bool ledCountdown = true;

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
            if ( ledCountdown)
            {
              ring[NUM_RINGS-1].setPixelColor(i, black);
            }
            else
            {
              ring[NUM_RINGS-1].setPixelColor(i, colourPreset[drumColour[targetDrum]]);
            }
            
        }
    }
    ring[NUM_RINGS-1].show();
}

void newRound()
{
    if (enable_serial_debug) Serial.println("NEW ROUND!");
    targetDrum = random(NUM_DRUMS);
    permutateColours();
    if ( score < transitionScore )
    {
        // Do not change roundDuration ... leave at 4s
    }
    else
    {
        roundDuration = float(roundDuration) * 0.95;
    }
    
    roundStartTick = currentTick;

    updateAllLights();
}

void initGameState()
{
    score = 0;
    if (enable_serial_debug) Serial.println("INIT GAME STATE");
    roundDuration = GAME_ROUND_INITIAL_TIMEOUT_MS;
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
                    
                    // send trigger for saxaphone / snake flamethrowers from here!
                    if (score > transitionScore )
                    {
                        if (score %bigFlameScore == 0 )
                        {
                            node.writeSingleCoil(5,1,SAXAPHONES);
                        }
                        else
                        {
                            node.writeSingleCoil(triggeredDrum+1,1,SNAKE_HEAD);   //  Returns 0 on success!
                        }
                        
                    }
                    else
                    {
                        if (score %bigFlameScore == 0 )
                        {
                            node.writeSingleCoil(5,1,SAXAPHONES);
                        }
                        else
                        {
                            node.writeSingleCoil(triggeredDrum+1,1,SAXAPHONES);
                        }
                    }

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

        // I don't care if this fails for now ...
        uint8_t result = node.writeSingleRegister(0, score, RPI);

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
    //  Maybe something like ... if ( setMode != IDLE ) ...
    {
        //  Starts a new game automatically
        mode = GAME;

        //  Get the instruction from the controller to start a new game!
        if (setMode == GAME)
        {
            setMode = IDLE;
            mode = GAME;
        } // BUSK is another option!
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
