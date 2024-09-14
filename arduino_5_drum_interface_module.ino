#include <Adafruit_NeoPixel.h>
#include <Adafruit_MPR121.h>
#include <Wire.h>

//  Modbus Setup
//  Modified library to allow for multiple slaves!
#include <ModbusMaster.h>


//  VARIABLES THAT CAN BE EASILY CHANGED HERE!  ///////////////////////////////////////////////////

// Delay before and after talking to the RPI. Higher numbers improve the RPI communications significantly. But slow down responsiveness.
#define RPI_TRANSMISSION_DELAY 30

// Value between 0 and 255
// Note: 255 is blindingly bright!
#define LED_BRIGHTNESS 30

//  Colours used for the targets. Don't change the number of them, only their values.
//  All values are between 0 and 255. and Correspond to R, G, B in that order.
uint32_t colourPreset[] = {
    Adafruit_NeoPixel::Color(0,   0, 255),   //blue
    Adafruit_NeoPixel::Color(0,   255, 0),   //green
    Adafruit_NeoPixel::Color(255, 0,   0),   //red
    Adafruit_NeoPixel::Color(255, 255, 0)    //yellow
};

//  Game variables
//  The initial duration of the game in milliseconds, and the duration during the tutorial section.
#define GAME_ROUND_INITIAL_TIMEOUT_MS 4000
//  The mutliplier applied to each new round.
//  The closer to 1.00 the longer the game will last.
//  The lower the faster it will get really hard.
float roundTimeMultiplier = 0.95;
//  The fastest a round can be in milliseconds
uint16_t minimumRoundTime = 300;

//  If the game isn't able to communicate with the scissor lift this is how long the tutorial section will last in milliseconds
uint32_t timerDuration = 20000; 

//  Attract mode timings! All in milliseconds

//  The duration of time before ANY attract occurs
uint32_t initialAttractInterval = 5*60000;

//  The minimum and maximum time between attracts
uint32_t minAttractInterval = 4*60000;
uint32_t maxAttractInterval = 6*60000;

//  IDLE LED light colour change time
uint32_t LEDAttractInterval = 3000;

//  Reset delay after a game. This is the amount of time the leds will blink on and off AND the delay before the fail blast
uint32_t resetDelay = 6000;

//  Delay between changing the snake head attract mode
uint32_t snakeAttractInterval = 30000;

////////////////////////////////////////////////////////////////////////////////////////

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

uint32_t white = Adafruit_NeoPixel::Color(255,255,255);
uint32_t black = Adafruit_NeoPixel::Color(0,0,0);

#define NUM_DRUMS 4

Adafruit_MPR121 drums = Adafruit_MPR121();

#ifndef _BV
#define _BV(bit) (1 << (bit)) 
#endif

//  Serial commnication for Modbus requires disabling the serial communication for
//  debugging capacitative sensing and the drum gameplay.
const bool enable_serial_debug = false;
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
  FAIL = 3,
  TUTORIAL_OVER = 4
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
        ring[r].setBrightness(LED_BRIGHTNESS); //adjust brightness here
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
            if (enable_serial_debug)
            {
                Serial.print(i/2);
                Serial.println(" touched");
            }
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
    if (!drums.begin(0x5A, &Wire ,14, 6)) // ADDRESS, WIRE, TOUCH (Default 12), RELEASE (DEFAULT 6)
    {
        if (enable_serial_debug) Serial.println("MPR121 not found, check wiring?");
        while (1) {
          Serial.println(".");
        };
    }
    if (enable_serial_debug) Serial.println("MPR121 Initialised!");
}

// ============== COIN ACCEPTOR =================================================
uint8_t credits = 0;

void setupCoinAcceptor()
{
    if (enable_serial_debug) Serial.println("Initializing coin acceptor...");
    pinMode(A1, INPUT_PULLUP);
}

uint32_t coinInterval = 50;
uint32_t lastCoinTick = 0;

bool doubleCountGuard = false;

void updateCoinAcceptor()
{
    if (currentTick - lastCoinTick > coinInterval)
    {
        if (!digitalRead(A1) && doubleCountGuard == false)
        {
            credits = credits + 1;
            doubleCountGuard = true;

            if (!enable_serial_debug)
            {
                node.writeSingleRegister(2, credits, RPI);  //  LOWER scissor lift!
            }
            else
            {
              Serial.println("Credit!");
            }
        }
        else
        {
          doubleCountGuard = false;
        }
        lastCoinTick = currentTick;
    }
}

//  ============= GAMESTATES ====================================================

//  ============= GAME VARIABLES ================================================
uint16_t score = 0;
uint16_t tutorialScore = 0;
uint16_t hardScore = 0;
uint16_t highScore = 0;
uint16_t lastScore = 0;

bool tutorialSection = true;

//  ============= RESET STATE ===================================================
uint32_t resetStateTick = 0;
uint32_t resetStateDuration = resetDelay;
uint32_t resetStateLastTick = 0;
uint32_t resetStateInterval = 200;
uint32_t resetAnimationTick = 0;
uint32_t resetAnimationInterval = 500; // This is the update frequency of the black and white colours

bool resetAnimationState = true;

bool scissorResetStatusCheck = false;
bool snakeHeadResetStatusCheck = false;
bool snakeBodyResetStatusCheck = false;

uint8_t timeoutCount[7] = { 0 };

uint8_t maxTimeouts = 10;

bool scissorLiftOverride = false;
bool snakeBodyOverride = false;

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
        delay(RPI_TRANSMISSION_DELAY); //60
        node.writeSingleRegister(0, score, RPI);
        delay(RPI_TRANSMISSION_DELAY); //20
        node.writeSingleRegister(2, credits, RPI);
        delay(RPI_TRANSMISSION_DELAY); //20
        node.writeSingleRegister(1, FAIL, RPI);
        delay(RPI_TRANSMISSION_DELAY); //20
    }
}

bool checkResetStatus(uint8_t module)
{
    //  Make sure the Scissor lift has lowered!
    uint8_t result = node.readHoldingRegisters(0,1,module);

    if (result == 0)
    {
        if (node.getResponseBuffer(0x00) == 0)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        if (result == 0xE2)
        {
            timeoutCount[module] = timeoutCount[module] + 1;
        }
    }

    return false;
}

bool checkTimeouts()
{
    // Scissor lift
    if (timeoutCount[SCISSOR] > maxTimeouts)
    {
        scissorLiftOverride = true;
    }
    else
    {
        scissorLiftOverride = false;
    }

    // Snake bodies!
    if (timeoutCount[SNAKE_BODY] > maxTimeouts)
    {
        snakeBodyOverride = true;
    }
    else
        {
        snakeBodyOverride = false;
    }
}

void resetTimeouts()
{
    for( uint8_t c = 0; c < 7; c++ )
    {
        timeoutCount[c] = 0;
    }
}

bool failChaseDone = false;

void updateResetState()
{   
    if (currentTick - resetStateTick >= resetStateDuration)
    {
        if (currentTick - resetStateLastTick >= resetStateInterval)
        {
            if (!enable_serial_debug)
            {
                if (!failChaseDone)
                {
                    node.writeSingleCoil(11,1,SAXAPHONES); // All-chase
                    failChaseDone = true;
                }

                /*
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
                */

                checkTimeouts();

                //  Try to reset the snakehead, snakebody and scissorlift
                if (!snakeBodyResetStatusCheck)
                {
                    if (snakeBodyOverride)
                    {
                        snakeBodyResetStatusCheck = true;
                    }
                    else
                    {
                        if (checkResetStatus(SNAKE_BODY))
                        {
                            snakeBodyResetStatusCheck = true;
                        }
                        else
                        {
                            node.writeSingleRegister(0, 0, SNAKE_BODY);
                        }
                    }
                }

                if (!snakeHeadResetStatusCheck)
                {
                    if (checkResetStatus(SNAKE_HEAD))
                    {
                        snakeHeadResetStatusCheck = true;
                    }
                    else
                    {
                        node.writeSingleRegister(0, 0, SNAKE_HEAD);
                    }
                }

                if (!scissorResetStatusCheck)
                {
                    if (scissorLiftOverride)
                    {
                        scissorResetStatusCheck = true;
                    }
                    else 
                    {
                        if (checkResetStatus(SCISSOR))
                        {
                            scissorResetStatusCheck = true;
                        }
                        else
                        {
                            node.writeSingleRegister(0, LOWERED, SCISSOR);
                        }
                    }
                }
            }
            else
            {
                //  This enables automatic reset when testing the drum unit standalone
                scissorResetStatusCheck = true;
                snakeHeadResetStatusCheck = true;
                snakeBodyResetStatusCheck = true;
            }

            // For now, do not require snakehead to be reset!
            if (scissorResetStatusCheck && /*snakeHeadResetStatusCheck &&*/ snakeBodyResetStatusCheck)
            {
                //  All checks passed! Set to IDLE and reset checks for next time!
                mode = IDLE;
                scissorResetStatusCheck = false;
                snakeHeadResetStatusCheck = false;
                snakeBodyResetStatusCheck = false;

                failChaseDone = false;
            }
            resetStateLastTick = currentTick;
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

bool waitingRound = false;

void newRound()
{
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

    if (!waitingRound)
    {
        if (enable_serial_debug) Serial.println("NEW ROUND!");
        targetDrum = random(NUM_DRUMS);
        permutateColours();

        updateAllLights();
    }

    roundStartTick = currentTick;
}

bool initTutorialTimer = false;
uint32_t startTimer = 0;

void initGameState()
{
    score = 0;
    tutorialScore = 0;
    hardScore = 0;
    tutorialSection = true;
    initTutorialTimer = false;
    if (enable_serial_debug)
    {
        Serial.println("INIT GAME STATE");
    }
    else
    {
        delay(RPI_TRANSMISSION_DELAY); // 50
        node.writeSingleRegister(1, GAME, RPI);
        delay(RPI_TRANSMISSION_DELAY); // 10
    }
    roundDuration = GAME_ROUND_INITIAL_TIMEOUT_MS;
    newRound();
}

uint8_t checkTutorialSection()
{
    if (tutorialSection)
    {
        if (!enable_serial_debug)
        {
            if (!scissorLiftOverride)
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
                        waitingRound = true;
                        delay(RPI_TRANSMISSION_DELAY); // 50
                        node.writeSingleRegister(1, TUTORIAL_OVER, RPI);
                        delay(RPI_TRANSMISSION_DELAY); // 50
                    }
                }
            }
            else
            {
              //  This is a fallback for the serial debug state ... or if the scissorlift has failed and is no longer responding
                if (initTutorialTimer == false)
                {
                    initTutorialTimer = true;
                    startTimer = currentTick;
                }
                else
                {
                    if (currentTick - startTimer > timerDuration)
                    {
                        tutorialSection = false;
                        tutorialScore = score;
                        hardScore = 0;
                        node.writeSingleCoil(5, 1, SAXAPHONES);
                        waitingRound = true;
                        delay(RPI_TRANSMISSION_DELAY); // 50
                        node.writeSingleRegister(1, TUTORIAL_OVER, RPI);
                        delay(RPI_TRANSMISSION_DELAY); // 50

                        initTutorialTimer = false;
                    }
                }
            }
        }
        else
        {
            //  This is a fallback for the serial debug state ... or if the scissorlift has failed and is no longer responding
            if (initTutorialTimer == false)
            {
                initTutorialTimer = true;
                startTimer = currentTick;
            }
            else
            {
                if (currentTick - startTimer > timerDuration)
                {
                    tutorialSection = false;
                    tutorialScore = score;
                    hardScore = 0;
                    waitingRound = true;
                    initTutorialTimer = false;
                }
            }
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
                if (waitingRound)
                {
                    // Start a new game after waiting round times out!
                    waitingRound = false;
                    newRound();
                }
                else
                {
                    //  Round timeout! You have lost!
                    mode = FAIL;
                }
            }
            

            return;
        }
        else
        {
            if (waitingRound)
            {
                // Wait to time out!

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
                                    node.writeSingleCoil(triggeredDrum+1,1,SAXAPHONES);
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
    //  REWARDS SECTION!
                                if (hardScore % bigFlameScore == 0 )
                                {
                                    switch (multiple)
                                    {
                                    case 2:
                                        node.writeSingleCoil(6,1,SAXAPHONES);
                                        break;
                                    case 4:
                                        node.writeSingleCoil(7,1,SAXAPHONES);
                                        break;
                                    case 5:
                                        node.writeSingleCoil(5,1,SAXAPHONES);
                                        break;
                                    case 6:
                                        node.writeSingleCoil(6,1,SAXAPHONES);
                                        node.writeSingleCoil(7,1,SAXAPHONES);
                                        break;
                                    case 7:
                                        node.writeSingleCoil(9,1,SAXAPHONES);
                                        break;
                                    case 8:
                                        node.writeSingleCoil(5,1,SAXAPHONES);
                                        node.writeSingleCoil(6,1,SAXAPHONES);
                                        node.writeSingleCoil(7,1,SAXAPHONES);
                                        break;
                                    case 9:
                                        node.writeSingleCoil(10,1,SAXAPHONES);
                                        break;
                                    case 10:
                                        node.writeSingleCoil(11,1,SAXAPHONES);
                                        break;
                                    default:
                                        node.writeSingleCoil(triggeredDrum+1,1,SNAKE_HEAD);
                                    }

                                    // 0 - No hit!
                                    // 1 - 1
                                    // 2 - 2
                                    // 3 - 3
                                    // 4 - 4
                                    // 5 - Center Mirrorball
                                    // 6 - Left Mirrorball
                                    // 7 - Right Mirrorball
                                    // 8 - Horn
                                    // 9 - Mirrorball Chase
                                    // 10 - Instrument Chase
                                    // 11 - All chase
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
        }

        if (score != lastScore)
        {
            if (score >= 1 && tutorialSection == true)
            {
                //  While in the tutorial section and the score is rising keep sending raise messages
                if (!enable_serial_debug)
                {
                    if (!scissorLiftOverride)
                    {
                        node.writeSingleRegister(0, RISEN, SCISSOR);  //  RAISE scissor lift!
                    }
                    // Turn on Snakehead LEDs and mouth animations!
                    node.writeSingleRegister(0, 1, SNAKE_HEAD); // Animate!
                }
            }

            if (tutorialSection == false)
            {
                if (!enable_serial_debug)
                {
                    if (!snakeBodyOverride)
                    {
                        node.writeSingleRegister(0, 1, SNAKE_BODY); // Animate!
                    }
                    // Stop snake mouths from  opening!
                    node.writeSingleRegister(2, 2, SNAKE_HEAD);
                }
            }

            // Quick and dirty way to help RPi keep up with updates
            //  60ms delay (seems to allow 95% of score messages through)
            delay(RPI_TRANSMISSION_DELAY); // 60

            if (!enable_serial_debug) node.writeSingleRegister(0, score, RPI);

            lastScore = score;
        }

        gameStateTick = currentTick;
    }
}

//  ================== IDLE STATE ===============================================

uint32_t initStartTick = 0;
uint32_t initStateInterval = 1000/30;

uint32_t attractInterval = initialAttractInterval;
uint8_t attractOutput = 0;
uint32_t lastAttractTick = 0;

uint32_t lastLEDAttractTick = 0;
uint8_t LEDOutput = 0;

uint32_t lastSnakeAttractTick = 0;
uint8_t snakeAttractState = 0;

void initIdleState()
{
    if (enable_serial_debug) Serial.println("INIT IDLE STATE");
    initStartTick = currentTick;
    lastAttractTick = currentTick;      //  Saxaphones
    lastLEDAttractTick = currentTick;   //  Lights
    lastSnakeAttractTick = currentTick; //  Snake heads!

    if (enable_serial_debug)
    {
      Serial.println("INIT IDLE STATE");
    }
    else
    {
      delay(RPI_TRANSMISSION_DELAY); // 50
      node.writeSingleRegister(1, IDLE, RPI);
      delay(RPI_TRANSMISSION_DELAY); // 50
    }
}

void updateIdleState()
{
    if ( currentTick - initStartTick > initStateInterval )
    {
        //  Starts a new game automatically
        //  mode = GAME;

        if (credits > 0)
        {
            attractInterval = initialAttractInterval;
            credits = credits - 1;
            if (!enable_serial_debug)
            {
                
                delay(RPI_TRANSMISSION_DELAY); // 20
                node.writeSingleRegister(2, credits, RPI);
                delay(RPI_TRANSMISSION_DELAY); // 20
                node.writeSingleRegister(0,0,SNAKE_BODY);
            }
            mode = GAME;
        }
        else 
        {
            if (currentTick - lastLEDAttractTick > LEDAttractInterval)
            {
                //  Update all Lights in sequence!

                for (int r = 0; r < NUM_RINGS - 1; r++)
                {
                    ring[r].fill(colourPreset[drumColour[LEDOutput]]);
                    ring[r].show();
                }

                LEDOutput += 1;
                if (LEDOutput > 3)
                {
                    LEDOutput = 0;
                }
                lastLEDAttractTick = currentTick;
            }

            if (currentTick - lastAttractTick > attractInterval)
            {
                if (!enable_serial_debug)
                {
                    switch(attractOutput)
                    {
                    case 0:
                        //  Instrument chase
                        node.writeSingleCoil(10,1,SAXAPHONES);
                        attractOutput = 1;
                        break;
                    case 1:
                        //  Two central saxaphones
                        node.writeSingleCoil(2,1,SAXAPHONES);
                        node.writeSingleCoil(3,1,SAXAPHONES);
                        attractOutput = 2;
                        break;
                    case 2:
                        node.writeSingleCoil(1,1,SAXAPHONES);
                        node.writeSingleCoil(4,1,SAXAPHONES);
                        attractOutput = 3;
                        break;
                    case 3:
                        node.writeSingleCoil(10,1,SAXAPHONES);
                        attractOutput = 4;
                        break;
                    case 4:
                        node.writeSingleCoil(5,1,SAXAPHONES);
                        attractOutput = 0;
                        break;
                    default:
                        node.writeSingleCoil(10,1,SAXAPHONES);
                        attractOutput = 0;
                        break;
                    }
                }
                // OPTIONS
                // SAXAPHONE
                //node.writeSingleCoil(11,1,SAXAPHONES);
                    // 0 - No hit!
                    // 1 - 1
                    // 2 - 2
                    // 3 - 3
                    // 4 - 4
                    // 5 - Center Mirrorball
                    // 6 - Left Mirrorball
                    // 7 - Right Mirrorball
                    // 8 - Horn
                    // 9 - Mirrorball Chase
                    // 10 - Instrument Chase
                    // 11 - All chase

                lastAttractTick = currentTick;
                
                attractInterval = random(minAttractInterval, maxAttractInterval);
            }

            if (currentTick - lastSnakeAttractTick > snakeAttractInterval)
            {
                if(!enable_serial_debug)
                {
                    switch(snakeAttractState)
                    {
                        case 0:
                            node.writeSingleRegister(1,0,SNAKE_HEAD);   // Pulsing Eyes on!
                            snakeAttractState = 1;
                            break;
                        case 1:
                            node.writeSingleRegister(2,0,SNAKE_HEAD);   // Animate mouths
                            snakeAttractState = 2;
                            break;
                        case 2:
                            node.writeSingleRegister(2,2,SNAKE_HEAD);   // Close mouths
                            snakeAttractState = 3;
                            break;
                        case 3:
                            node.writeSingleRegister(0,3,SNAKE_BODY);   // Animate mouths
                            snakeAttractState = 4;
                            break;
                        case 4:
                            node.writeSingleRegister(0,0,SNAKE_BODY);   // Close mouths
                            snakeAttractState = 5;
                            break;
                        case 5:
                            node.writeSingleRegister(1,2,SNAKE_HEAD);   // Pulsing Eyes off!
                            snakeAttractState = 0;
                            break;
                        default:
                            snakeAttractState = 0;
                            break;

                    }
                    //  SNAKE HEAD options
                    //  node.writeSingleRegister(1,0,SNAKE_HEAD); // Pulsing Eyes on!
                    //  node.writeSingleRegister(1,1,SNAKE_HEAD); // Eyes on!
                    //  node.writeSingleRegister(1,2,SNAKE_HEAD); // Eyes off!

                    //  node.writeSingleRegister(2,0,SNAKE_HEAD); // Animate Mouths
                    //  node.writeSingleRegister(2,1,SNAKE_HEAD); // All open Mouths
                    //  node.writeSingleRegister(2,2,SNAKE_HEAD); // All close Mouths
                }

                lastSnakeAttractTick = currentTick;
            }
        }
    }
}

//  ================== MAIN GAME LOOP ===========================================

void updateGame()
{

    updateCoinAcceptor();

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
