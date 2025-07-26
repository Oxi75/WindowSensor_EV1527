/*
  Simple example for receiving
  
  https://github.com/sui77/rc-switch/
*/

#include <RCSwitch.h>

#define INPUT_PIN GPIO_NUM_15

RCSwitch mySwitch = RCSwitch();

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("**** EV1527 Test Program ****");
  delay(500);
  mySwitch.enableReceive(digitalPinToInterrupt(INPUT_PIN));  // Receiver on interrupt 0 => that is pin #2
}


void loop()
{
  static bool firstCall = true;
  static bool firstReceive = true;

  if (firstCall)
  {
    Serial.println("Entering Loop() for the first time");
    firstCall = false;
  }

  if (mySwitch.available())
  {
    uint32_t P = mySwitch.getReceivedProtocol();
    uint32_t l = mySwitch.getReceivedBitlength();
    uint32_t code = mySwitch.getReceivedValue();

      Serial.println("received value dec: " + String(code) + ", bin: " + String(code, BIN) + ", (" + String(l) + " bits), Sensor Address: " + String(code >> 4) + ", Sensor Data: " + String(code & 0x0F) + ", Protocol: " + String(P));

    mySwitch.resetAvailable();
  }
}



//  pinMode(INPUT_PIN, INPUT);
