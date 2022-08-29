#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Notecard.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_BMP581.h"

#define serialDebugOut Serial
#define PRODUCT_UID "com.blues.nf1"
Notecard notecard;

#define BUTTON_A  9
#define BUTTON_B  6
#define BUTTON_C  5

unsigned long startMillis;
unsigned long currentMillis;
const unsigned long period = 1000 * 300;

uint8_t lastFloor = 0;
float floorHeight = 4.2672; // Height in meters (14 ft)
float seaLevelPressure = 986.60; // Starting Sea Level Pressure in HPa
float firstFloorAltitude = 0;
float firstFloorPressure = seaLevelPressure;

bool setFirstFloor = false;

// Forward declarations
void captureAndSendReadings(void);

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
Adafruit_BMP581 bmp;

void setup() {
  serialDebugOut.begin(115200);
  notecard.setDebugOutputStream(serialDebugOut);
  delay(2500);
  serialDebugOut.println("Floor Level Detector");
  serialDebugOut.println("====================");

  Wire.begin();
  notecard.begin();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  serialDebugOut.println("OLED connected...");

  if (!bmp.begin_I2C())
  {
    serialDebugOut.println("Could not find a valid BMP581 sensor, check wiring!");
    while (1);
  }

  serialDebugOut.println("BMP581 connected...");

  /* Highest accuracy for stationary devices */
  bmp.setTemperatureOversampling(BMP5_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP5_OVERSAMPLING_128X);
  bmp.setIIRFilterCoeff(BMP5_IIR_FILTER_COEFF_127);
  bmp.setOutputDataRate(BMP5_ODR_10_HZ);

  /* Suggestions from Bosch for movement */
  // bmp.setTemperatureOversampling(BMP5_OVERSAMPLING_2X);
  // bmp.setPressureOversampling(BMP5_OVERSAMPLING_16X);
  // bmp.setIIRFilterCoeff(BMP5_IIR_FILTER_COEFF_3);
  // bmp.setOutputDataRate(BMP5_ODR_50_HZ);

  // Throw the first one away
  bmp.performReading();

  // Clear the buffer.
  display.clearDisplay();
  display.display();

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  // text display tests
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("NF1");
  display.println("Indoor Floor Level Tracker");
  display.setCursor(0,0);
  display.display();

  J *req = notecard.newRequest("hub.set");
  JAddStringToObject(req, "product", PRODUCT_UID);
  JAddStringToObject(req, "mode", "continuous");
  notecard.sendRequest(req);

  // Enable Outboard DFU
  req = notecard.newRequest("card.dfu");
  JAddStringToObject(req, "name", "stm32");
  JAddBoolToObject(req, "on", true);
  notecard.sendRequest(req);

  delay(2000);

  captureAndSendReadings();
  startMillis = millis();
}

void loop() {
  currentMillis = millis();

  if (currentMillis - startMillis >= period) {
    captureAndSendReadings();

    startMillis = currentMillis;
  }
}

void captureAndSendReadings() {
  if (! bmp.performReading()) {
      Serial.println("Failed to perform reading :(");
      return;
    }

    float temp = bmp.temperature;
    float pressure = bmp.pressure / 100.0;

    if (setFirstFloor) {
      firstFloorPressure = pressure;
    }

    float altitude = bmp.readAltitude(firstFloorPressure);
    int8_t floor = altitude / floorHeight + 1;

    Serial.print("Temperature = ");
    Serial.print(temp);
    Serial.println(" *C");

    Serial.print("Pressure = ");
    Serial.print(pressure);
    Serial.println(" hPa");

    Serial.print("Approx. Altitude = ");
    Serial.print(altitude);
    Serial.println(" m");

    Serial.print("Floor = ");
    Serial.println(floor);

    Serial.println();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Press. ");
    display.println(pressure);
    display.print("Alt.   ");
    display.println(altitude);
    display.print("Floor: ");
    display.print(floor);
    display.display();

    J *req = notecard.newRequest("note.add");
    JAddBoolToObject(req, "sync", true);
    J *body = JCreateObject();

    JAddNumberToObject(body, "floor", floor);
    JAddNumberToObject(body, "altitude", altitude);
    JAddNumberToObject(body, "pressure", pressure);
    JAddNumberToObject(body, "temp", temp);
    JAddItemToObject(req, "body", body);
    notecard.sendRequest(req);
}
