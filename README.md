# p10-with-20-panels-and-esp32-version-2.0.2

# 4x5 P10 Pixel Panel Quality Monitor

A production monitoring system built using an **ESP32** and a **4x5 P10 monochrome LED matrix display** (4 panels wide, 5 panels high). It tracks **Total Count**, **OK Quantity**, **NG (Not Good) Quantity**, and **NG Percentage** across 5 distinct production lines.

---

## 🛠️ Hardware & Pin Connections

This project is built using a standard **ESP32 DevKit** board. No external pull-up or pull-down resistors are required for the buttons, as the code configures the ESP32's internal pull-up resistors. Buttons should be wired to connect the respective GPIO pin directly to **GND** when pressed.

### 1. P10 LED Matrix (DMD32 Connection)
The display panel is driven using standard hardware SPI (VSPI interface) for maximum speed and flicker-free rendering.

| LED Panel Signal | Description | ESP32 GPIO | VSPI Pin Function |
| :--- | :--- | :--- | :--- |
| **OE** | Output Enable (Active Low) | **GPIO 22** | General Output |
| **A** | Row Select A | **GPIO 19** | MISO (used as output) |
| **B** | Row Select B | **GPIO 21** | CS (used as output) |
| **CLK** | Shift Register Clock | **GPIO 18** | SCK (SPI Clock) |
| **SCLK / LAT** | Latch / Strobe | **GPIO 2** | General Output |
| **R_DATA** | SPI Serial Data | **GPIO 23** | MOSI (SPI Master Out) |
| **GND** | Ground | **GND** | Common Ground |

> [!CAUTION]
> **Power Supply Warning:** Do not power the P10 panels directly from the ESP32's 5V or 3.3V pins. A 4x5 panel array (20 panels total) can draw significant current. Use an external **5V power supply (at least 5A to 10A)** and connect the external power supply ground to the ESP32's GND to create a **common ground**.

---

### 2. Input Button Connections

#### OK Increment Buttons (Inputs)
*Pressing these buttons increments the OK count for the respective production line.*
*   **Line 1 OK:** **GPIO 32**
*   **Line 2 OK:** **GPIO 25**
*   **Line 3 OK:** **GPIO 27**
*   **Line 4 OK:** **GPIO 15**
*   **Line 5 OK:** **GPIO 4**

#### NG Increment Buttons (Inputs)
*Pressing these buttons increments the NG count for the respective production line.*
*   **Line 1 NG:** **GPIO 33**
*   **Line 2 NG:** **GPIO 26**
*   **Line 3 NG:** **GPIO 14**
*   **Line 4 NG:** **GPIO 13**
*   **Line 5 NG:** **GPIO 12**

#### Factory Reset Button
*Pressing this button resets all counters (OK and NG) across all 5 lines back to zero.*
*   **Factory Reset:** **GPIO 0** (The built-in `BOOT` button on standard ESP32 boards)

---

## 📚 Libraries & Dependencies

The project relies on the following libraries:

1.  **DMD32**: Fork of the original DMD library modified specifically for ESP32.
    *   *Path:* `libraries/DMD32/`
    *   *Protocol:* SPI (VSPI port)
2.  **EEPROM**: Standard ESP32 EEPROM emulation library (used for saving/loading quantities to flash so they persist across power loss).
    *   *Reserved Size:* 512 bytes
3.  **Fonts**: Uses a custom proportional Arial 14 font.
    *   *Header:* `"fonts/Arial14.h"` (Arial_14)

---

## ⚙️ Software Architecture & Stability Notes

### 1. CPU Configuration
The CPU frequency is explicitly set to **240 MHz** in `setup()` using `setCpuFrequencyMhz(240)` to ensure sufficient performance for refreshing the 20 P10 panels.

### 2. Timer Interrupt refresh
A hardware timer interrupt (`timer 0`) is configured to run the screen refresh scan function (`dmd.scanDisplayBySPI()`) every **500 microseconds** to ensure a steady, flicker-free display.

### 3. Crash Prevention (Cache Access Error Fix)
During flash writing (`EEPROM.commit()`), the ESP32 disables the flash memory instruction cache. If a timer interrupt fires at that exact moment and calls code stored in flash (like the DMD display library code), it triggers a fatal **Cache Access Violation** and resets the board.
To prevent this, the code:
*   Completely terminates the timer interrupt using `timerEnd()` before starting the EEPROM write.
*   Performs the EEPROM commit safely.
*   Re-initializes and restarts the timer interrupt immediately after the write finishes.

### 4. Flash Preservation (Deferred Saving)
To avoid wearing out the ESP32's flash memory (which has a limit of ~100,000 write cycles) and to prevent the display from stuttering during rapid inputs:
*   Button presses trigger counts and display updates **instantly**.
*   The actual write to the flash memory is **deferred by 3 seconds** of inactivity. If you press buttons multiple times, only one write operation is executed 3 seconds after the last press.
