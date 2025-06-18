#include <Arduino.h>
#include <RCSwitch.h>
#include <Wire.h>

#define ARDUINO_VERSION     1.0
#define DEBUG
#define DIN_RECEIVER        2           // interrupt pin for 433 MHz receiver
#define DOUT_POWER          5           // pin to control power for receiver module
#define FILTER_TIME_MS      200         // duplicate filter timeout
#define MAX_PAUSE_INTERVAL  10000       // dummy code timeout
#define I2C_SLAVE_ADDRESS   0x08        // I2C address for this Arduino
#define DUMMY_CODE_0        0x00000000 // dummy code to insert after long pause
#define DUMMY_CODE_F        0xFFFFFFFF // dummy code to indicate an empty FIFO

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
    if (!fifoIsEmpty())
    {
        unsigned long value = fifoBuffer[fifoTail];
        fifoTail = (fifoTail + 1) % FIFO_SIZE;
        return value;
    }
    return DUMMY_CODE_F; // dummy code
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
    Serial.println("***    RC Receiver       " + String(ARDUINO_VERSION, 2) + "          ***");
    Serial.println("******************************************");

    Wire.begin(I2C_SLAVE_ADDRESS);  // Initialize I2C as slave with specified address
    Wire.onRequest(requestEvent);   // Register request handler

    pinMode(DOUT_POWER, OUTPUT);
    digitalWrite(DOUT_POWER, LOW);
    delay(500);
    digitalWrite(DOUT_POWER, HIGH);
    delay(500);

    mySwitch.enableReceive(digitalPinToInterrupt(DIN_RECEIVER));

    Serial.println("Loop() started, waiting for codes...");
}

// ===== Main loop =====

void loop()
{
    unsigned long now = millis();

    if (mySwitch.available())
    {
        unsigned long code = mySwitch.getReceivedValue();

        if ((code != lastReceivedCode) || (now - lastReceivedTime) > FILTER_TIME_MS)
        {            
            fifoPush(code);
            lastReceivedCode = code;
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
        fifoPush(DUMMY_CODE_0); // dummy code
        lastReceivedTime = now; 
        lastReceivedCode = DUMMY_CODE_0; // reset last received code
    }
}
