/*
  EV1527 Window Sensor Monitor for homee
  
  This program monitors signals coming from an EV1527 chip, filters them and sends them via I2C to a slave device.
  It can handle multiple sensors and supports a FIFO buffer to manage incoming signals.
  
  Hardware: Arduino Nano with 433 MHz receiver module (rbx14 or similar)

  Libraries:
  - RCSwitch: https://github.com/sui77/rc-switch/
*/


#include <Arduino.h>
#include <RCSwitch.h>
#include <Wire.h>
#include "global_defines.h"

#define DIN_RECEIVER        2           // interrupt pin for 433 MHz receiver
#define DOUT_POWER          5           // pin to control power for receiver module

#ifdef DEBUG
    #define FILTER_TIME_MS      1000     // duplicate filter timeout
#else
    #define FILTER_TIME_MS      300     // duplicate filter timeout
#endif    

#define MAX_PAUSE_INTERVAL  10000       // dummy code timeout

#define FIFO_SIZE 32                    // max buffered codes

RCSwitch mySwitch = RCSwitch();

// FIFO ring buffer
unsigned long fifoBuffer[FIFO_SIZE];
uint8_t fifoHead = 0;
uint8_t fifoTail = 0;

unsigned long lastReceivedCode = 0;
unsigned long lastReceivedTime = 0;

// ===== FIFO functions =====

bool fifoIsEmpty()
{
    return fifoHead == fifoTail;
}

bool fifoIsFull()
{
    return ((fifoHead + 1) % FIFO_SIZE) == fifoTail;
}

void fifoPush(unsigned long value)
{
    if (fifoIsFull())
    {
        fifoTail = (fifoTail + 1) % FIFO_SIZE;  // drop oldest
#ifdef DEBUG
        Serial.println("FIFO full -> oldest code dropped");
#endif
    }

    fifoBuffer[fifoHead] = value;
    fifoHead = (fifoHead + 1) % FIFO_SIZE;

#ifdef DEBUG
    Serial.print("Code queued: ");
    Serial.println(value, HEX);
#endif
}

unsigned long fifoPop()
{
    if (fifoIsEmpty()) return DUMMY_CODE_0; // return dummy code if empty

    unsigned long value = fifoBuffer[fifoTail];
    fifoTail = (fifoTail + 1) % FIFO_SIZE;
    return value;
}

// ===== I2C request handler =====

void requestEvent()
{
    unsigned long codeToSend = fifoPop();
    Wire.write((uint8_t *)&codeToSend, sizeof(codeToSend));
#ifdef DEBUG
    Serial.print("I2C request -> sent code: ");
    Serial.println(codeToSend, HEX);
#endif
}

// ===== Setup =====

void setup()
{
    Serial.begin(9600);
    Serial.println("******************************************");
    Serial.println("***    RC Receiver       " + String(FW_VERSION_ARDUINO, 2) + "          ***");
    Serial.println("******************************************");

    Wire.begin(I2C_SLAVE_ADDRESS);  // Initialize I2C as slave with specified address
    Wire.onRequest(requestEvent);   // Register request handler

    Serial.println("433MHz module power on sequence started (off (500ms) -> on)");
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW); // turn off built-in LED to indicate power off

    pinMode(DOUT_POWER, OUTPUT);
    digitalWrite(DOUT_POWER, LOW);

    delay(500);
    digitalWrite(DOUT_POWER, HIGH);
    digitalWrite(LED_BUILTIN, HIGH); // turn on built-in LED to indicate power on
    delay(500);
    digitalWrite(LED_BUILTIN, LOW); // turn off built-in LED - standard mode

    mySwitch.enableReceive(digitalPinToInterrupt(DIN_RECEIVER));
}

// ===== Main loop =====

void loop()
{
    static bool firstRun = true;
    if (firstRun)
    {
        firstRun = false;
        Serial.println("Loop() started, waiting for codes...");
#ifndef DEBUG
        Serial.println("Note: Debug-Mode disabled, codes will not be printed here!");
#endif
    }

    unsigned long now = millis();

    if (mySwitch.available())
    {
        unsigned long code = mySwitch.getReceivedValue();

        // Visualize every decoded frame (even filtered duplicates) by toggling
        // the built-in LED, so reception can be checked without serial access.
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

        if ((code != lastReceivedCode) || (now - lastReceivedTime) > FILTER_TIME_MS)
        {   
            fifoPush(code);
            lastReceivedCode = code;
#ifndef DEBUG
            Serial.print(".");
#endif            
        }
#ifdef DEBUG
        else
        {
            Serial.print("Filtered duplicate code: ");
            Serial.println(code, HEX);
        }
#endif

        lastReceivedTime = now;
        mySwitch.resetAvailable();
    }

    // Insert dummy code after long pause
    if ((now - lastReceivedTime >= MAX_PAUSE_INTERVAL) && !fifoIsFull())
    {
#ifdef DEBUG
        Serial.println("No activity -> insert dummy code");
#endif
        fifoPush(DUMMY_CODE_F); // dummy code
        lastReceivedTime = now; 
        lastReceivedCode = DUMMY_CODE_F; // reset last received code
    }
}
