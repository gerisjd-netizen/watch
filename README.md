# DIY Smartwatch

A DIY smartwatch built around the Seeed Studio XIAO ESP32-C3.

> **Status: work in progress.** The project is under active development, so features, wiring and the hardware layout may change.

## Features

- **Clock**: large HH:MM:SS display with the date shown smaller underneath.
- **Stopwatch**: start, stop and reset, with centisecond resolution.
- **Manual time setting**: set hour, minute, second, year, month and day from the Settings menu.
- **Accurate timekeeping**: an external DS3231 RTC chip keeps the time, even while the microcontroller sleeps.
- **Auto sleep**: after a period without button input, the display turns off and the microcontroller goes to sleep. Any button press wakes it up, and that press is ignored so it doesn't trigger an action by accident.

## Hardware

| Part | Notes |
|------|-------|
| Seeed Studio XIAO ESP32-C3 | Main microcontroller |
| SSD1306 128x64 OLED display | I2C |
| DS3231 RTC module | I2C, keeps the time |
| 3 push buttons | UP, OK, DOWN |
| Rechargeable battery | Connected directly, no power switch needed |

<img width="300" alt="IMG_20261119_221725" src="https://github.com/user-attachments/assets/d14f18b9-b67b-4cf9-85af-c10969b28a8e" /> <img width="300" alt="IMG_20261119_221655" src="https://github.com/user-attachments/assets/ca9aea57-8a37-420c-aa0b-71c2b04c6147" />




### Pinout

| Function | Pin |
|----------|-----|
| Button UP | GPIO 2 |
| Button OK | GPIO 3 |
| Button DOWN | GPIO 4 |
| I2C SDA | D4 |
| I2C SCL | D5 |

The buttons use the internal pull-up resistors, so wire each button between its pin and GND.

### Power

The battery can be connected directly. No power switch is required, because the microcontroller goes into sleep mode when no button input is detected for a while. Pressing any button wakes the watch up again.

### Physical layout

From the back of the watch to the front:

1. Flat Wi-Fi antenna
2. Battery
3. PCB
4. Display and buttons

> **Note:** the watch is **not waterproof** in any way. Keep it away from water.

## Controls

The watch has three buttons: **UP**, **OK** and **DOWN**.

| Button | Action |
|--------|--------|
| UP / DOWN | Switch between Clock, Stopwatch and Settings in the menu. Change values in Settings. Reset in the Stopwatch. |
| OK (short press) | Open the selected screen. Start/stop the stopwatch. Go to the next field in Settings. |
| OK (hold 3 s) | **Back**: exit the current screen to the menu. |

### Stopwatch

1. Select **STOPWATCH** with UP/DOWN and press OK to enter.
2. Press OK to start, press OK again to stop.
3. Press UP or DOWN to reset.
4. Hold OK for 3 seconds to leave.

### Setting the time

1. Select **SETTINGS** with UP/DOWN and press OK to enter.
2. Use UP/DOWN to change the value (hold for fast repeat) and OK to go to the next field: hour, minute, second, year, month, day.
3. Press OK on the last field (day) to save the time to the RTC.
4. Holding OK for 3 seconds exits without saving.

<img width="300" alt="IMG_20261119_221758" src="https://github.com/user-attachments/assets/e602d91b-54fc-4676-ba9d-060150989abc" />


## Building

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support, and select the XIAO ESP32-C3 board.
2. Install these libraries from the Library Manager:
   - `RTClib` (Adafruit)
   - `U8g2` (olikraus)
3. Open the sketch, then compile and upload.

If the RTC has lost power, it is set to the compile time on startup. After that, correct the time from the Settings menu.

## Configuration

These constants at the top of the sketch can be changed:

| Constant | Default | Meaning |
|----------|---------|---------|
| `SLEEP_AFTER_MS` | 60000 | Inactivity time before the display turns off and the microcontroller sleeps |
| `EXIT_HOLD_MS` | 3000 | How long to hold OK to go back |
| `DEBOUNCE_MS` | 30 | Button debounce time |

## Roadmap

This is an early version. More features are planned.

## License

MIT license
