// CYD 2.8" VU Meter Display from WD5ACP
/*
This program is used with the CYD device + a ADDS115 4 channel analog/digital converter board
for accepting2 analog inputs for the VU meter display at analog0 and analog1
https://a.co/d/05YexELj
Brightness control is available by touching the top  left and right side of the display.
The left and right VU meters can be switched between 6 different modes by touching
the bottom of the display mode selectors.
The current brightness level and left/right meter modes are stored in non-volatile memory
for recall on power up.

CYD Port CN1 Pin	<> ADS1115 Pin	Description
GND	            GND	          Ground Reference
3V3	            VDD	          3.3V Power Supply
GPIO 22	        SCL	          I2C Clock Line
GPIO 27	        SDA	          I2C Data Line

Use your meter calibration level on the radio's meter output configuration in order
to adjust the input voltage range for full deflection of the VU meters.
v 1.0 initial release
v 1.1 cleaned up code, added comments, added setupInit flag to prevent mode
v 1.2 added peak hold functionality and improved animation smoothness
v 1.3 Lowered CPU clock speed to 80 MHz for power saving and stability,
v 1.4 made background color selectable by touching the center of the display
v 1.5 Fixed bug in meter-2 peak hold decay (moved a } around)
*/
const char *Ver = "1.5"; // Program version

// #include <SPI.h>
#include <TFT_eSPI.h>            // Hardware-specific library
#include <Preferences.h>         // For non-volatile memory
#include <spi.h>                 // required for 2.8" display touch screen
#include <XPT2046_Touchscreen.h> // required for 2.8" CYD for touch screen functions
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite vuMeter1 = TFT_eSprite(&tft); // Left meter sprite buffer
TFT_eSprite vuMeter2 = TFT_eSprite(&tft); // Right meter sprite buffer
// Create an instance of the Preferences & WiFi library
Preferences preferences;
Adafruit_ADS1115 ads;

// The 2.8 CYD touch uses some non default SPI Pins
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

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

int bgColorIndex = 2;      // 0 = Black, 1 = Blue, 2 = Green
uint16_t bgColor = 0x0000; // Selected background color

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
const int numSegments = 135;
const int segmentGap = 1; // Pixels between blocks
// Calculate individual block height dynamically based on constraints
const int segmentHeight = (meterHeight - (segmentGap * (numSegments - 1))) / numSegments;

float currentHeight1 = 0;   // The smooth animated height tracker
float currentHeight2 = 0;   // The smooth animated height tracker
float targetHeight1 = 0;    // The actual raw volume target
float targetHeight2 = 0;    // The actual raw volume target
float fallTimeSpeed = 0.08; // How fast the meter drops down (decay rate)
float riseTimeSpeed = 0.40; // Snappy up response for audio signals

float maxVolts = 3.0;   // Maximum voltage for full scale deflection
bool adFailure = false; // Flag for ADS1115 initialization failure
float volts0 = 0;       // Voltage reading for channel 0
float volts1 = 0;       // Voltage reading for channel 1

int leftMode = 0;  // 0=Power, 1=ALC, 2=Volt, 3=Comp, 4=Amps, 5=SWR, 6=Raw
int rightMode = 0; // 0=Power, 1=ALC, 2=Volt, 3=Comp, 4=Amps, 5=SWR, 6=Raw

void getTouch(int &tp_x, int &tp_y); //
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
void drawRawScale(int xPos);

// Peak Hold Variables
int currentPeak1 = 0;
unsigned long peakHoldMillis1 = 0;
int currentPeak2 = 0;
unsigned long peakHoldMillis2 = 0;
const unsigned long HOLD_DURATION1 = 1000; // Hold peak for 1000ms
const unsigned long HOLD_DURATION2 = 1000; // Hold peak for 1000ms
const int DECAY_RATE = 1;
const int DECAY_RATE2 = 1; // Drop peak by 1 pixel per frame during decay

bool setupInit = true;

void setBackgroundColor()
{
  bgColor = tft.color565(0, 0, 0); // Black
  if (bgColorIndex == 1)
  {
    bgColor = tft.color565(0, 0, 60); // Blue
  }
  else if (bgColorIndex == 2)
  {
    bgColor = tft.color565(0, 60, 50); // Green
  }
  tft.fillScreen(bgColor);
}

void setup() // Setup function runs once at startup
{

  // Slow down CPU clock speed to 80 MHz
  setCpuFrequencyMhz(80); // Reduce 240 CPU clock speed to 80 MHz for power saving and stability
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);                                    // 0 or 2 for Portrait mode (best for vertical gauges)
  ledcSetup(channel, freq, resolution);                  // Setup the PWM channel for backlight control
  ledcAttachPin(BACKLIGHT_PIN, channel);                 // Attach the PWM channel to the backlight pin
  preferences.begin("AppSettings", false);               // This is the namespace for local storage
  BL_Level = preferences.getInt("BackLight_Lvl", 100);   // Look for backlight level, default to 100
  bgColorIndex = preferences.getInt("BG_ColorIndex", 2); // Look for background color, default to green
  leftMode = preferences.getInt("Left_Mode", 3);         // Look for left meter mode, default to 3
  rightMode = preferences.getInt("Right_Mode", 1);       // Look for right meter mode, default to 1
  preferences.end();                                     // Close the Preferences namespace to free up resources
  ledcWrite(channel, BL_Level);                          // Set the backlight level to the stored value

  setBackgroundColor();

  // Start the SPI for the touch screen and init the TS library
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS); // Initialize the custom SPI bus with the specified pins
  touchscreen.begin(mySpi);                                         // Initialize the touchscreen with the custom SPI bus
  touchscreen.setRotation(1);                                       // Set the touchscreen rotation to match the display orientation

  // Allocate memory for the vertical meter canvas
  vuMeter1.createSprite(meterWidth, meterHeight);
  vuMeter2.createSprite(meterWidth, meterHeight);

  // Static visual borders
  tft.drawRect(meter1X - 3, meter1Y - 3, meterWidth + 6, meterHeight + 6, tft.color565(60, 60, 60));
  tft.drawRect(meter2X - 3, meter2Y - 3, meterWidth + 6, meterHeight + 6, tft.color565(60, 60, 60));

  // Initialize I2C with CYD specific pins: 27, 22
  Wire.begin(27, 22);
  // Initialize the ADS1115 at address 0x48
  if (!ads.begin(0x48))
  {
    Serial.println("Failed to initialize ADS1115!");
    adFailure = true;
  }

  ads.setGain(GAIN_TWOTHIRDS); // Set the gain to 2/3 for a full-scale range of ±6.144V
  Serial.println("ADS1115 successfully initialized.");

  drawSmeterScale();
  checkLeftMeter();  // Check for touch input on the left meter area
  checkRightMeter(); // Check for touch input on the right meter area
  setupInit = false;
}

void loop() // Main loop runs continuously after setup
{
  checkBrightness();
  checkLeftMeter();  // Check for touch input on the left meter area
  checkRightMeter(); // Check for touch input on the right meter area

  int16_t adc0, adc1; // Variables to hold raw ADC readings from the ADS1115
  // Read the two single-ended analog inputs
  if (!adFailure)
  {
    // Convert to voltage values
    adc0 = ads.readADC_SingleEnded(0); // Read channel 0
    adc1 = ads.readADC_SingleEnded(1); // Read channel 1
    volts0 = ads.computeVolts(adc0);   // Convert raw ADC reading to voltage for channel 0
    volts1 = ads.computeVolts(adc1);   // Convert raw ADC reading to voltage for channel 1
  }
  else
  {
    volts0 = maxVolts;
    volts1 = maxVolts;
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawString("     A/D Board Error!     ", 12, 60, 4);
    delay(500);
  }

  // Convert to voltage values
  //  Serial.printf("Input 1: %f V | Input 2: %f V\n", volts0, volts1);

  currentHeight1 = map(((volts0 / maxVolts) * 100), 0, 100, 0, meterHeight); // Map the voltage to a height for the VU meter
  currentHeight2 = map(((volts1 / maxVolts) * 100), 0, 100, 0, meterHeight); //

  // 3. Clear our internal sprite background
  vuMeter1.fillSprite(bgColor); // Clear the left meter sprite buffer
  vuMeter2.fillSprite(bgColor); // Clear the right meter sprite buffer

  // 4a. Render Segments from Bottom to Top Meter 1
  uint16_t blockColor = TFT_GREEN;      // Retain the active segment color for the peak indicator
  for (int i = 0; i < numSegments; i++) // Loop through each segment of the VU meter
  {
    // Math to locate the bottom boundary of each respective block
    int segBottomY = meterHeight - (i * (segmentHeight + segmentGap)); // Calculate the bottom Y coordinate of the current segment
    int segTopY = segBottomY - segmentHeight;                          // Calculate the top Y coordinate of the current segment

    // Check if the current volume height reaches this block
    if (currentHeight1 >= (meterHeight - segBottomY)) // If the current height is greater than or equal to the bottom of this segment, it should be lit
    {

      // Assign Classic LED Hardware VU colors based on height tier
      if (i < (numSegments / 4)) // If the segment is in the bottom quarter of the meter
      {
        blockColor = TFT_GREEN; // Safe Level (Bottom 60%)
      }
      else if (i < numSegments / 2) // If the segment is in the middle half of the meter
      {
        blockColor = TFT_YELLOW; // Warning Level
      }
      else
      {
        blockColor = TFT_RED; // Clipping Zone (Top 20%)
      }

      // Fill active block
      vuMeter1.fillRect(0, segTopY, meterWidth, segmentHeight, blockColor); // Draw the segment with the determined color
    }
    else
    {
      // OPTIONAL: Draw dim, unlit placeholder segments for realistic hardware look
      vuMeter1.fillRect(0, segTopY, meterWidth, segmentHeight, tft.color565(20, 20, 5));
    }
  }

  // --- Peak Hold Logic ---
  if (currentHeight1 >= currentPeak1)
  {
    currentPeak1 = currentHeight1;
    peakHoldMillis1 = millis(); // Reset the hold timer
  }
  else
  {
    // Current value dropped below peak. Check if hold duration expired.
    if (millis() - peakHoldMillis1 > HOLD_DURATION1)
    {
      // Decay the peak downward (increase Y coordinate)
      currentPeak1 -= DECAY_RATE;
      if (currentPeak1 < 0)
      {
        currentPeak1 = 0; // Cap at bottom
      }
    }
  } // end of render segments

  // Draw the Peak Hold indicator line (only if it's above the bottom)
  if (currentPeak1 >= 0)
  {
    // Draw a 3-pixel thick peak line
    vuMeter1.fillRect(0, meterHeight - currentPeak1, meterWidth, 3, TFT_LIGHTGREY); // Use the same color as the last active segment for consistency
  }

  // Render Segments from Bottom to Top Meter 2
  for (int i = 0; i < numSegments; i++)
  {
    // Math to locate the bottom boundary of each respective block
    int segBottomY = meterHeight - (i * (segmentHeight + segmentGap));
    int segTopY = segBottomY - segmentHeight;

    // Check if the current volume height reaches this block
    if (currentHeight2 >= (meterHeight - segBottomY))
    {

      // Assign Classic LED Hardware VU colors based on height tier
      uint16_t blockColor;
      if (i < (numSegments / 4))
      {
        blockColor = TFT_GREEN; // Safe Level (Bottom 60%)
      }
      else if (i < numSegments / 2)
      {
        blockColor = TFT_YELLOW; // Warning Level
      }
      else
      {
        blockColor = TFT_RED; // Clipping Zone (Top 20%)
      }

      // Fill active block
      vuMeter2.fillRect(0, segTopY, meterWidth, segmentHeight, blockColor);
    }
    else
    {
      // OPTIONAL: Draw dim, unlit placeholder segments for realistic hardware look
      vuMeter2.fillRect(0, segTopY, meterWidth, segmentHeight, tft.color565(20, 20, 5));
    }
  }

  // --- Peak Hold Logic ---
  if (currentHeight2 >= currentPeak2)
  {
    currentPeak2 = currentHeight2;
    peakHoldMillis2 = millis(); // Reset the hold timer
  }
  else
  {
    // Current value dropped below peak. Check if hold duration expired.
    if (millis() - peakHoldMillis2 > HOLD_DURATION2)
    {
      // Decay the peak downward (increase Y coordinate)
      currentPeak2 -= DECAY_RATE2;
      if (currentPeak2 < 0)
      {
        currentPeak2 = 0; // Cap at bottom
      }
    }
  } // end of render segments

  // Draw the Peak Hold indicator line (only if it's above the bottom)
  if (currentPeak2 >= 0)
  {
    // Draw a 3-pixel thick peak line
    vuMeter2.fillRect(0, meterHeight - currentPeak2, meterWidth, 3, TFT_LIGHTGREY); // Use the same color as the last active segment for consistency
  }

  // 5. Instantly push computed frame to the physical hardware display
  vuMeter1.pushSprite(meter1X, meter1Y); // Push the left meter sprite buffer to the display at the specified coordinates
  vuMeter2.pushSprite(meter2X, meter2Y); // Push the right meter sprite buffer to the display at the specified coordinates
  delay(10);                             // High polling speed (~100Hz updates) for agile audio spikes
}

void getTouch(int &tp_x, int &tp_y)
{
  isTouched = touchscreen.tirqTouched() && touchscreen.touched(); // Here if the screen has been touched
  TS_Point tp;
  tp_x = 0;
  tp_y = 0;

  if (isTouched)
  {
    tp = touchscreen.getPoint();                                              // returns raw pixel coordinates from touch screen 0-4000
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
  if (tp_x > 1 && tp_x < 80 && tp_y > 1 && tp_y < 100)
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
  if (tp_x > 160 && tp_y > 1 && tp_y < 100)
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
    delay(100); // Debounce delay
  }

  // Check for background color change
  if (tp_x > 80 && tp_x < 160 && tp_y > 1 && tp_y < 100)
  {
    bgColorIndex++;
    if (bgColorIndex > 2)
    {
      bgColorIndex = 0;
    }
    setBackgroundColor();
    preferences.begin("AppSettings", false);           // This is the namespace for local storage
    preferences.putInt("BG_ColorIndex", bgColorIndex); // Write level
    preferences.end();
    setupInit = true; // Set the setupInit flag to true to prevent mode changes during background color change
    drawSmeterScale();
    checkLeftMeter();  // Check for touch input on the left meter area
    checkRightMeter(); // Check for touch input on the right meter area
    setupInit = false; // Set the setupInit flag to true to prevent mode changes during background color change
    delay(100);        // Debounce delay
  }
}

void checkLeftMeter()
{
  getTouch(tp_x, tp_y);
  if ((tp_x > 1 && tp_x < 150 && tp_y > 220 && tp_y < 320) || setupInit)
  {
    tft.fillRect(LeftX - 25, 12, 55, meterHeight + 10, bgColor); // Clear the left meter scale
    tft.fillRect(40, 300, 100, 60, bgColor);                     // Clear the left meter scale
    if (!setupInit)
    {
      leftMode++;
    }

    if (leftMode > 6)
    {
      leftMode = 0;
    }

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
    case 6:
      drawRawScale(LeftX);
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
  if ((tp_x > 150 && tp_x < 300 && tp_y > 220 && tp_y < 320) || setupInit)
  {
    Serial.println("Right Meter Touched");
    tft.fillRect(RightX - 25, 12, 55, meterHeight + 10, bgColor); // Clear the right meter scale
    tft.fillRect(150, 300, 100, 60, bgColor);                     // Clear the right meter scale
    if (!setupInit)
    {
      rightMode++;
    }
    if (rightMode > 6)
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
    case 6:
      drawRawScale(RightX);
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
  tft.setTextColor(TFT_LIGHTGREY, bgColor); // Set text color to light grey with background color for better visibility
  tft.drawString(" -DIMMER-   WD5ACP VU v" + String(Ver) + "   +BRIGHTER+", 2, 0, 1);

  // Static visual borders
  tft.drawRect(meter1X - 3, meter1Y - 3, meterWidth + 6, meterHeight + 6, tft.color565(60, 60, 60));
  tft.drawRect(meter2X - 3, meter2Y - 3, meterWidth + 6, meterHeight + 6, tft.color565(60, 60, 60));

  tft.drawLine(20, 20, 20, (meterHeight / 2) + 12, TFT_RED);
  tft.setTextColor(TFT_RED, bgColor);
  tft.drawString("+60 dB", 2, 10, 2);
  tft.drawString("+40 dB", 2, (meterHeight / 6) + 12, 2);
  tft.drawString("+20 dB", 2, (meterHeight / 3) + 12, 2);
  tft.drawLine(20, (meterHeight / 2) + 12, 20, meterHeight + 10, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("9 dB", 2, (meterHeight / 2) + 12, 2);
  tft.drawString("7 dB", 2, ((meterHeight / 10) * 6) + 12, 2);
  tft.drawString("5 dB", 2, ((meterHeight / 10) * 7) + 12, 2);
  tft.drawString("3 dB", 2, ((meterHeight / 10) * 8) + 12, 2);
  tft.drawString("1 dB", 2, ((meterHeight / 10) * 9) + 12, 2);
  tft.drawString("0 dB", 2, meterHeight + 12, 2);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("S", 5, (meterHeight + 30), 4);
}

void drawCompScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_RED);
  tft.setTextColor(TFT_RED, bgColor);
  tft.drawString("20 dB", xPos - 23, (meterHeight / 3) + 12, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("10 dB", xPos - 23, ((meterHeight / 10) * 7) + 12, 2);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("COMP", xPos - 65, (meterHeight + 30), 4);
}

void drawAlcScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_LIGHTGREY);
  tft.setTextColor(TFT_MAROON, bgColor);
  // tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_RED);
  tft.drawRect(xPos - 18, (meterHeight / 2) + 12, 10, (meterHeight / 2) + 10, TFT_RED);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("ALC", xPos - 65, (meterHeight + 30), 4);
}

void drawPowerScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("150W", xPos - 23, 10, 2);
  tft.drawString("100W", xPos - 23, (meterHeight / 6) + 12, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_LIGHTGREY);
  tft.drawString("50W", xPos - 23, (meterHeight / 2) + 12, 2);
  tft.drawString("25W", xPos - 23, ((meterHeight / 10) * 7) + 12, 2);
  tft.drawString("10W", xPos - 23, ((meterHeight / 10) * 8) + 15, 2);
  tft.drawString("5W", xPos - 23, ((meterHeight / 10) * 9) + 10, 2);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);

  tft.drawString("PWR", xPos - 65, (meterHeight + 30), 4);
}

void drawAmpScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("20A ", xPos - 23, 10, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_LIGHTGREY);
  tft.drawString("10A", xPos - 23, (meterHeight / 2) + 12, 2);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("AMPS", xPos - 65, (meterHeight + 30), 4);
}

void drawVoltScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("15V ", xPos - 23, 10, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_LIGHTGREY);
  tft.drawString("10V", xPos - 23, (meterHeight / 3) + 12, 2);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("VOLTS", xPos - 65, (meterHeight + 30), 4);
}

void drawSwrScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("INF", xPos - 23, 10, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("3.0", xPos - 23, (meterHeight / 3) + 12, 2);
  tft.drawString("2.0", xPos - 23, ((meterHeight / 10) * 6.5) + 12, 2);
  tft.drawString("1.5", xPos - 23, ((meterHeight / 10) * 8) + 12, 2);

  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("SWR", xPos - 65, (meterHeight + 30), 4);
}

void drawRawScale(int xPos)
{
  tft.drawLine(xPos - 10, 20, xPos - 10, (meterHeight / 2) + 12, TFT_LIGHTGREY);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString(String(maxVolts, 1) + "v", xPos - 23, 10, 2);
  tft.drawString(String(maxVolts * 0.83333, 1) + "v", xPos - 23, (meterHeight / 6) * 1 + 12, 2);
  tft.drawLine(xPos - 10, (meterHeight / 2) + 12, xPos - 10, meterHeight + 10, TFT_LIGHTGREY);
  tft.drawString(String(maxVolts * 0.66666, 1) + "v", xPos - 23, (meterHeight / 6) * 2 + 12, 2);
  tft.drawString(String(maxVolts * 0.5, 1) + "v", xPos - 23, ((meterHeight / 6) * 3) + 12, 2);
  tft.drawString(String(maxVolts * 0.33333, 1) + "v", xPos - 23, ((meterHeight / 6) * 4) + 15, 2);
  tft.drawString(String(maxVolts * 0.16666, 1) + "v", xPos - 23, ((meterHeight / 6) * 5) + 15, 2);
  tft.setTextColor(TFT_LIGHTGREY, bgColor);
  tft.drawString("RAW", xPos - 65, (meterHeight + 30), 4);
}