#include <Adafruit_NeoPixel.h>
#include <Adafruit_MPR121.h>
#include <Wire.h>

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
    Adafruit_NeoPixel::Color(0,   0, 255),   //blue
    Adafruit_NeoPixel::Color(0,   255, 0),   //green
    Adafruit_NeoPixel::Color(255, 0,   0),   //red
    Adafruit_NeoPixel::Color(255, 255, 0),   //yellow
};

uint32_t white = Adafruit_NeoPixel::Color(255,255,255);
uint32_t black = Adafruit_NeoPixel::Color(0,0,0);

#define NUM_DRUMS 4

Adafruit_MPR121 drums = Adafruit_MPR121();

#ifndef _BV
#define _BV(bit) (1 << (bit)) 
#endif

//  Serial commnication for Modbus requires disabling the serial communication for
//  debugging capacitative sensing and the drum gameplay.
const bool enable_serial_debug = true;
const bool enable_drum_debug = true; // Requires enable serial debug to be true also!

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
    setupCoinAcceptor();
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

//  SCISSOR LIFT COMMANDS
#define LOWERED 0
#define RISEN 2
#define STOP 4


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

uint8_t lastBestDrumId = -1;

uint16_t lastTouched = 0;
uint16_t currentTouch = 0;

int8_t getTriggeredDrum()
{
    currentTouch = drums.touched();

    if (enable_serial_debug)
    {
        if (enable_drum_debug)
        {
            Serial.print("\t\t\t\t\t\t\t\t\t\t\t\t\t 0x"); Serial.println(drums.touched(), HEX);
            Serial.print("Filt: ");
            for (uint8_t i = 0; i < 12; i++)
            {
                Serial.print(drums.filteredData(i)); Serial.print("\t");
            }
            Serial.println();
            Serial.print("Base: ");
            for (uint8_t i = 0; i < 12; i++)
            {
                Serial.print(drums.baselineData(i)); Serial.print("\t");
            }
            Serial.println();
        }
    }

    for (uint8_t i = 0; i < 12; i++)
    {
        // it if *is* touched and *wasnt* touched before, alert!
        if ((currentTouch & _BV(i)) && !(lastTouched & _BV(i)) )
        {
            if (enable_serial_debug) Serial.print(i/2); Serial.println(" touched");
            return i/2;
        }
        // if it *was* touched and now *isnt*, alert!
        //if (!(currentTouch & _BV(i)) && (lastTouched & _BV(i)) )
        //{
        //    Serial.print(i); Serial.println(" released");
        //}
    }

    lastTouched = currentTouch;
    
    return -1;
}

// ============== Drums task ======================================================
void setupDrums()
{
    // Default address is 0x5A, if tied to 3.3V its 0x5B
    // If tied to SDA its 0x5C and if SCL then 0x5D
    if (!drums.begin(0x5A))
    {
        if (enable_serial_debug) Serial.println("MPR121 not found, check wiring?");
        while (1) {
          Serial.println(".");
        };
    }
    if (enable_serial_debug) Serial.println("MPR121 Initialised!");
}

// ============== COIN ACCEPTOR =================================================

void setupCoinAcceptor()
{
    if (enable_serial_debug) Serial.println("Initializing coin acceptor...");
    pinMode(A6, INPUT_PULLUP);
}

bool updateCoinAcceptor()
{
    return !digitalRead(A6);
}

//  ============= GAMESTATES ====================================================

//  ============= GAME VARIABLES ================================================
#define GAME_ROUND_INITIAL_TIMEOUT_MS 4000
float roundTimeMultiplier = 0.95;
uint16_t minimumRoundTime = 300;
uint16_t score = 0;
uint16_t tutorialScore = 0;
uint16_t hardScore = 0;
uint16_t highScore = 0;
uint16_t lastScore = 0;

bool tutorialSection = true;

//  ============= RESET STATE ===================================================
uint32_t resetStateTick = 0;
uint32_t resetStateDuration = 4000;
uint32_t resetAnimationTick = 0;
uint32_t resetAnimationInterval = 500; // This is the update frequency of the black and white colours

bool resetAnimationState = true;

bool scissorResetStatusCheck = false;

void initResetState()
{   
    if (enable_serial_debug) Serial.println("INIT RESET STATE");
    resetStateTick = currentTick;
    resetAnimationState = true;

    //  Start the process of resetting the scissor lift and snake bodies etc ...
    if (!enable_serial_debug)
    {
        node.writeSingleRegister(0, LOWERED, SCISSOR);  //  LOWER scissor lift!
        node.writeSingleRegister(0, 0, SNAKE_HEAD);
        // Make sure there is enough time to update all the score / mode information
        delay(60);
        uint8_t result = node.writeSingleRegister(0, score, RPI);
    }
}

void updateResetState()
{   
    if (currentTick - resetStateTick >= resetStateDuration)
    {
        if (!enable_serial_debug)
        {
            //  Make sure the Scissor lift has lowered!
            uint8_t result = node.readHoldingRegisters(0,1,SCISSOR);

            if (result == 0)
            {
                if (node.getResponseBuffer(0x00) == 0)
                {
                    //  The scissor lift has been lowered!
                    scissorResetStatusCheck = true;
                }
                else
                {
                    //  If it returns a state of anything other than lowered, it will request again to lower the scissor list
                    uint8_t result = node.writeSingleRegister(0, LOWERED, SCISSOR);
                }
            }
        }
        else
        {
            //  This enables automatic reset when testing the drum unit standalone
            scissorResetStatusCheck = true;
        }

        if (scissorResetStatusCheck /* && snakeBodyResetStatusCheck etc ... */)
        {
            //  All checks passed! Set to IDLE and reset checks for next time!
            mode = IDLE;
            scissorResetStatusCheck = false;
        }
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
uint32_t gameStateInterval = 1000/20; // 30FPS
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

void updateAllDrumLightsToTarget()
{
    for (int r = 0; r < NUM_RINGS - 1; r++)
    {
        ring[r].fill(colourPreset[drumColour[targetDrum]]);
        ring[r].show();
    }
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
            if (ledCountdown)
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

// Tutorial Section info ...

// tutorialSection should be used directly, not checktutorialSection() as this might change after the round starts!
bool tutorialRoundSuccess = false;

void newRound()
{
    if (enable_serial_debug) Serial.println("NEW ROUND!");
    targetDrum = random(NUM_DRUMS);
    permutateColours();
    if ( tutorialSection )
    {
        // Do not change roundDuration ... leave at 4s!
        checkTutorialSection();
        tutorialRoundSuccess = false;
    }
    else
    {
        roundDuration = float(roundDuration) * roundTimeMultiplier;
        
        if (roundDuration <= minimumRoundTime)
        {
            roundDuration = minimumRoundTime;
        }
    }
    
    roundStartTick = currentTick;

    updateAllLights();
}

void initGameState()
{
    score = 0;
    tutorialScore = 0;
    hardScore = 0;
    tutorialSection = true;
    if (enable_serial_debug) Serial.println("INIT GAME STATE");
    roundDuration = GAME_ROUND_INITIAL_TIMEOUT_MS;
    newRound();
}

uint8_t checkTutorialSection()
{
    if (tutorialSection)
    {
        if (!enable_serial_debug)
        {
            //  The tutorial section is when the lift is still rising!
            uint8_t result = node.readHoldingRegisters(0, 1, SCISSOR);

            if (result == 0)
            {
                if (node.getResponseBuffer(0x00) == RISEN)
                {
                    //  The scissor lift has risen!
                    tutorialSection = false;
                    tutorialScore = score;
                    hardScore = 0;
                    //  Fire SAXAPHONE 5! Boom!
                    node.writeSingleCoil(5, 1, SAXAPHONES);
                }
            }
        }
        else
        {
            //  For debugging purposes we don't need to worry about the tutorial section!
        }
    }
}

void updateGameState()
{
    if ( currentTick - gameStateTick >= gameStateInterval )
    {
        if (currentTick - roundStartTick >= roundDuration)
        {
            if (tutorialSection)
            {
                if (tutorialRoundSuccess)
                {
                    newRound();
                }
                else
                {
                    mode = FAIL;
                }
            }
            else
            {
                //  Round timeout! You have lost!
                mode = FAIL;
            }
            

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
                if (tutorialSection)
                {
                    if (triggeredDrum == targetDrum)
                    {
                        if (!tutorialRoundSuccess)
                        {
                            score += 1;
                            tutorialRoundSuccess = true;

                            updateAllDrumLightsToTarget();

                            if (!enable_serial_debug)
                            {
                                if (score % bigFlameScore == 0 )
                                {
                                    node.writeSingleCoil(5,1,SAXAPHONES);
                                }
                                else
                                {
                                    node.writeSingleCoil(triggeredDrum+1,1,SAXAPHONES);
                                }
                            }
                        }
                    }
                    else
                    {
                        //  You touched the wrong drum!
                        if (currentTick - roundStartTick > 200)
                        {
                            if (tutorialRoundSuccess)
                            {
                                //  Ignore any incorrect touches after a success!
                            }
                            else
                            {
                                mode = FAIL;
                            }
                            
                        }
                        else
                        {
                            // This should give a bit of break against the same drum immidiately triggering
                            // Have another go!
                        }
                    }

                }
                else
                {
                    if (triggeredDrum == targetDrum)
                    {
                        
                        score += 1;
                        
                        // send trigger for saxaphone / snake flamethrowers from here!
                        if (!enable_serial_debug)
                        {
                            hardScore += 1; // Also add score to hard score section!

                            uint8_t multiple = hardScore / bigFlameScore;

                            if (hardScore % bigFlameScore == 0 )
                            {
                                switch (multiple)
                                {
                                case 0:
                                    node.writeSingleCoil(triggeredDrum+1,1,SNAKE_HEAD);
                                    break;
                                case 1:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    break;
                                case 2:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    node.writeSingleCoil(4,1,SAXAPHONES);
                                    break;
                                case 3:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    node.writeSingleCoil(2,1,SAXAPHONES);
                                    node.writeSingleCoil(4,1,SAXAPHONES);
                                    break;
                                case 4:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    node.writeSingleCoil(2,1,SAXAPHONES);
                                    node.writeSingleCoil(3,1,SAXAPHONES);
                                    node.writeSingleCoil(4,1,SAXAPHONES);
                                    break;
                                case 5:
                                    node.writeSingleCoil(5,1,SAXAPHONES);
                                    break;
                                case 6:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    node.writeSingleCoil(5,1,SAXAPHONES);
                                    break;
                                case 7:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    node.writeSingleCoil(4,1,SAXAPHONES);
                                    node.writeSingleCoil(5,1,SAXAPHONES);
                                    break;
                                case 8:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    node.writeSingleCoil(2,1,SAXAPHONES);
                                    node.writeSingleCoil(4,1,SAXAPHONES);
                                    node.writeSingleCoil(5,1,SAXAPHONES);
                                    break;
                                default:
                                    node.writeSingleCoil(1,1,SAXAPHONES);
                                    node.writeSingleCoil(2,1,SAXAPHONES);
                                    node.writeSingleCoil(3,1,SAXAPHONES);
                                    node.writeSingleCoil(4,1,SAXAPHONES);
                                    node.writeSingleCoil(5,1,SAXAPHONES);
                                    break;
                                }
                            }
                            else
                            {
                                node.writeSingleCoil(triggeredDrum+1,1,SNAKE_HEAD);   //  Returns 0 on success!
                            }
                        }

                        newRound();

                    } else {
                        //  You touched the wrong drum!
                        if (currentTick - roundStartTick > 200)
                        {
                            mode = FAIL;
                        }
                        else
                        {
                            // This should give a bit of break against the same drum immidiately triggering
                            // Have another go!
                        }
                    }
                }
            }
            else
            {
                updateTargetLight();
            }
        }

        if (score != lastScore)
        {
            if (score >= 1 && tutorialSection == true)
            {
                //  While in the tutorial section and the score is rising keep sending raise messages
                if (!enable_serial_debug)
                {
                    node.writeSingleRegister(0, RISEN, SCISSOR);  //  RAISE scissor lift!
                    // Turn on Snakehead LEDs and mouth animations!
                    node.writeSingleRegister(0, 1, SNAKE_HEAD); // Animate!
                }
            }

            // Quick and dirty way to help RPi keep up with updates
            //  60ms delay (seems to allow 95% of score messages through)
            delay(60);

            if (!enable_serial_debug) node.writeSingleRegister(0, score, RPI);

            lastScore = score;
        }

        gameStateTick = currentTick;
    }
}

//  ================== IDLE STATE ===============================================

// For now ... just timeout after a couple seconds and start a new game!

uint32_t initStartTick = 0;
uint32_t initStateInterval = 1000/30;

void initIdleState()
{
    if (enable_serial_debug) Serial.println("INIT IDLE STATE");
    initStartTick = currentTick;
}

void updateIdleState()
{
    if ( currentTick - initStartTick > initStateInterval )
    {
        //  Starts a new game automatically
        //  mode = GAME;

        //  Check if the start button is pressed!
        if (updateCoinAcceptor())
        {
            mode = GAME;
        }
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
