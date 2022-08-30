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
#define ATTN_INPUT_PIN 5
#define DEBUG true

unsigned long startMillis;
unsigned long currentMillis;
const unsigned long period = 1000 * 300;

struct environmentVariables {
  bool live;
  double floorHeight;
  int baselineFloor;
  float baselineFloorPressure;
  int noMovementThreshold;
};

bool setBaselineFloor = false;
environmentVariables envVars;

// Set to true whenever ATTN interrupt occurs
static bool attnInterruptOccurred;

Notecard notecard;

// Forward declarations
void attnISR(void);
void attnArm();
void fetchEnvironmentVariables(environmentVariables);
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
  // bmp.setTemperatureOversampling(BMP5_OVERSAMPLING_8X);
  // bmp.setPressureOversampling(BMP5_OVERSAMPLING_128X);
  // bmp.setIIRFilterCoeff(BMP5_IIR_FILTER_COEFF_127);
  // bmp.setOutputDataRate(BMP5_ODR_10_HZ);

  /* Suggestions from Bosch for devices in motion */
  bmp.setTemperatureOversampling(BMP5_OVERSAMPLING_2X);
  bmp.setPressureOversampling(BMP5_OVERSAMPLING_16X);
  bmp.setIIRFilterCoeff(BMP5_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP5_ODR_50_HZ);

  // Throw the first one away
  bmp.performReading();

  // Clear the display buffer.
  display.clearDisplay();
  display.display();

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

  // Check Environment Variables
  fetchEnvironmentVariables(envVars);

  // Disarm ATTN To clear any previous state before rearming
  req = notecard.newRequest("card.attn");
  JAddStringToObject(req, "mode", "disarm,-env");
  notecard.sendRequest(req);

  // Configure ATTN to wait for a specific list of files
  req = notecard.newRequest("card.attn");
  JAddStringToObject(req, "mode", "arm,env");
  notecard.sendRequest(req);

  // Attach an interrupt pin
  pinMode(ATTN_INPUT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(ATTN_INPUT_PIN), attnISR, RISING);

  // Arm the interrupt, so that we are notified whenever ATTN rises
  attnArm();

  delay(2000);

  captureAndSendReadings();
  startMillis = millis();
}

void loop() {

  if (attnInterruptOccurred) {
    fetchEnvironmentVariables(envVars);

    attnArm();
  }

  if (!envVars.live) {
    currentMillis = millis();

    if (currentMillis - startMillis >= period) {
      captureAndSendReadings();

      startMillis = currentMillis;
    }
  } else {
    // Behave differently if we are live
  }
}

void fetchEnvironmentVariables(environmentVariables vars) {
  J *req = NoteNewRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("active"));
  JAddItemToArray(names, JCreateString("baseline_floor"));
  JAddItemToArray(names, JCreateString("floor_height"));
  JAddItemToArray(names, JCreateString("no_movement_threshold"));

  J *rsp = NoteRequestResponse(req);
  if (rsp != NULL) {
      if (notecard.responseError(rsp)) {
          notecard.deleteResponse(rsp);
          return;
      }

      // Get the note's body
      J *body = JGetObject(rsp, "body");
      if (body != NULL) {
          vars.baselineFloor = atoi(JGetString(body, "baseline_floor"));
          vars.floorHeight = atof(JGetString(body, "floor_height"));
          vars.noMovementThreshold = atoi(JGetString(body, "no_movement_threshold"));
          char *liveStr = JGetString(body, "live");

          if (liveStr == "true" || liveStr == "1") {
            vars.live = true;
          } else {
            vars.live = false;
          }

          serialDebugOut.printf("\nBaseline Floor: %d\n", vars.baselineFloor);
          serialDebugOut.print("Floor Height: ");
          serialDebugOut.print(vars.floorHeight);
          serialDebugOut.printf("\nMovement Threshold: %d\n", vars.noMovementThreshold);
          serialDebugOut.printf("Live: %s\n\n", vars.live ? "true" : "false");

          setBaselineFloor = true;
          envVars = vars;
      }

  }
  notecard.deleteResponse(rsp);
}

void captureAndSendReadings() {
  if (! bmp.performReading()) {
      serialDebugOut.println("Failed to perform reading from pressure sensor.");
      return;
    }

    float temp = bmp.temperature;
    float pressure = bmp.pressure / 100.0;

    if (setBaselineFloor) {
      envVars.baselineFloorPressure = pressure;

      setBaselineFloor = false;
    }

    float altitude = bmp.readAltitude(envVars.baselineFloorPressure);
    int8_t floor = (altitude / envVars.floorHeight) + 1;

    serialDebugOut.print("Temperature = ");
    serialDebugOut.print(temp);
    serialDebugOut.println(" *C");

    serialDebugOut.print("Pressure = ");
    serialDebugOut.print(pressure);
    serialDebugOut.println(" hPa");

    serialDebugOut.print("Approx. Altitude = ");
    serialDebugOut.print(altitude);
    serialDebugOut.println(" m");

    serialDebugOut.print("Floor = ");
    serialDebugOut.println(floor);

    serialDebugOut.println();

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

void attnISR()
{
    attnInterruptOccurred = true;
}

// Re-arm the ATTN interrupt
void attnArm()
{
    // Make sure that we pick up the next RISING edge of the interrupt
    attnInterruptOccurred = false;

    // Set the ATTN pin low and reset to the previous state of waiting for
    // Env var updates
    J *req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "reset");
    notecard.sendRequest(req);

}
