// **** Include libraries here ****
// Standard libraries
#include <stdio.h>

//CSE13E Support Library
#include "BOARD.h"
#include "Leds.h"
#include "Oled.h"
#include "OledDriver.h"
#include "Buttons.h"
#include "Ascii.h"
#include "Adc.h"


// Microchip libraries
#include <xc.h>
#include <sys/attribs.h>
#include <string.h>



// **** Set any macros or preprocessor directives here ****
// Set a macro for resetting the timer, makes the code a little clearer.
#define TIMER_2HZ_RESET() (TMR1 = 0)

#define LONG_PRESS_COUNT 5
#define ZERO 0
#define STARTUP_PERIOD 15
#define MINUTES 60

#define TEMP 2
#define TIME 10

#define TempTimeFlag 0x01
#define ModeFlag 0x02
#define CursorTickFlag  0x04
#define ValueFlag 0x08
#define CookFlag 0x10 //event flags for the oven
#define StopFlag 0x20
#define OverFlag 0x40

#define TextLength 20
#define CursorLength 2

#define minTemp 350
#define minTime 1
#define broilTemp 500
#define nukeTemp 1000

// **** Set any local typedefs here ****
typedef enum {
    SETUP, SELECTOR_CHANGE_PENDING, COOKING, COOK_OVER //oven states for state machine
} OvenState;

typedef enum {
    BAKE, TOAST, BROIL, NUKE //mode enums for oven cooking modes
} CookMode;

typedef struct OvenData {
    OvenState state; //state of oven
    int temp; //in degrees
    uint16_t time; //in seconds
    CookMode mode; //different enums defined above
    uint16_t cookStart; //start of cooking time to track time elapsed
    int TimeOrTemp; //whether editing time or temp
    uint8_t event; //flag to trigger event
    uint8_t eventFlag; //flags to know what to do with state machine
    int CursorOn; //whether cursor is blinking on or off
} OvenData;

// **** Declare any datatypes here ****

struct Timer {
    uint8_t event;
    int16_t timeRemaining;
};

struct AdcResult {
    uint8_t event;
    int16_t voltage;
};

// **** Define any module-level, global, or external variables here ****

const char Line1OFF[28] = "|" OVEN_TOP_OFF "" OVEN_TOP_OFF "" OVEN_TOP_OFF "" OVEN_TOP_OFF "" OVEN_TOP_OFF "|";
const char Line1ON[28] = "|" OVEN_TOP_ON "" OVEN_TOP_ON "" OVEN_TOP_ON "" OVEN_TOP_ON "" OVEN_TOP_ON "|";
const char Line2[28] = "|     |";
const char Line3[28] = "|_____|"; //variables to help with oven printing to oled
const char LineNuke1[28] =  "|*____|";
const char LineNuke2[28] =  "|_*___|";
const char LineNuke3[28] =  "|__*__|";
const char LineNuke4[28] =  "|___*_|";
const char LineNuke5[28] =  "|____*|";
const char LineNuke6[28] =  "|*----|";
const char LineNuke7[28] =  "|-*---|";
const char LineNuke8[28] =  "|--*--|";
const char LineNuke9[28] =  "|---*-|";
const char LineNuke10[28] =  "|----*|";
const char Line4OFF[28] = "|" OVEN_BOTTOM_OFF "" OVEN_BOTTOM_OFF "" OVEN_BOTTOM_OFF "" OVEN_BOTTOM_OFF "" OVEN_BOTTOM_OFF "|";
const char Line4ON[28] = "|" OVEN_BOTTOM_ON "" OVEN_BOTTOM_ON "" OVEN_BOTTOM_ON "" OVEN_BOTTOM_ON "" OVEN_BOTTOM_ON "|";

char screen[120] = "";

static uint8_t prevVal = ZERO;

struct Timer Timer1 = {FALSE, BUTTONS_DEBOUNCE_PERIOD};

uint8_t TIMER_TICK = FALSE;
uint16_t GLOBAL_TIMER = ZERO;

#define WINDOW 5
#define SMOLWINDOW 0.5
struct AdcResult ADC1 = {FALSE, ZERO};

#define BUTTON_STATE_3 0x20
#define BUTTON_STATE_4 0x80

struct OvenData Oven1 = {SETUP, FALSE, FALSE, BAKE, FALSE, FALSE, TRUE, FALSE, TRUE};

// **** Put any helper functions here ****


/*This function will update your OLED to reflect the state .*/
void updateOvenOLED(OvenData ovenData){
    if (!ovenData.cookStart && !ovenData.TimeOrTemp && !ovenData.temp && !ovenData.time) {
        //Initial Menu Setup
        sprintf(screen, "%s Warming up..\n%s\n%s\n%s", Line2, Line2, Line3, Line2);
        OledDrawString(screen);
        OledUpdate();
    }
    
    char tempText[TextLength] = "";
    char tempText2[TextLength] = "";
    char tempText3[TextLength] = "";
    char cursor[CursorLength] = " ";
    
    if (ovenData.state == SETUP) { //handles all screen printing during setup mode
        OledDriverSetDisplayNormal(); //in case coming back from inverted cook done screen
        if (ovenData.mode == BAKE) {
            strcpy(tempText, "Bake  ");
            sprintf(tempText2, "Time: %i:%02i", ovenData.time / MINUTES, ovenData.time % MINUTES);
            sprintf(tempText3, "Temp: %i%sF ", ovenData.temp, DEGREE_SYMBOL);
        }
        if (ovenData.mode == TOAST) {
            strcpy(tempText, "Toast ");
            sprintf(tempText2, "Time: %i:%02i", ovenData.time / 60, ovenData.time % 60);
            sprintf(tempText3, "           ");
        }
        if (ovenData.mode == BROIL) {
            strcpy(tempText, "Broil ");
            sprintf(tempText2, "Time: %i:%02i", ovenData.time / 60, ovenData.time % 60);
            sprintf(tempText3, "Temp: %i%sF ", broilTemp, DEGREE_SYMBOL);
        }
        if (ovenData.mode == NUKE) {
            strcpy(tempText, "*NUKE*");
            sprintf(tempText2, "Time: %i:%02i", ovenData.time / 60, ovenData.time % 60);
            sprintf(tempText3, "Temp: %i%sF", ovenData.temp, DEGREE_SYMBOL);
        }
        if (ovenData.CursorOn) {
            strcpy(cursor, ">");
        } else if (!ovenData.CursorOn) { //blinking cursor next to currently editable value (temp or time)
            strcpy(cursor, " ");
        }
        if (ovenData.TimeOrTemp == TIME) { //different screen setup whether time or temp is being edited
            sprintf(screen, "%s  Mode: %s\n%s %s%s\n%s  %s\n%s", Line1OFF, tempText, Line2, cursor, tempText2,  Line3, tempText3, Line4OFF);
        } else if (ovenData.TimeOrTemp == TEMP) {
            sprintf(screen, "%s  Mode: %s\n%s  %s\n%s %s%s\n%s", Line1OFF, tempText, Line2, tempText2,  Line3, cursor, tempText3, Line4OFF);
        }
    }
    
    if (ovenData.state == COOKING) { //handles screen printing while in cook mode
        uint16_t timeRemaining = ZERO;
        
        if (ovenData.time < ((GLOBAL_TIMER - ovenData.cookStart) / 5)) {
            timeRemaining = 0; //prevent overflow errors in time remaining
        } else {
            timeRemaining = ovenData.time - ((GLOBAL_TIMER - ovenData.cookStart) / 5);
        }
        printf("cookstart: %i\nstarttime: %i\ntime remaining: %i\nglobal timer: %i\n", ovenData.cookStart, ovenData.time, timeRemaining, GLOBAL_TIMER);
        
        if (ovenData.mode == BAKE) { //different screen setups based on cook mode
            strcpy(tempText, "Bake  ");
            sprintf(tempText2, "Time: %i:%02i", timeRemaining / MINUTES, timeRemaining % MINUTES);
            sprintf(tempText3, "Temp: %i%sF ", ovenData.temp, DEGREE_SYMBOL);
            sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1ON, tempText, Line2, tempText2,  Line3, tempText3, Line4ON);
        }
        if (ovenData.mode == TOAST) {
            strcpy(tempText, "Toast ");
            sprintf(tempText2, "Time: %i:%02i", timeRemaining / 60, timeRemaining % 60);
            sprintf(tempText3, "           ");
            sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1OFF, tempText, Line2, tempText2,  Line3, tempText3, Line4ON);
        }
        if (ovenData.mode == BROIL) {
            strcpy(tempText, "Broil ");
            sprintf(tempText2, "Time: %i:%02i", timeRemaining / 60, timeRemaining % 60);
            sprintf(tempText3, "Temp: %i%sF ", broilTemp, DEGREE_SYMBOL);
            sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1ON, tempText, Line2, tempText2,  Line3, tempText3, Line4OFF);
        }
        if (ovenData.mode == NUKE) {
            strcpy(tempText, "*NUKE*");
            sprintf(tempText2, "Time: %i:%02i", timeRemaining / 60, timeRemaining % 60);
            sprintf(tempText3, "Temp: %i%sF", ovenData.temp, DEGREE_SYMBOL);
            switch (GLOBAL_TIMER % 8) { //special animation during nuke mode cooking (check it out!)
                case 0:
                    sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1ON, tempText, LineNuke10, tempText2,  LineNuke1, tempText3, Line4ON);
                    break;
                case 1:
                case 7:
                    sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1ON, tempText, LineNuke9, tempText2,  LineNuke2, tempText3, Line4ON);
                    break;
                case 2:
                case 6:
                    sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1ON, tempText, LineNuke8, tempText2,  LineNuke3, tempText3, Line4ON);
                    break;
                case 3:
                case 5:
                    sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1ON, tempText, LineNuke7, tempText2,  LineNuke4, tempText3, Line4ON);
                    break;
                case 4:
                    sprintf(screen, "%s  Mode: %s\n%s  %s\n%s  %s\n%s", Line1ON, tempText, LineNuke6, tempText2,  LineNuke5, tempText3, Line4ON);
                    break;
            }
        }
        
        LEDS_SET((0xFF >> (timeRemaining * 8) / ovenData.time) ^ 0xFF); //using bit arithmetic to display time left on leds
    }
    
    if (ovenData.state == COOK_OVER) { //handles display inverting when cook is over
        if (ovenData.CursorOn) { //utilizes cursor blinking timer to invert screen
            OledDriverSetDisplayInverted(); //utilize oleddriver.h function that inverts display
            LEDS_SET(0xFF); //flashes leds along with oled screen when cooking is over
            printf("inverting display\n");
        } else if (!ovenData.CursorOn) {
            OledDriverSetDisplayNormal();
            LEDS_SET(0x00);
            printf("display normal\n");
        }
    }
    
    if (GLOBAL_TIMER % 15 == 0) { //clear screen every 3 seconds to prevent weird oled artifacts
        OledClear(OLED_COLOR_BLACK);
        OledUpdate(); //clear screen before updating to prevent weirdness
    }
    
    OledDrawString(screen); //write to oled screen
    OledUpdate();
}

/*This function will execute your state machine.  
 * It should ONLY run if an event flag has been set.*/
void runOvenSM(void)
{
    if (!Oven1.TimeOrTemp && !Oven1.temp && !Oven1.cookStart && !Oven1.time) { //only during startup
        updateOvenOLED(Oven1); //initial menu boot
        Oven1.TimeOrTemp = TIME; //editing time
        Oven1.cookStart = ZERO;
        Oven1.temp = minTemp;
        Oven1.time = minTime;
    }
    
    if (GLOBAL_TIMER < STARTUP_PERIOD) { //initial boot sequence for fun
        return;
    }
    
    printf("Eventflag: %x\n", Oven1.eventFlag);
    
    if ((Oven1.eventFlag & ModeFlag) >> 1) { //flag to change mode of oven (from bake to toast, etc.)
        Oven1.eventFlag ^= ModeFlag;
        if (Oven1.state == SETUP) {
            if (Oven1.mode == BAKE) {
                Oven1.mode = TOAST;
                Oven1.TimeOrTemp = TIME;
                //printf("changing to toast\n");
            } else if (Oven1.mode == TOAST) {
                Oven1.mode = BROIL;
                Oven1.TimeOrTemp = TIME;
                //printf("changing to broil\n");
            } else if (Oven1.mode == BROIL) {
                Oven1.mode = NUKE;
                Oven1.temp = nukeTemp;
                Oven1.time = minTime;
                //printf("changing to nuke\n");
            } else if (Oven1.mode == NUKE) {
                Oven1.mode = BAKE;
                Oven1.temp = minTemp;
                //printf("changing to bake\n");
            }
        }
    }
    
    if ((Oven1.eventFlag & CursorTickFlag) >> 2) { //blinks the cursor for ui reasons
        Oven1.eventFlag ^= CursorTickFlag;
        if (Oven1.CursorOn) {
            Oven1.CursorOn = FALSE;
        } else if (!Oven1.CursorOn) { //toggles the cursor on and off periodically
            Oven1.CursorOn = TRUE;
        }
    }
    
    if ((Oven1.eventFlag & TempTimeFlag)) { //toggles between editing time and temp for applicable oven modes
        Oven1.eventFlag ^= TempTimeFlag;
        if (Oven1.mode == BAKE || Oven1.mode == NUKE) {
            if (Oven1.TimeOrTemp == TIME) {
                Oven1.TimeOrTemp = TEMP;
            } else if (Oven1.TimeOrTemp == TEMP) {
                Oven1.TimeOrTemp = TIME;
            }
        }
    }
    
    if ((Oven1.eventFlag & ValueFlag) >> 3) { //edits time and/or temperature based on potentiometer value
        Oven1.eventFlag ^= ValueFlag;
        if (Oven1.state == SETUP) {
            if (Oven1.TimeOrTemp == TIME) {
                if (Oven1.mode == NUKE) {
                    Oven1.time = (ADC1.voltage >> 5) + 1; //nuke mode has different time range than other modes
                } else {
                    Oven1.time = (ADC1.voltage >> 2) + 1;
                }
            } else if (Oven1.TimeOrTemp == TEMP) {
                if (Oven1.mode == BAKE) {
                    Oven1.temp = (ADC1.voltage >> 2) + 300;
                } else if (Oven1.mode == NUKE) {
                    Oven1.temp = (ADC1.voltage) * 4 + 1000; //nuke mode also has different temp range than other modes
                }
            }
        }
    }
    
    if ((Oven1.eventFlag & CookFlag) >> 4) { //when button 3 is short-pressed, go from setup to cooking
        Oven1.eventFlag ^= CookFlag;
        if (Oven1.state == SETUP) {
            printf("going from setup to cooking \n");
            Oven1.state = COOKING;
            Oven1.cookStart = GLOBAL_TIMER;
        }
    }
    
    if ((Oven1.eventFlag & StopFlag) >> 5) { //when in cook mode and button 4 is held, exit cook mode
        Oven1.eventFlag ^= StopFlag;
        if (Oven1.state == COOKING || Oven1.state == COOK_OVER) {
            Oven1.state = SETUP;
            LEDS_SET(0x00); //turn off progress leds
        }
    }
    
    if ((Oven1.eventFlag & OverFlag) >> 6) { //when cooking completes, go into display invert mode
        Oven1.eventFlag ^= OverFlag;
        if (Oven1.state == COOKING) {
            Oven1.state = COOK_OVER;
        }
    }
    
    updateOvenOLED(Oven1); //call oven oled updater function based on values edited above
}


int main()
{
    BOARD_Init();

     //initalize timers and timer ISRs:
    // <editor-fold defaultstate="collapsed" desc="TIMER SETUP">
    
    // Configure Timer 2 using PBCLK as input. We configure it using a 1:16 prescalar, so each timer
    // tick is actually at F_PB / 16 Hz, so setting PR2 to F_PB / 16 / 100 yields a .01s timer.

    T2CON = 0; // everything should be off
    T2CONbits.TCKPS = 0b100; // 1:16 prescaler
    PR2 = BOARD_GetPBClock() / 16 / 100; // interrupt at .5s intervals
    T2CONbits.ON = 1; // turn the timer on

    // Set up the timer interrupt with a priority of 4.
    IFS0bits.T2IF = 0; //clear the interrupt flag before configuring
    IPC2bits.T2IP = 4; // priority of  4
    IPC2bits.T2IS = 0; // subpriority of 0 arbitrarily 
    IEC0bits.T2IE = 1; // turn the interrupt on

    // Configure Timer 3 using PBCLK as input. We configure it using a 1:256 prescaler, so each timer
    // tick is actually at F_PB / 256 Hz, so setting PR3 to F_PB / 256 / 5 yields a .2s timer.

    T3CON = 0; // everything should be off
    T3CONbits.TCKPS = 0b111; // 1:256 prescaler
    PR3 = BOARD_GetPBClock() / 256 / 5; // interrupt at .5s intervals
    T3CONbits.ON = 1; // turn the timer on

    // Set up the timer interrupt with a priority of 4.
    IFS0bits.T3IF = 0; //clear the interrupt flag before configuring
    IPC3bits.T3IP = 4; // priority of  4
    IPC3bits.T3IS = 0; // subpriority of 0 arbitrarily 
    IEC0bits.T3IE = 1; // turn the interrupt on;

    // </editor-fold>
   
    printf("Welcome to Ayman's Lab07 (Toaster Oven).  Compiled on %s %s.\n", __TIME__, __DATE__);

    //initialize state machine (and anything else you need to init) here
    LEDS_INIT();
    OledInit(); //init absolutely everything
    ButtonsInit();
    AdcInit();
    //variables to make life easier
    uint8_t superPrevVal = prevVal;
    uint16_t Btn3ticks = ZERO;
    uint16_t Btn4ticks = ZERO; //variables to tell whether a button is being held or pressed short
    uint8_t Btn3HOLD = FALSE;
    uint8_t Btn4HOLD = FALSE;

    while (1){
        
        if (Oven1.event) {
            //printf("Oven1 event triggered \n");
            Oven1.event = FALSE;
            runOvenSM(); //event flag, separate from the actual oven flags
        }
        
        if (Timer1.event) { //timer isr for buttons and potentiometer
            Timer1.event = FALSE;
            //printf("Timer event detected \n");
            //printf("Preval: %x \n", prevVal);
            //printf("SPreval: %x \n", superPrevVal);
            
            if (superPrevVal == prevVal) { //initial startup, skip
                continue;
            }
            
            if ((superPrevVal & BUTTON_STATE_3) == (prevVal & BUTTON_STATE_3)) {} else { //if current button value is different than before
                if ((prevVal & BUTTON_STATE_3) >> 2) { //bitshifting to read button3 value
                    printf("Button 3 Down\n");
                    Btn3HOLD = TRUE;
                    Btn3ticks = GLOBAL_TIMER;
                } else {
                    printf("Button 3 Up\n");
                    if (Btn3HOLD) {
                        printf("Btn3 Short Press\n");
                        Btn3HOLD = FALSE;
                        Oven1.eventFlag |= ModeFlag;
                        Oven1.event = TRUE;
                    }
                }
            }
            
            if ((superPrevVal & BUTTON_STATE_4) == (prevVal & BUTTON_STATE_4)) {} else {//button state comparing same as above
                if ((prevVal & BUTTON_STATE_4) >> 3) { //bitshifting same as above
                    printf("Button 4 Down\n");
                    Btn4HOLD = TRUE;
                    Btn4ticks = GLOBAL_TIMER;
                } else {
                    printf("Button 4 Up\n");
                    if (Btn4HOLD) {
                        printf("Btn4 Short Press\n");
                        Btn4HOLD = FALSE;
                        Oven1.eventFlag |= CookFlag;
                        Oven1.event = TRUE;
                    }
                }
            }
            superPrevVal = prevVal; //save current button states for comparison later
        }
        
        if (TIMER_TICK) {
            TIMER_TICK = FALSE;
            if (Btn3HOLD) {
                if ((GLOBAL_TIMER - Btn3ticks) >= LONG_PRESS_COUNT) { //if button state doesn't change from down, long press
                    printf("Btn3 Long Press\n");
                    Btn3HOLD = FALSE;
                    Oven1.eventFlag |= TempTimeFlag; //button 3 long press toggles editing time or temp
                    Oven1.event = TRUE;
                }
            }
            if (Btn4HOLD) {
                if ((GLOBAL_TIMER - Btn4ticks) >= LONG_PRESS_COUNT) { //same as above
                    printf("Btn4 Long Press \n");
                    Btn4HOLD = FALSE;
                    Oven1.eventFlag |= StopFlag; //button 4 long press exits cook mode
                    Oven1.event = TRUE;
                }
            }
            if (!(GLOBAL_TIMER % 3)) {
                Oven1.eventFlag |= CursorTickFlag; //enables the cursor to blink at a good frequency
                Oven1.event = TRUE;
            }
            if (!((GLOBAL_TIMER) % 5)) {
                Oven1.event = TRUE; //for more accurate countdown while cooking
            }
            if (Oven1.mode == NUKE && Oven1.state == COOKING) {
                Oven1.event = TRUE; //for more accurate animations during nuke mode
            }
            if ((Oven1.time - ((GLOBAL_TIMER - Oven1.cookStart) / 5)) < ZERO) {
                Oven1.eventFlag |= OverFlag; //if enough time has elapsed in cook mode, exit cook mode
                Oven1.event = TRUE;
            }
            if ((Oven1.time + 4 - (GLOBAL_TIMER - Oven1.cookStart) / 5) < ZERO) {
                Oven1.eventFlag |= StopFlag; //invert screen for about 4 seconds before returning to setup mode
                Oven1.event = TRUE;
            }
        }
        if (ADC1.event) {
            ADC1.event = FALSE;
            printf("Voltage: %iv, (%i%%)\n", ADC1.voltage, (ADC1.voltage * 100) / 1023);
            Oven1.eventFlag |= ValueFlag; //if potentiometer voltage change detected, edit time/temp accordingly
            Oven1.event = TRUE;
        }
        // Add main loop code here:
        // check for events
        // on event, run runOvenSM()
        // clear event flags
    };
}


/*The 5hz timer is used to update the free-running timer and to generate TIMER_TICK events*/
void __ISR(_TIMER_3_VECTOR, ipl4auto) TimerInterrupt5Hz(void)
{
    // Clear the interrupt flag.
    IFS0CLR = 1 << 12;

    TIMER_TICK = TRUE;
    GLOBAL_TIMER++; //global timer for animations, button pressed, etc.
}


/*The 100hz timer is used to check for button and ADC events*/
void __ISR(_TIMER_2_VECTOR, ipl4auto) TimerInterrupt100Hz(void)
{
    // Clear the interrupt flag.
    IFS0CLR = 1 << 8;

    uint8_t buttonStates = ButtonsCheckEvents();
    /*
    if (prevVal != buttonStates) { //if button state is different that previous
        Timer1.timeRemaining = BUTTONS_DEBOUNCE_PERIOD; //reset debounce timer
        prevVal = buttonStates; //save current state for future reference
        printf("idk: %x\n", prevVal);
    } else {
        Timer1.timeRemaining--; //decrement debounce timer
    }
    
    if (!Timer1.timeRemaining) { //once debounce timer reaches zero, trigger event
        Timer1.event = TRUE;
        printf("Buttonstates: %d\n", prevVal);
    }
     */
    if (prevVal != buttonStates) { //if button state is different that previous
        if (!buttonStates) { //if buttonStates is returning BUTTON_EVENT_NONE
            ; //prevents weird behavior after a button event
        } else {
            prevVal = buttonStates; //save current state for future use
            Timer1.event = TRUE; //debouncing is already done for us?!?!
        }
    }
    
    //smoothing out potentiometer values before alerting the rest of the program
    int currValue = (ADC1BUF0 + ADC1BUF1 + ADC1BUF2 + ADC1BUF3 + ADC1BUF4 + ADC1BUF5 + ADC1BUF6 + ADC1BUF7) / 8;
    //printf("Current Value: %i\n", currValue);
    //average of last 8 adc1 values
    if (ADC1.voltage - currValue > WINDOW || ADC1.voltage - currValue < -WINDOW) {
        ADC1.voltage = currValue; //if the difference between previous and current value is more than 5, update value
        ADC1.event = TRUE;
    } 
    if ((currValue < 4 || currValue > 1018 ) && (ADC1.voltage - currValue > SMOLWINDOW || ADC1.voltage - currValue < -SMOLWINDOW)) {
        ADC1.voltage = currValue; //or if the current value is close to upper and lower boundaries, update value
        ADC1.event = TRUE;
    }
}