#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Notecard.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_BMP581.h"

#define serialDebugOut Serial
// #define DEBUG_NOTECARD
#define PRODUCT_UID "com.blues.nf1"
#define ENV_POLL_SECS	1

// Variables for Env Var polling
static unsigned long nextPollMs = 0;
static uint32_t lastModifiedTime = 0;
// Variables for sensor reading period when not in live mode
static unsigned long startMs;
static unsigned long currentMillis;
const unsigned long period = 1000 * 300;

struct environmentVariables {
  bool live;
  double floorHeight;
  int baselineFloor;
  float baselineFloorPressure;
  int currentFloor;
  int noMovementThreshold;
};

bool setBaselineFloor = false;
environmentVariables envVars;

Notecard notecard;

// Forward declarations
void fetchEnvironmentVariables(environmentVariables);
bool pollEnvVars();
void captureAndSendReadings(void);

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
Adafruit_BMP581 bmp;

void setup() {
  serialDebugOut.begin(115200);
#ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
  delay(500);
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
  JAddBoolToObject(req, "sync", true);
  notecard.sendRequest(req);

  // Enable Outboard DFU
  req = notecard.newRequest("card.dfu");
  JAddStringToObject(req, "name", "stm32");
  JAddBoolToObject(req, "on", true);
  notecard.sendRequest(req);

  // Check Environment Variables
  fetchEnvironmentVariables(envVars);

  delay(2000);

  captureAndSendReadings();
  startMs = millis();
}

void loop() {

  if (pollEnvVars()) {
    fetchEnvironmentVariables(envVars);

    serialDebugOut.println("Environment Variable Updates Received");

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "notify.qo");
    JAddBoolToObject(req, "sync", true);
    J *body = JCreateObject();

    JAddStringToObject(body, "message", "environment variable update received");
    JAddItemToObject(req, "body", body);
    notecard.sendRequest(req);
  }

  if (!envVars.live) {
    currentMillis = millis();

    if (currentMillis - startMs >= period) {
      captureAndSendReadings();

      startMs = currentMillis;
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

      // Set the lastModifiedTime based on the return
      lastModifiedTime = JGetNumber(rsp, "time");

      // Get the note's body
      J *body = JGetObject(rsp, "body");
      if (body != NULL) {
          vars.baselineFloor = atoi(JGetString(body, "baseline_floor"));
          vars.floorHeight = atof(JGetString(body, "floor_height"));
          vars.noMovementThreshold = atoi(JGetString(body, "no_movement_threshold"));

          char *liveStr = JGetString(body, "live");
          bool isLive = false;

          if (liveStr == "true" || liveStr == "1") {
            vars.live = true;
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

bool pollEnvVars() {
  if (millis() < nextPollMs) {
    return false;
  }

  nextPollMs = millis() + (ENV_POLL_SECS * 1000);

  J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));

	if (rsp == NULL) {
		return false;
	}

	uint32_t modifiedTime = JGetInt(rsp, "time");
  notecard.deleteResponse(rsp);
	if (lastModifiedTime == modifiedTime) {
		return false;
	}

	lastModifiedTime = modifiedTime;

  return true;
}

void captureAndSendReadings() {
  if (!bmp.performReading()) {
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
