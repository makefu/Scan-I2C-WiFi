// **********************************************************************************
// Scan-I2C-WiFi
// **********************************************************************************
// Written by Charles-Henri Hallard (http://hallard.me)
//
// History : V1.00 2014-04-21 - First release
//         : V1.11 2015-09-23 - rewrite for ESP8266 target
//         : V1.20 2016-07-13 - Added new OLED Library and NeoPixelBus
//         : V1.30 2018-04-01 - Added ESP32 support
//
// **********************************************************************************
#define ARDUINO_ARCH_ESP8266
#if !defined(ARDUINO_ARCH_ESP8266) && !defined(ARDUINO_ARCH_ESP32) 
#error "This sketch runs only on ESP32 or ESP8266 target"
#endif 

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <Wire.h>

#ifdef ARDUINO_ARCH_ESP8266
extern "C" {
#include "user_interface.h"
}
#endif

#include "icons.h"
#include "fonts.h"


// ===========================================
// Setup your board configuration here
// ===========================================

#define SDA_DISPLAY_PIN 4 //D2
#define SDC_DISPLAY_PIN 0 //D3

#define SDA_SCAN_PIN 12 // D6
#define SDC_SCAN_PIN 14 // D5

#define RGB_LED_PIN 2 // D4
#define RGB_LED_COUNT 2

// Display Settings
// OLED will be checked with this address and this address+1
// so here 0x03c and 0x03d
#define I2C_DISPLAY_ADDRESS 0x3c
// Choose OLED Driver Type (one only)
#define OLED_SSD1306
//#define OLED_SH1106

// RGB Led on GPIO0 comment this line if you have no RGB LED
// Number of RGB Led (can be a ring or whatever) 

// ===========================================
// End of configuration
// ===========================================

#ifdef RGB_LED_PIN
#include <NeoPixelBus.h>
#endif

#if defined (OLED_SSD1306)
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>
#elif defined (OLED_SH1106)
#include <SH1106Wire.h>
#include <OLEDDisplayUi.h>
#endif


// value for HSL color
// see http://www.workwithcolor.com/blue-color-hue-range-01.htm
#define COLOR_RED              0
#define COLOR_ORANGE          30
#define COLOR_ORANGE_YELLOW   45
#define COLOR_YELLOW          60
#define COLOR_YELLOW_GREEN    90
#define COLOR_GREEN          120
#define COLOR_GREEN_CYAN     165
#define COLOR_CYAN           180
#define COLOR_CYAN_BLUE      210
#define COLOR_BLUE           240
#define COLOR_BLUE_MAGENTA   275
#define COLOR_MAGENTA        300
#define COLOR_PINK           350

#ifdef RGB_LED_PIN
NeoPixelBus<NeoRgbFeature, NeoEsp8266Dma400KbpsMethod>rgb_led(RGB_LED_COUNT, RGB_LED_PIN);
#endif

// Number of line to display for devices and Wifi
#define I2C_DISPLAY_DEVICE  4
#define WIFI_DISPLAY_NET    4

// OLED Driver Instantiation
SSD1306Wire  display(I2C_DISPLAY_ADDRESS, SDA_DISPLAY_PIN, SDC_DISPLAY_PIN);
OLEDDisplayUi ui( &display );

Ticker ticker;
bool readyForUpdate = true;  // flag to launch update (I2CScan)

bool has_display          = true;  // if I2C display detected
uint8_t NumberOfI2CDevice = 0;      // number of I2C device detected
uint8_t rgb_luminosity    = 50 ;    // Luminosity from 0 to 100%

char i2c_dev[I2C_DISPLAY_DEVICE][32]; // Array on string displayed

#ifdef RGB_LED_PIN
void LedRGBOFF(uint16_t led = 0);
void LedRGBON (uint16_t hue, uint16_t led = 0);
#else
void LedRGBOFF(uint16_t led = 0) {};
void LedRGBON (uint16_t hue, uint16_t led = 0) {};
#endif


#ifdef RGB_LED_PIN
/* ======================================================================
  Function: LedRGBON
  Purpose : Set RGB LED strip color, but does not lit it
  Input   : Hue of LED (0..360)
          led number (from 1 to ...), if 0 then all leds
  Output  : -
  Comments:
  ====================================================================== */
void LedRGBON (uint16_t hue, uint16_t led)
{
  uint8_t start = 0;
  uint8_t end   = RGB_LED_COUNT - 1; // Start at 0

  // Convert to neoPixel API values
  // H (is color from 0..360) should be between 0.0 and 1.0
  // S is saturation keep it to 1
  // L is brightness should be between 0.0 and 0.5
  // rgb_luminosity is between 0 and 100 (percent)
  RgbColor target = HslColor( hue / 360.0f, 1.0f, 0.005f * rgb_luminosity);

  // just one LED ?
  // Strip start 0 not 1
  if (led) {
    led--;
    start = led ;
    end   = start ;
  }

  for (uint8_t i = start ; i <= end; i++) {
    rgb_led.SetPixelColor(i, target);
    rgb_led.Show();
  }
}

/* ======================================================================
  Function: LedRGBOFF
  Purpose : light off the RGB LED strip
  Input   : Led number starting at 1, if 0=>all leds
  Output  : -
  Comments: -
  ====================================================================== */
void LedRGBOFF(uint16_t led)
{
  uint8_t start = 0;
  uint8_t end   = RGB_LED_COUNT - 1; // Start at 0

  // just one LED ?
  if (led) {
    led--;
    start = led ;
    end   = start ;
  }

  // stop animation, reset params
  for (uint8_t i = start ; i <= end; i++) {
    // clear the led strip
    rgb_led.SetPixelColor(i, RgbColor(0));
    rgb_led.Show();
  }
}
#endif

/* ======================================================================
  Function: i2cScan
  Purpose : scan I2C bus
  Input   : specifc address if looking for just 1 specific device
  Output  : number of I2C devices seens
  Comments: -
  ====================================================================== */
uint8_t i2c_scan(uint8_t address = 0xff)
{
  Wire.begin(SDA_SCAN_PIN, SDC_SCAN_PIN);
  Wire.setClock(100000);
  uint8_t error;
  int nDevices;
  uint8_t start = 1 ;
  uint8_t end   = 0x7F ;
  uint8_t index = 0;
  char device[16];
  char buffer[32];

  if (address >= start && address <= end) {
    start = address;
    end   = address + 1;
    Serial.print(F("Searching for device at address 0x"));
    Serial.printf("%02X ", address);
  } else {
    Serial.println(F("Scanning I2C bus ..."));
  }

  nDevices = 0;
  for (address = start; address < end; address++ ) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("Device ");

      if (address == 0x40)
        strcpy(device, "TH02" );
      else if (address == 0x29 || address == 0x39 || address == 0x49)
        strcpy(device, "TSL2561" );

      else if (address==0x50) {
        strcpy(device, "24AA02E64" );
        // This device respond to 0x50 to 0x57 address
        address+=0x07;
      }
      else if (address == 0x55 )
        strcpy(device, "BQ72441" );
      else if (address == 0x77 )
        strcpy(device, "BMP180" );
      else if (address == I2C_DISPLAY_ADDRESS || address == I2C_DISPLAY_ADDRESS + 1)
        strcpy(device, "OLED SSD1306" );
      else if (address >= 0x60 && address <= 0x62 ) {
        strcpy(device, "MCP4725_Ax" );
        device[9]= '0' + (address & 0x03);
      } else if (address >= 0x68 && address <= 0x6A ) {
        strcpy(device, "MCP3421_Ax" );
        device[9]= '0' + (address & 0x03);
      } else if (address == 0x64)
        strcpy(device, "ATSHA204" );
      else
        strcpy(device, "Unknown" );

      sprintf(buffer, "0x%02X : %s", address, device );
      if (index < I2C_DISPLAY_DEVICE) {
        strcpy(i2c_dev[index++], buffer );
      }

      Serial.println(buffer);
      nDevices++;
    }
    else if (error == 4)
    {
      Serial.printf("Unknow error at address 0x%02X", address);
    }

    yield();
  }
  if (nDevices == 0)
    Serial.println(F("No I2C devices found"));
  else
    Serial.printf("Scan done, %d device found\r\n", nDevices);

  return nDevices;
}


/* ======================================================================
  Function: updateData
  Purpose : update by rescanning I2C bus
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */
void updateData(OLEDDisplay *display) {

  Serial.println(F("Starting Scan"));
  uint8_t pbar = 0;
  //LedRGBON(100,0);
  Wire.begin(SDA_DISPLAY_PIN, SDC_DISPLAY_PIN);
  for (pbar=0; pbar <= 100; pbar += 4 ){
    drawProgress(display, pbar, F("Scanning I2C"));
  }
  NumberOfI2CDevice = i2c_scan();
  Wire.begin(SDA_DISPLAY_PIN, SDC_DISPLAY_PIN);
  drawFrameI2C(display,(OLEDDisplayUiState*) 0, 0, 0);
  display->display();
  readyForUpdate = false;
  //LedRGBOFF();
}

/* ======================================================================
  Function: drawProgress
  Purpose : prograss indication
  Input   : OLED display pointer
          percent of progress (0..100)
          String above progress bar
          String below progress bar
  Output  : -
  Comments: -
  ====================================================================== */
void drawProgress(OLEDDisplay *display, int percentage, String labeltop, String labelbot) {
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(Roboto_Condensed_Bold_Bold_16);
    display->drawString(64, 8, labeltop);
    display->drawProgressBar(10, 28, 108, 12, percentage);
    display->drawString(64, 48, labelbot);
    display->display();
}

/* ======================================================================
  Function: drawProgress
  Purpose : prograss indication
  Input   : OLED display pointer
          percent of progress (0..100)
          String above progress bar
  Output  : -
  Comments: -
  ====================================================================== */
void drawProgress(OLEDDisplay *display, int percentage, String labeltop ) {
  drawProgress(display, percentage, labeltop, String(""));
}

/* ======================================================================
  Function: drawFrameI2C
  Purpose : I2C info screen (called by OLED ui)
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */
void drawFrameI2C(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char buff[16];
  sprintf(buff, "%d I2C Device%c", NumberOfI2CDevice, NumberOfI2CDevice > 1 ? 's' : ' ');

  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(Roboto_Condensed_Bold_Bold_16);
  display->drawString(x + 64, y +  0, buff);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  //display->setFont(Roboto_Condensed_Plain_16);
  display->setFont(Roboto_Condensed_12);

  for (uint8_t i = 0; i < NumberOfI2CDevice; i++) {
    if (i < I2C_DISPLAY_DEVICE)
      display->drawString(x + 0, y + 16 + 12 * i, i2c_dev[i]);
  }
  ui.disableIndicator();
}


/* ======================================================================
  Function: drawFrameLogo
  Purpose : Company logo info screen (called by OLED ui)
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */
void drawFrameLogo(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->clear();
  display->drawXbm(x + (128 - ch2i_width) / 2, y, ch2i_width, ch2i_height, ch2i_bits);
  ui.disableIndicator();
}

// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
FrameCallback frames[] = { drawFrameI2C };
int numberOfFrames = 1;


/* ======================================================================
  Function: setReadyForUpdate
  Purpose : Called by ticker to tell main loop we need to update data
  Input   : -
  Output  : -
  Comments: -
  ====================================================================== */
void setReadyForUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForUpdate = true;
}

/* ======================================================================
  Function: setup
  Purpose : you should know ;-)
  ====================================================================== */
void setup()
{
  uint16_t led_color ;
  char thishost[33];
  uint8_t pbar = 0;
  uint64_t chipid;  

  Serial.begin(115200);
  // I like to know what sketch is running on board
  Serial.print(F("\r\nBooting on "));
  Serial.printf_P( PSTR("Scan-I2C-ino on %s %s %s\n"), "LOLWUT", __DATE__ , __TIME__ );
  #ifdef ARDUINO_ARCH_ESP8266
    Serial.print(F("ESP8266 Core Version "));
    Serial.println(ESP.getCoreVersion());
    chipid = ESP.getChipId();
  #else
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    Serial.printf_P(PSTR("ESP32 %d cores\nWiFi%s%s\n"), chip_info.cores,
        (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    Serial.printf_P(PSTR("ESP Rev.%d\n"), chip_info.revision);
    Serial.printf_P(PSTR("%dMB %s Flash\n"), spi_flash_get_chip_size() / (1024 * 1024),
        (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "int." : "ext.");  
    chipid=ESP.getEfuseMac();
  #endif


  //Wire.pins(SDA, SCL);


  if (has_display) {
    Serial.println(F("Display found"));
    // initialize dispaly
    display.init();
    display.flipScreenVertically();
    display.clear();
    display.drawXbm((128 - ch2i_width) / 2, 0, ch2i_width, ch2i_height, ch2i_bits);
    display.display();

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setContrast(255);
    delay(200);
  }

  // Set WiFi to station mode and disconnect from an AP if it was previously connected

  // Loop until connected or 20 sec time out
  if (has_display) {
    ui.setTargetFPS(30);
    ui.setFrameAnimation(SLIDE_LEFT);
    ui.setFrames(frames, numberOfFrames);
    ui.init();
    ui.disableAutoTransition();
    display.flipScreenVertically();
  }


  // Rescan I2C every 5 seconds
  ticker.attach(5, setReadyForUpdate);
}

/* ======================================================================
  Function: loop
  Purpose : you should know ;-)
  ====================================================================== */
void loop()
{
  if (has_display) {
    if (readyForUpdate ) {
      updateData(&display);
    }
  }

}

