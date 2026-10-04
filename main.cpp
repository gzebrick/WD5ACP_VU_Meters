// CYD 2.8" VU Meter Display from WD5ACP
/*
This program is used with the CYD device + a analog/digital converter board for accepting
2 analog inputs for the VU meter display.
Brightness control is available by touching the top of the display.
The left and right VU meters can be switched between 6 different modes by touching
the bottom of the display.
The current brightness level and left/right meter modes are stored in non-volatile memory
for recall on power up.

The default input level is for 0-3.3V analog input.
Use your meter calibration level on the radio's meter output configuration in order
to adjust the input voltage range for full deflection of the VU meters.


*/
const char *Ver = "0.2";

#include <SPI.h>
#include <TFT_eSPI.h>    // Hardware-specific library
#include <Preferences.h> // For non-volatile memory
// #include <spi.h>                 // required for 2.8" display touch screen
#include <XPT2046_Touchscreen.h> // required for 2.8" CYD for touch screen functions

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite vuMeter = TFT_eSprite(&tft); // Sprite buffer for flicker-free rendering
// Create an instance of the Preferences & WiFi library
Preferences preferences;

// The 2.8 CYD touch uses some non default SPI Pins
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

unsigned long ButtMs = 0;
unsigned long PressDelayMs = 3000;
bool isBeingHeld = false;
float tpx, tpy;
int tp_x, tp_y;
int tx, ty;
bool isTouched;

// PWM related parameter settings for dimming screen backlight.
#define BACKLIGHT_PIN 21
#define TFT_BACKLIGHT_ON HIGH
int freq = 2000;
int channel = 0;
int resolution = 8;
int BL_Level = 100; // Backlight level 0-255

// VU Meter Layout Dimensions (Portrait Layout)
const int meterWidth = 35;
const int meterHeight = 270;
const int meter1X = 50;
const int meter1Y = 20;
const int meter2X = 160;
const int meter2Y = 20;

int LeftX = 115;  // Left margin for the VU meter
int RightX = 225; // Right margin for the VU meter

// Segment Configurations
const int numSegments = 70;
const int segmentGap = 1; // Pixels between blocks
// Calculate individual block height dynamically based on constraints
const int segmentHeight = (meterHeight - (segmentGap * (numSegments - 1))) / numSegments;

float currentHeight = 0;    // The smooth animated height tracker
float targetHeight = 0;     // The actual raw volume target
float fallTimeSpeed = 0.08; // How fast the meter drops down (decay rate)
float riseTimeSpeed = 0.40; // Snappy up response for audio signals

const int analogPin = 35; // IO35 on the P3 connector

int leftMode = 0;
int rightMode = 0;

const char *leftModeNames[] = {
    "POWER",
    "ALC",
    "VOLT",
    "COMP",
    "AMPS ",
    "SWR",
};

const char *rightModeNames[] = {
    "POWER",
    "ALC",
    "VOLT",
    "COMP",
    "AMPS ",
    "SWR",
};

void getTouch(int &tp_x, int &tp_y);
void checkBrightness();
void checkLeftMeter();
void checkRightMeter();

void drawSmeterScale();
void drawAlcScale(int xPos);
void drawCompScale(int xPos);
void drawPowerScale(int xPos);
void drawAmpScale(int xPos);
void drawVoltScale(int xPos);
void drawSwrScale(int xPos);

void setup()
{
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0); // 0 or 2 for Portrait mode (best for vertical gauges)
  ledcSetup(channel, freq, resolution);
  ledcAttachPin(BACKLIGHT_PIN, channel);
  preferences.begin("AppSettings", false);             // This is the namespace for local storage
  BL_Level = preferences.getInt("BackLight_Lvl", 100); // Look for backlight level, default to 100
  leftMode = preferences.getInt("Left_Mode", 3);       // Look for left meter mode, default to 3
  rightMode = preferences.getInt("Right_Mode", 1);     // Look for right meter mode, default to 1
  preferences.end();
  ledcWrite(channel, BL_Level);
  tft.fillScreen(TFT_BLACK);

  // Start the SPI for the touch screen and init the TS library
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);

  // Allocate memory for the vertical meter canvas
  vuMeter.createSprite(meterWidth, meterHeight);

  // Static visual borders
  tft.drawRect(meter1X - 3, meter1Y - 3, meterWidth + 6, meterHeight + 6, tft.color565(60, 60, 60));
  tft.drawRect(meter2X - 3, meter2Y - 3, meterWidth + 6, meterHeight + 6, tft.color565(60, 60, 60));

  pinMode(analogPin, INPUT);
  tft.setTextColor(TFT_DARKCYAN, TFT_BLACK);
  tft.drawString("- Dimmer     WD5ACP VU V:" + String(Ver) + "   Brigher +", 2, 0, 1);

  drawSmeterScale();

  switch (leftMode)
  {
  case 0:
    drawPowerScale(LeftX);
    break;
  case 1:
    drawAlcScale(LeftX);
    break;
  case 2:
    drawVoltScale(LeftX);
    break;
  case 3:
    drawCompScale(LeftX);
    break;
  case 4:
    drawAmpScale(LeftX);
    break;
  case 5:
    drawSwrScale(LeftX);
    break;
  }

  switch (rightMode)
  {
  case 0:
    drawPowerScale(RightX);
    break;
  case 1:
    drawAlcScale(RightX);
    break;
  case 2:
    drawVoltScale(RightX);
    break;
  case 3:
    drawCompScale(RightX);
    break;
  case 4:
    drawAmpScale(RightX);
    break;
  case 5:
    drawSwrScale(RightX);
    break;
  }
}

void loop()
{
  checkBrightness();
  checkLeftMeter();
  checkRightMeter();

  // Read raw value (0 - 4095)
  int rawValue = analogRead(analogPin);

  // Convert raw value to voltage measured at the pin (0 to 3.3V)
  float pinVoltage = (rawValue * 3.3) / 4095.0;

  // If using an external 1:1 voltage divider, multiply by 2 to get actual battery/source voltage
  float actualVoltage = pinVoltage * 1.0;
  /*
    Serial.print("Raw: ");
    Serial.print(rawValue);
    Serial.print(" | Actual Voltage: ");
    Serial.println(actualVoltage);
    */

  currentHeight = map(((actualVoltage / 3.3) * 100), 0, 100, 0, meterHeight);

  // 3. Clear our internal sprite background
  vuMeter.fillSprite(TFT_BLACK);

  // 4. Render Segments from Bottom to Top
  for (int i = 0; i < numSegments; i++)
  {
    // Math to locate the bottom boundary of each respective block
    int segBottomY = meterHeight - (i * (segmentHeight + segmentGap));
    int segTopY = segBottomY - segmentHeight;

    // Check if the current volume height reaches this block
    if (currentHeight >= (meterHeight - segBottomY))
    {

      // Assign Classic LED Hardware VU colors based on height tier
      uint16_t blockColor;
      if (i < 18)
      {
        blockColor = TFT_GREEN; // Safe Level (Bottom 60%)
      }
      else if (i < 36)
      {
        blockColor = TFT_YELLOW; // Warning Level
      }
      else
      {
        blockColor = TFT_RED; // Clipping Zone (Top 20%)
      }

      // Fill active block
      vuMeter.fillRect(0, segTopY, meterWidth, segmentHeight, blockColor);
    }
    else
    {
      // OPTIONAL: Draw dim, unlit placeholder segments for realistic hardware look
      vuMeter.fillRect(0, segTopY, meterWidth, segmentHeight, tft.color565(25, 25, 25));
    }
  }

  // 5. Instantly push computed frame to the physical hardware display
  vuMeter.pushSprite(meter1X, meter1Y);
  vuMeter.pushSprite(meter2X, meter2Y);

  delay(10); // High polling speed (~100Hz updates) for agile audio spikes
}

void getTouch(int &tp_x, int &tp_y)
{
  isTouched = ts.tirqTouched() && ts.touched(); // Here if the screen has been touched
  TS_Point tp;
  tp_x = 0;
  tp_y = 0;

  if (isTouched)
  {
    tp = ts.getPoint();                                                       // returns raw pixel coordinates from touch screen 0-4000
    tpx = tft.width() - ((static_cast<float>(tp.y) / 4000.0f) * tft.width()); // adjusted for 240/320
    tpy = (static_cast<float>(tp.x) / 3800.0f) * tft.height();
    // Serial.println(String(tpx) + " & " + String(tpy));
    tp_x = int(tpx);
    tp_y = int(tpy);
    Serial.println(String(tp_x) + " getTouch " + String(tp_y));
  }
  else
  {
    tp_x = 9999;
    tp_y = 9999;
    ButtMs = 0;
  }
}

void checkBrightness()
{
  getTouch(tp_x, tp_y);
  if (tp_x > 1 && tp_x < 150 && tp_y > 1 && tp_y < 100)
  {
    BL_Level = BL_Level - 25;
    if (BL_Level < 25)
    {
      BL_Level = 25;
    }
    ledcWrite(channel, BL_Level);
    preferences.begin("AppSettings", false);       // This is the namespace for local storage
    preferences.putInt("BackLight_Lvl", BL_Level); // Write level
    preferences.end();
    ledcWrite(channel, BL_Level);
    delay(100); // Debounce delay
  }
  if (tp_x > 150 && tp_y > 1 && tp_y < 100)
  {
    BL_Level = BL_Level + 25;
    if (BL_Level > 250)
    {
      BL_Level = 250;
    }
    ledcWrite(channel, BL_Level);
    preferences.begin("AppSettings", false);       // This is the namespace for local storage
    preferences.putInt("BackLight_Lvl", BL_Level); // Write level
    preferences.end();
    ledcWrite(channel, BL_Level);
    delay(100); // Debounce delay  {
  }
}

void checkLeftMeter()
{
  getTouch(tp_x, tp_y);
  if (tp_x > 1 && tp_x < 150 && tp_y > 280 && tp_y < 320)
  {
    Serial.println("Left Meter Touched");
    tft.fillRect(LeftX - 25, 12, 55, meterHeight + 10, TFT_BLACK); // Clear the left meter scale
    tft.fillRect(40, 300, 100, 60, TFT_BLACK);                     // Clear the left meter scale
    Serial.println("Left Meter mode " + String(leftMode));
    leftMode++;
    Serial.println("Left Meter mode updated  " + String(leftMode));

    if (leftMode > 5)
    {
      leftMode = 0;
    }
    Serial.println("Left Meter mode for switch  " + String(leftMode));

    switch (leftMode)
    {
    case 0:
      drawPowerScale(LeftX);
      break;
    case 1:
      drawAlcScale(LeftX);
      break;
    case 2:
      drawVoltScale(LeftX);
      break;
    case 3:
      drawCompScale(LeftX);
      break;
    case 4:
      drawAmpScale(LeftX);
      break;
    case 5:
      drawSwrScale(LeftX);
      ;
      break;
    }

    preferences.begin("AppSettings", false); // This is the namespace for local storage
    preferences.putInt("Left_Mode", leftMode);
    preferences.end();
    delay(100); // Debounce delay
  }
}

void checkRightMeter()
{
  getTouch(tp_x, tp_y);
  if (tp_x > 150 && tp_x < 300 && tp_y > 280 && tp_y < 320)
  {
    Serial.println("Right Meter Touched");
    tft.fillRect(RightX - 25, 12, 55, meterHeight + 10, TFT_BLACK); // Clear the right meter scale
    tft.fillRect(150, 300, 100, 60, TFT_BLACK);                     // Clear the right meter scale
    rightMode++;
    if (rightMode > 5)
    {
      rightMode = 0;
    }

    switch (rightMode)
    {
    case 0:
      drawPowerScale(RightX);
      break;
    case 1:
      drawAlcScale(RightX);
      break;
    case 2:
      drawVoltScale(RightX);
      break;
    case 3:
      drawCompScale(RightX);
      break;
    case 4:
      drawAmpScale(RightX);
      break;
    case 5:
      drawSwrScale(RightX);
      break;
    }

    preferences.begin("AppSettings", false); // This is the namespace for local storage
    preferences.putInt("Right_Mode", rightMode);
    preferences.end();
    delay(100); // Debounce delay
  }
}

void drawSmeterScale()
{
  tft.drawLine(20, 20, 20, (meterHeight / 2) + 12, TFT_MAROON);
  tft.setTextColor(TFT_MAROON, TFT_BLACK);
  tft.drawString("+60 dB", 2, 10, 2);
  tft.drawString("+40 dB", 2, (meterHeight / 6) + 12, 2);
  tft.drawString("+20 dB", 2, (meterHeight / 3) + 12, 2);
  tft.drawLine(20, (meterHeight / 2) + 12, 20, meterHeight + 10, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("9 dB", 2, (meterHeight / 2) + 12, 2);
  tft.drawString("7 dB", 2, ((meterHeight / 10) * 6) + 12, 2);
  tft.drawString("5 dB", 2, ((meterHeight / 10) * 7) + 12, 2);
  tft.drawString("3 dB", 2, ((meterHeight / 10) * 8) + 12, 2);
  tft.drawString("1 dB", 2, ((meterHeight / 10) * 9) + 12, 2);
  tft.drawString("0 dB", 2, meterHeight + 12, 2);
  tft.drawString("S", 5, (meterHeight + 30), 4);
}

void drawCompScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_MAROON);
  tft.setTextColor(TFT_MAROON, TFT_BLACK);
  tft.drawString("20 dB", xPos - 13, (meterHeight / 3) + 12, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("10 dB", xPos - 13, ((meterHeight / 10) * 7) + 12, 2);
  tft.drawString("COMP", xPos - 65, (meterHeight + 30), 4);
}

void drawAlcScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_MAROON);
  tft.setTextColor(TFT_MAROON, TFT_BLACK);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);

  tft.drawString("ALC", xPos - 65, (meterHeight + 30), 4);
}

void drawPowerScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("150W", xPos - 23, 10, 2);
  tft.drawString("100W", xPos - 23, (meterHeight / 6) + 12, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_DARKGREY);
  tft.drawString("50W", xPos - 23, (meterHeight / 2) + 12, 2);
  tft.drawString("25W", xPos - 23, ((meterHeight / 10) * 7) + 12, 2);
  tft.drawString("10W", xPos - 23, ((meterHeight / 10) * 8) + 15, 2);
  tft.drawString("5W", xPos - 23, ((meterHeight / 10) * 9) + 10, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);

  tft.drawString("PO", xPos - 65, (meterHeight + 30), 4);
}

void drawAmpScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("20A ", xPos - 23, 10, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_DARKGREY);
  tft.drawString("10A", xPos - 23, (meterHeight / 2) + 12, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("AMPS", xPos - 65, (meterHeight + 30), 4);
}

void drawVoltScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("15V ", xPos - 23, 10, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_DARKGREY);
  tft.drawString("10V", xPos - 23, (meterHeight / 3) + 12, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("VOLTS", xPos - 65, (meterHeight + 30), 4);
}

void drawSwrScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("INF", xPos - 23, 10, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("3.0", xPos - 23, (meterHeight / 3) + 12, 2);
  tft.drawString("2.0", xPos - 23, ((meterHeight / 10) * 6.5) + 12, 2);
  tft.drawString("1.5", xPos - 23, ((meterHeight / 10) * 8) + 12, 2);
  tft.drawString("SWR", xPos - 65, (meterHeight + 30), 4);
}