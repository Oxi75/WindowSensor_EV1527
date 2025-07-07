#pragma once

#define I2C_SLAVE_ADDRESS   0x08        // I2C address for this Arduino
#define DUMMY_CODE_0        0x00000000 // dummy code to insert after long pause
#define DUMMY_CODE_F        0xFFFFFFFF // dummy code to indicate an empty FIFO
#define DUMMY_CODE_E        0x0000000E // dummy code to indicate incomplete data

#define FW_VERSION_ESP 0.41            //firmware version of the ESP code
#define FW_VERSION_ARDUINO 1.0         //firmware version of the ESP code

//#define DEBUG                         // Uncomment to enable debug output
