# WindowSensor_EV1527

This project enables the integration of affordable 433 MHz window sensors and push buttons into the **homee** smart home system via **vhih**.

## Hardware

- **ESP32** microcontroller
- **EV1527** 433 MHz receiver module

## Features

- Receives signals from standard 433 MHz window sensors and push buttons (EV1527 protocol)
- Forwards sensor states to homee via vhih
- Simple configuration through a web interface

## Installation

1. Assemble the hardware according to the schematic (ESP32 + EV1527 receiver).
2. Flash the firmware to the ESP32 using [PlatformIO](https://platformio.org/).
3. Connect the device to Wi-Fi and configure it via the web interface.

## Directory Structure

- `src/` – Project source code
- `include/` – Header files
- `data/` – Web interface files (e.g., configuration)
- `examples/` – Example projects

## Notes

- Further information about integrating with homee and vhih can be found in the documentation and source code.
- License: See [LICENSE](LICENSE)

---

Window and door sensor for homee, based on EV1527 and ESP32.