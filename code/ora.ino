#include <Wire.h>
#include <RTClib.h>
#include <U8g2lib.h>
#include "esp_sleep.h"
#include "driver/gpio.h"

// ---------- Pins (XIAO ESP32-C3) ----------
const int PIN_UP   = 2;
const int PIN_OK   = 3;
const int PIN_DOWN = 4;
const int PIN_SDA  = D4;
const int PIN_SCL  = D5;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
RTC_DS3231 rtc;

// ---------- Settings ----------
const unsigned long DEBOUNCE_MS     = 30;
const unsigned long EXIT_HOLD_MS    = 3000;   // hold OK 3 s = exit
const unsigned long SLEEP_AFTER_MS  = 60000;  // display off + sleep after 60 s without button press
const unsigned long REPEAT_DELAY_MS = 500;    // auto-repeat in settings
const unsigned long REPEAT_RATE_MS  = 120;

// ---------- State ----------
enum Mode { MODE_MENU, MODE_STOPWATCH, MODE_SETTINGS };
enum Screen { SCR_CLOCK, SCR_STOPWATCH, SCR_SETTINGS, SCR_COUNT };
enum SetStep { SET_HOUR, SET_MINUTE, SET_SECOND, SET_YEAR, SET_MONTH, SET_DAY };

Mode mode = MODE_MENU;
int screenIdx = SCR_CLOCK;
SetStep setStep = SET_HOUR;

int sHour = 0, sMinute = 0, sSecond = 0, sYear = 2026, sMonth = 1, sDay = 1;

// Stopwatch
bool swRunning = false;
unsigned long swStartMs = 0;
unsigned long swAccum = 0;

// Screensaver / sleep
bool asleep = false;
unsigned long lastActivity = 0;

// Buttons
enum { B_UP, B_OK, B_DOWN, B_COUNT };
struct Btn {
  uint8_t pin;
  bool stable;          // debounced pressed state
  bool lastRaw;
  unsigned long changedAt;
  unsigned long pressedAt;
  unsigned long lastRepeat;
  bool longFired;
  bool swallow;         // press that woke the device: ignored entirely
};
Btn btn[B_COUNT] = {
  {PIN_UP,   false, false, 0, 0, 0, false, false},
  {PIN_OK,   false, false, 0, 0, 0, false, false},
  {PIN_DOWN, false, false, 0, 0, 0, false, false}
};

// ---------- Helpers ----------
int daysInMonth(int y, int m) {
  static const int d[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}

unsigned long swElapsed() {
  return swRunning ? swAccum + (millis() - swStartMs) : swAccum;
}

void drawCentered(const char* s, int y) {
  int w = u8g2.getStrWidth(s);
  u8g2.drawStr((128 - w) / 2, y, s);
}

void drawHint(const char* s) {
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(s, 63);
}

// ---------- Actions ----------
void upDown(int dir) {  // dir = +1 or -1
  if (mode == MODE_MENU) {
    screenIdx = (screenIdx + dir + SCR_COUNT) % SCR_COUNT;
  } else if (mode == MODE_STOPWATCH) {
    // UP or DOWN = reset
    swRunning = false;
    swAccum = 0;
  } else if (mode == MODE_SETTINGS) {
    switch (setStep) {
      case SET_HOUR:   sHour   = (sHour   + dir + 24) % 24; break;
      case SET_MINUTE: sMinute = (sMinute + dir + 60) % 60; break;
      case SET_SECOND: sSecond = (sSecond + dir + 60) % 60; break;
      case SET_YEAR:
        sYear += dir;
        if (sYear < 2000) sYear = 2000;
        if (sYear > 2099) sYear = 2099;
        break;
      case SET_MONTH:  sMonth = (sMonth - 1 + dir + 12) % 12 + 1; break;
      case SET_DAY: {
        int dm = daysInMonth(sYear, sMonth);
        sDay = (sDay - 1 + dir + dm) % dm + 1;
        break;
      }
    }
  }
}

void okShort() {
  if (mode == MODE_MENU) {
    if (screenIdx == SCR_STOPWATCH) {
      mode = MODE_STOPWATCH;
    } else if (screenIdx == SCR_SETTINGS) {
      DateTime now = rtc.now();
      sHour = now.hour(); sMinute = now.minute(); sSecond = now.second();
      sYear = now.year(); sMonth = now.month(); sDay = now.day();
      setStep = SET_HOUR;
      mode = MODE_SETTINGS;
    }
  } else if (mode == MODE_STOPWATCH) {
    if (swRunning) {
      swAccum += millis() - swStartMs;
      swRunning = false;
    } else {
      swStartMs = millis();
      swRunning = true;
    }
  } else if (mode == MODE_SETTINGS) {
    if (setStep == SET_DAY) {
      int dm = daysInMonth(sYear, sMonth);
      if (sDay > dm) sDay = dm;
      rtc.adjust(DateTime(sYear, sMonth, sDay, sHour, sMinute, sSecond));
      mode = MODE_MENU;
    } else {
      setStep = (SetStep)(setStep + 1);
    }
  }
}

void okLong() {  // hold OK 3 s = exit to the menu (settings: without saving)
  if (mode != MODE_MENU) mode = MODE_MENU;
}

// ---------- Button handling ----------
void wake() {
  asleep = false;
  u8g2.setPowerSave(0);
}

void onPress(int i) {
  lastActivity = millis();
  btn[i].pressedAt = millis();
  btn[i].lastRepeat = millis();
  btn[i].longFired = false;
  if (asleep) {            // safety: this press only wakes the display
    wake();
    btn[i].swallow = true;
    return;
  }
  btn[i].swallow = false;
  if (i == B_UP)   upDown(+1);
  if (i == B_DOWN) upDown(-1);
}

void onRelease(int i) {
  lastActivity = millis();
  if (btn[i].swallow) { btn[i].swallow = false; return; }
  if (i == B_OK && !btn[i].longFired) okShort();
}

void handleButtons() {
  unsigned long now = millis();
  for (int i = 0; i < B_COUNT; i++) {
    Btn &b = btn[i];
    bool raw = (digitalRead(b.pin) == LOW);
    if (raw != b.lastRaw) { b.lastRaw = raw; b.changedAt = now; }
    if (now - b.changedAt >= DEBOUNCE_MS && raw != b.stable) {
      b.stable = raw;
      if (raw) onPress(i); else onRelease(i);
    }
    if (b.stable && !b.swallow) {
      lastActivity = now;
      if (i == B_OK && !b.longFired && now - b.pressedAt >= EXIT_HOLD_MS) {
        b.longFired = true;
        okLong();
      }
      if (i != B_OK && mode == MODE_SETTINGS &&
          now - b.pressedAt >= REPEAT_DELAY_MS && now - b.lastRepeat >= REPEAT_RATE_MS) {
        b.lastRepeat = now;
        upDown(i == B_UP ? +1 : -1);
      }
    }
  }
}

// ---------- Sleep ----------
void goToSleep() {
  // Do not sleep while any button is held (low-level wake would fire at once)
  for (int i = 0; i < B_COUNT; i++) {
    if (digitalRead(btn[i].pin) == LOW) return;
  }

  asleep = true;
  u8g2.setPowerSave(1);          // display off
  Serial.flush();

  // Wake on any button (low level)
  for (int i = 0; i < B_COUNT; i++) {
    gpio_wakeup_enable((gpio_num_t)btn[i].pin, GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();

  esp_light_sleep_start();       // CPU sleeps here until a button is pressed

  // ---- woke up ----
  unsigned long now = millis();
  wake();                        // display on
  lastActivity = now;

  // The press that woke us is swallowed: it must not trigger any action
  for (int i = 0; i < B_COUNT; i++) {
    gpio_wakeup_disable((gpio_num_t)btn[i].pin);
    if (digitalRead(btn[i].pin) == LOW) {
      Btn &b = btn[i];
      b.lastRaw = true;
      b.stable = true;
      b.changedAt = now;
      b.pressedAt = now;
      b.lastRepeat = now;
      b.longFired = true;
      b.swallow = true;          // cleared when the button is released
    }
  }
}

void handleScreensaver() {
  if (asleep) return;
  if (mode == MODE_STOPWATCH && swRunning) return;  // never sleep while timing
  if (millis() - lastActivity > SLEEP_AFTER_MS) {
    goToSleep();
  }
}

// ---------- Drawing ----------
void drawClock() {
  DateTime now = rtc.now();
  char buf[24];
  u8g2.setFont(u8g2_font_helvB18_tn);
  sprintf(buf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  drawCentered(buf, 30);
  u8g2.setFont(u8g2_font_6x10_tf);
  sprintf(buf, "%04d.%02d.%02d", now.year(), now.month(), now.day());
  drawCentered(buf, 46);
  drawHint("UP/DOWN: menu");
}

void drawStopwatch() {
  unsigned long t = swElapsed();
  unsigned long cs = (t / 10) % 100;
  unsigned long s  = (t / 1000) % 60;
  unsigned long m  = (t / 60000) % 100;
  char buf[24];
  sprintf(buf, "%02lu:%02lu.%02lu", m, s, cs);
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered("STOPWATCH", 10);
  u8g2.setFont(u8g2_font_helvB18_tn);
  drawCentered(buf, 40);
  drawHint(swRunning ? "OK:Stop U/D:Reset" : "OK:Start U/D:Reset");
}

void drawSettings() {
  char buf[16];
  const char* title = "";
  switch (setStep) {
    case SET_HOUR:   title = "SET HOUR";   sprintf(buf, "%02d", sHour);   break;
    case SET_MINUTE: title = "SET MINUTE"; sprintf(buf, "%02d", sMinute); break;
    case SET_SECOND: title = "SET SECOND"; sprintf(buf, "%02d", sSecond); break;
    case SET_YEAR:   title = "SET YEAR";   sprintf(buf, "%04d", sYear);   break;
    case SET_MONTH:  title = "SET MONTH";  sprintf(buf, "%02d", sMonth);  break;
    case SET_DAY:    title = "SET DAY";    sprintf(buf, "%02d", sDay);    break;
  }
  u8g2.setFont(u8g2_font_ncenR10_tf);
  drawCentered(title, 14);
  u8g2.setFont(u8g2_font_helvB18_tn);
  drawCentered(buf, 44);
  drawHint(setStep == SET_DAY ? "U/D:+/- OK:Save" : "U/D:+/- OK:Next");
}

void drawMenu() {
  if (screenIdx == SCR_CLOCK) { drawClock(); return; }
  u8g2.setFont(u8g2_font_ncenR14_tf);
  drawCentered(screenIdx == SCR_STOPWATCH ? "STOPWATCH" : "SETTINGS", 32);
  drawHint("OK: open  U/D: menu");
}

void updateDisplay() {
  if (asleep) return;
  u8g2.clearBuffer();
  if (mode == MODE_MENU) drawMenu();
  else if (mode == MODE_STOPWATCH) drawStopwatch();
  else drawSettings();
  u8g2.sendBuffer();
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_OK, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  u8g2.begin();

  if (!rtc.begin()) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 20, "RTC error!");
    u8g2.sendBuffer();
    while (1);
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  lastActivity = millis();
}

void loop() {
  handleButtons();
  handleScreensaver();
  updateDisplay();
  delay(10);
}