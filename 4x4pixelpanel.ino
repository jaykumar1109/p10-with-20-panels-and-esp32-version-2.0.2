#include <DMD32.h>
#include <EEPROM.h>
#include "fonts/Arial14.h"

#define DISPLAYS_ACROSS 4
#define DISPLAYS_DOWN   5

DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);
hw_timer_t *timer = NULL;

// EEPROM Constants
#define EEPROM_SIZE 512
#define MAGIC_NUMBER 0xDEADE102

struct LineData {
  uint32_t okQty;
  uint32_t ngQty;
};

struct ConfigData {
  uint32_t magic;
  LineData lines[5];
};

ConfigData config;

// Button Pin Definitions
// 10 buttons for OK and NG increments, 1 for factory reset
const int OK_PINS[5] = {32, 25, 27, 15, 4};
const int NG_PINS[5] = {33, 26, 14, 13, 12};
const int RESET_PIN = 0; // Built-in BOOT button on the ESP32 board

// Debounce settings
unsigned long lastDebounceTime[5][2] = {0}; // [row][0 = OK, 1 = NG]
unsigned long lastResetDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 250; // ms

// Global variables for deferred EEPROM saving
bool pendingSave = false;
unsigned long lastButtonPressTime = 0;
const unsigned long EEPROM_SAVE_DELAY = 3000; // Save 3 seconds after the last button press

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
}

void loadConfig() {
  EEPROM.get(0, config);
  if (config.magic != MAGIC_NUMBER) {
    Serial.println("EEPROM not initialized. Initializing with default values (0)...");
    config.magic = MAGIC_NUMBER;
    for (int i = 0; i < 5; i++) {
      config.lines[i].okQty = 0;
      config.lines[i].ngQty = 0;
    }
    saveConfig();
  } else {
    Serial.println("EEPROM configuration loaded successfully.");
  }
}

void saveConfig() {
  // Completely stop the timer to prevent any interrupts accessing flash code during commit
  if (timer != NULL) {
    timerEnd(timer);
    timer = NULL;
  }
  
  EEPROM.put(0, config);
  if (EEPROM.commit()) {
    Serial.println("EEPROM changes successfully committed to flash.");
  } else {
    Serial.println("ERROR: EEPROM commit failed!");
  }
  
  // Re-initialize and enable the timer
  uint8_t cpuClock = ESP.getCpuFreqMHz();
  timer = timerBegin(0, cpuClock, true);
  timerAttachInterrupt(timer, &triggerScan, true);
  timerAlarmWrite(timer, 500, true);
  timerAlarmEnable(timer);
}

// Calculate the rendered width of a string dynamically using dmd.charWidth()
int getStringWidth(const char* str) {
  int width = 0;
  int len = strlen(str);
  for (int i = 0; i < len; i++) {
    int w = dmd.charWidth(str[i]);
    if (w > 0) {
      width += w + 1; // char width + 1 pixel spacing
    }
  }
  if (width > 0) {
    width--; // remove the extra spacing after the last character
  }
  return width;
}

void updateLineDisplay(int row) {
  if (row < 0 || row >= 5) return;
  
  uint32_t ok = config.lines[row].okQty;
  uint32_t ng = config.lines[row].ngQty;
  uint32_t total = ok + ng;
  
  float ngPercent = 0.0;
  if (total > 0) {
    ngPercent = (float)ng * 100.0 / total;
  }
  
  // Buffers for display strings
  char txtTotal[6];  // "0000" to "9999"
  char txtOK[6];     // "0000" to "9999"
  char txtNG[6];     // "0000" to "9999"
  char txtPercent[7];// "00.0" to "100.0"
  
  sprintf(txtTotal, "%04d", total);
  sprintf(txtOK, "%04d", ok);
  sprintf(txtNG, "%04d", ng);
  sprintf(txtPercent, "%04.1f", ngPercent);
  
  // Column definitions (widths: 32, 32, 32, 32)
  int colX[4] = {0, 32, 64, 96};
  int colW[4] = {32, 32, 32, 32};
  
  // Clear only this line's panel row area (16 pixels high)
  dmd.drawFilledBox(0, row * 16, 127, row * 16 + 15, GRAPHICS_INVERSE);
  
  int y = row * 16 + 2; // Compensate for Arial_14 vertical offset to center it perfectly
  
  // Render each column centered
  const char* txts[4] = {txtTotal, txtOK, txtNG, txtPercent};
  for (int c = 0; c < 4; c++) {
    int strW = getStringWidth(txts[c]);
    int x = colX[c] + (colW[c] - strW) / 2;
    dmd.drawString(x, y, txts[c], strlen(txts[c]), GRAPHICS_NORMAL);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000); // Give the serial monitor a moment to connect
  
  // Print Reset Reason
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("ESP32 Reset Reason: ");
  switch (reason) {
    case ESP_RST_POWERON:   Serial.println("Power-on Reset"); break;
    case ESP_RST_EXT:       Serial.println("External Pin Reset (e.g. Reset Button)"); break;
    case ESP_RST_SW:        Serial.println("Software Reset"); break;
    case ESP_RST_PANIC:     Serial.println("Exception/Panic Reset (Cache Error / Crash)"); break;
    case ESP_RST_INT_WDT:   Serial.println("Interrupt Watchdog Reset"); break;
    case ESP_RST_TASK_WDT:  Serial.println("Task Watchdog Reset"); break;
    case ESP_RST_DEEPSLEEP: Serial.println("Deep Sleep Reset"); break;
    case ESP_RST_BROWNOUT:  Serial.println("Brownout Reset (Voltage Drop)"); break;
    default:                Serial.println("Unknown Reset Reason"); break;
  }
  
  Serial.println("STEP 1: Serial started");
  
  setCpuFrequencyMhz(240);
  Serial.println("STEP 2: CPU frequency set");
  
  uint8_t cpuClock = ESP.getCpuFreqMHz();
  Serial.print("STEP 3: CPU Frequency = ");
  Serial.println(cpuClock);
  
  // Setup button inputs with internal pullups
  Serial.println("STEP 4: Initializing input pins...");
  for (int i = 0; i < 5; i++) {
    pinMode(OK_PINS[i], INPUT_PULLUP);
    pinMode(NG_PINS[i], INPUT_PULLUP);
  }
  pinMode(RESET_PIN, INPUT_PULLUP);
  Serial.println("STEP 5: Input pins initialized");

  // Initialize EEPROM
  Serial.println("STEP 6: Initializing EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();
  Serial.println("STEP 7: EEPROM initialized");
  
  Serial.println("STEP 8: Creating timer...");
  timer = timerBegin(0, cpuClock, true);
  Serial.println("STEP 9: Timer created");
  
  Serial.println("STEP 10: Attaching interrupt...");
  timerAttachInterrupt(timer, &triggerScan, true);
  Serial.println("STEP 11: Interrupt attached");
  
  Serial.println("STEP 12: Setting alarm...");
  timerAlarmWrite(timer, 500, true);
  Serial.println("STEP 13: Alarm set");
  
  Serial.println("STEP 14: Enabling timer...");
  timerAlarmEnable(timer);
  Serial.println("STEP 15: Timer enabled");
  
  Serial.println("STEP 16: Clearing screen...");
  dmd.clearScreen(true);
  Serial.println("STEP 17: Screen cleared");
  
  Serial.println("STEP 18: Selecting font...");
  dmd.selectFont(Arial_14);
  Serial.println("STEP 19: Font selected");
  
  Serial.println("STEP 20: Drawing initial counters...");
  for (int i = 0; i < 5; i++) {
    updateLineDisplay(i);
  }
  
  Serial.println("STEP 21: Setup complete!");
}

void loop()
{
  // 1. Check Factory Reset Button
  if (digitalRead(RESET_PIN) == LOW) {
    unsigned long now = millis();
    if (now - lastResetDebounceTime > DEBOUNCE_DELAY) {
      lastResetDebounceTime = now;
      Serial.println("Factory Reset triggered! Clearing all counters...");
      for (int i = 0; i < 5; i++) {
        config.lines[i].okQty = 0;
        config.lines[i].ngQty = 0;
      }
      pendingSave = true;
      lastButtonPressTime = now;
      for (int i = 0; i < 5; i++) {
        updateLineDisplay(i);
      }
    }
  }

  // 2. Check OK and NG Buttons for all 5 lines
  for (int i = 0; i < 5; i++) {
    // OK Increment
    if (digitalRead(OK_PINS[i]) == LOW) {
      unsigned long now = millis();
      if (now - lastDebounceTime[i][0] > DEBOUNCE_DELAY) {
        lastDebounceTime[i][0] = now;
        config.lines[i].okQty++;
        Serial.printf("Line %d OK Count increased to: %d\n", i + 1, config.lines[i].okQty);
        pendingSave = true;
        lastButtonPressTime = now;
        updateLineDisplay(i);
      }
    }

    // NG Increment
    if (digitalRead(NG_PINS[i]) == LOW) {
      unsigned long now = millis();
      if (now - lastDebounceTime[i][1] > DEBOUNCE_DELAY) {
        lastDebounceTime[i][1] = now;
        config.lines[i].ngQty++;
        Serial.printf("Line %d NG Count increased to: %d\n", i + 1, config.lines[i].ngQty);
        pendingSave = true;
        lastButtonPressTime = now;
        updateLineDisplay(i);
      }
    }
  }

  // 3. Handle Deferred EEPROM Save
  if (pendingSave && (millis() - lastButtonPressTime > EEPROM_SAVE_DELAY)) {
    saveConfig();
    pendingSave = false;
  }

  // 4. Periodic debug output
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    Serial.print("System Running - Free Heap: ");
    Serial.println(ESP.getFreeHeap());
    lastPrint = millis();
  }
}