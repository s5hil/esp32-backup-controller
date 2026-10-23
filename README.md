# esp32-handoff

## Description

This project is a failover system built for an ESP32-S3 and an ESP32D. Both boards run the same firmware and share one output, an LED that fades continuously. One board is active and fades the LED, while the other is on standby, watching the active board and keeping a copy of its state. If the active board loses power, resets, or hangs, the standby takes over in about 25 ms and continues the fade from where it left off.

## Features

- Takeover in about 25 ms when the active board loses power, resets, or hangs
- Two links between the boards, a heartbeat wire and ESP-NOW, so losing only one never causes a takeover
- The standby keeps a copy of the fade, so the LED continues where it left off
- A watchdog resets a frozen board and releases the LED right away
- A board that restarts always comes back as standby, so it never fights the active board
- Takeover logic tested on a laptop with GoogleTest

## Technologies

- C++
- Arduino ESP32
- ESP-NOW
- GoogleTest
- CMake

## Hardware

- ESP32-S3-DevKitC-1
- ESP32D (ESP32 DevKit V1)
- One LED
- Resistors: 1x 220Ω, 2x 1kΩ, 5x 10kΩ

## Wiring

| From | Through | To |
| --- | --- | --- |
| ESP32-S3 GPIO6 | 5kΩ (two 10kΩ in parallel) | ESP32D GPIO27 |
| ESP32-S3 GND | | ESP32D GND |
| ESP32-S3 GPIO4 | 1kΩ | LED node |
| ESP32D GPIO25 | 1kΩ | LED node |
| LED node | 220Ω | LED, then GND |
| LED node | 10kΩ | GND |
| ESP32-S3 GPIO5 | 10kΩ | GND |
| ESP32D GPIO26 | 10kΩ | GND |

## Configuration

1. Clone the repository

```
$ git clone https://github.com/s5hil/esp32-handoff
```

2. Open `failover/failover.ino` in Arduino IDE

3. Set `PEER_MAC` for each board to the other board's MAC address

4. Flash the ESP32-S3 with **ESP32S3 Dev Module** selected and **USB CDC On Boot** enabled

5. Flash the ESP32D with **ESP32 Dev Module** selected. If the upload gets stuck at `Connecting...`, hold the **BOOT** button

## Running the Tests

```
$ cd failover/test
$ cmake -S . -B build
$ cmake --build build
$ ./build/leader_fsm_test
```

The tests check the logic that decides which board is in charge, including cases that are hard to create on real boards, like both boards starting at the same moment.

## Testing Failures

- Unplug or reset the active board
- Type `drop N` into the active board's Serial Monitor to drop N% of ESP-NOW messages
- Type `hang` into the active board's Serial Monitor to freeze it