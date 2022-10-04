#include "Adafruit_BMP581.h"
#include "metadata.h"
#include <Arduino.h>
#include <FIR.h>
#include <Notecard.h>
#include <Wire.h>
#include <floor_tracker.h>

// Uncomment to use a connected SSD1306 Display
// #define USE_DISPLAY

// Uncomment to output the floor sample every second
// #define DEBUG_FLOOR_SAMPLES

#ifdef USE_DISPLAY
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

#define serialDebugOut Serial
// Uncomment to view Note requests from the Host
// #define DEBUG_NOTECARD

#define PRODUCT_UID "com.blues.nf1"
#define ENV_POLL_SECS 1
#define IDLE_UPDATE_PERIOD (1000 * 60 * 5)
#define LIVE_UPDATE_PERIOD (1000 * 60 * 1)
#define NO_MOVEMENT_THRESHOLD_SCALE_MS \
  (1000) // No movement threshold given in seconds.
#define FLOOR_SAMPLE_PERIOD (250)
#define FLOOR_FILTER_ORDER (10)
#define FLOOR_OFFSET (0.3)

// Variables for Env Var polling
static unsigned long nextPollMs = 0;
static uint32_t lastModifiedTime = 0;
// Variables for sensor reading period when not in live mode
static unsigned long lastUpdateMs;
static unsigned long updatePeriod;
static unsigned long lastFloorSampleMs;

FIR<double, FLOOR_FILTER_ORDER> floorFilter;

bool setBaselineFloor = false;
applicationState state = {0};

Notecard notecard;

// Forward declarations
void fetchEnvironmentVariables(applicationState &state);
bool pollEnvVars(void);
sensorReadings captureSensorReadings(void);
void sendSensorReadings(const sensorReadings &readings, bool alarm = false);
void displayReadings(const sensorReadings &readings);
bool publishSensorReadings(sensorReadings &readings, uint32_t currentMillis);

#ifdef USE_DISPLAY
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
#endif

Adafruit_BMP581 bmp;

void setup()
{
  serialDebugOut.begin(115200);
#ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
  delay(1500);
  serialDebugOut.println("Floor Level Detector");
  serialDebugOut.println("====================");

  Wire.begin();
  notecard.begin();

#ifdef USE_DISPLAY
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  serialDebugOut.println("OLED connected...");
#endif

  if (!bmp.begin_I2C())
  {
    serialDebugOut.println(
        "Could not find a valid BMP581 sensor, check wiring!");
    while (1)
      ;
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

  double coef[FLOOR_FILTER_ORDER];
  for (int i = 0; i < FLOOR_FILTER_ORDER; i++)
  {
    coef[i] = 1.0; // rolling average
  }
  floorFilter.setFilterCoeffs(coef);

#ifdef USE_DISPLAY
  // Clear the display buffer.
  display.clearDisplay();
  display.display();

  // text display tests
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("NF1");
  display.println("Indoor Floor Level Tracker");
  display.setCursor(0, 0);
  display.display();
#endif

  J *req = notecard.newRequest("hub.set");
  if (req != NULL)
  {
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);
  }

  // Notify Notehub of the current firmware version
  req = notecard.newRequest("dfu.status");
  if (req != NULL)
  {
    JAddStringToObject(req, "version", firmwareVersion());
    notecard.sendRequest(req);
  }

  // Enable Outboard DFU
  req = notecard.newRequest("card.dfu");
  if (req != NULL)
  {
    JAddStringToObject(req, "name", "stm32");
    JAddBoolToObject(req, "on", true);
    notecard.sendRequest(req);
  }

  // Check Environment Variables
  fetchEnvironmentVariables(state);

  delay(2000);

  if (!state.live)
  {
    if (state.floorHeight != 0.0 && state.baselineFloor != 0)
    {
      sensorReadings readings = captureSensorReadings();
      displayReadings(readings);
      sendSensorReadings(readings);
    }
    else
    {
      serialDebugOut.println(
          "Waiting for Environment Variables from the Notecard");
    }
  }
  lastUpdateMs = millis();
}

void resetFloorFilter(double floor)
{
  for (int i = 0; i < FLOOR_FILTER_ORDER; i++)
  {
    floorFilter.processReading(floor);
  }
}

void loop()
{
  if (pollEnvVars())
  {
    fetchEnvironmentVariables(state);

    if (state.variablesUpdated)
    {
      serialDebugOut.println("Environment Variable Updates Received");

      J *req = notecard.newRequest("note.add");
      if (req != NULL)
      {
        JAddStringToObject(req, "file", "notify.qo");
        JAddBoolToObject(req, "sync", true);
        J *body = JCreateObject();
        if (body != NULL)
        {
          JAddStringToObject(body, "message",
                            "environment variable update received");
          JAddItemToObject(req, "body", body);
          notecard.sendRequest(req);
        }
      }
      state.variablesUpdated = false;
    }
  }

  const uint32_t currentMillis = millis();
  if (!state.live)
  {
    if (currentMillis - lastUpdateMs >= updatePeriod)
    {
      if (state.floorHeight != 0.0 && state.baselineFloor != 0)
      {
        sensorReadings readings = captureSensorReadings();
        displayReadings(readings);
        lastUpdateMs = currentMillis;
        sendSensorReadings(readings);
      }
      else
      {
        serialDebugOut.println(
            "Waiting for Environment Variables from the Notecard");
      }
    }
  }
  else
  {
    if (state.floorHeight != 0.0 && state.baselineFloor != 0)
    {
      sensorReadings readings = captureSensorReadings();
      if ((currentMillis - lastFloorSampleMs) >= FLOOR_SAMPLE_PERIOD)
      {
        lastFloorSampleMs = currentMillis;
        publishSensorReadings(readings, currentMillis);
      }
    }
    else
    {
      serialDebugOut.println(
          "Waiting for Environment Variables from the Notecard");
    }
  }
}

bool publishSensorReadings(sensorReadings &readings, uint32_t currentMillis)
{
  bool send = false;
  bool alarm = false;
  bool floorChange = false;

  // run the floor signal through a FIR low-pass filter
  double filteredFloor = floorFilter.processReading(readings.floor);
  readings.currentFloor = (int)(filteredFloor + FLOOR_OFFSET);

#ifdef DEBUG_FLOOR_SAMPLES
  serialDebugOut.print("floor sample: ");
  serialDebugOut.print(readings.floor);
  serialDebugOut.print(", filtered: ");
  serialDebugOut.print(filteredFloor);
  serialDebugOut.println();
#endif

  if (state.baselineChanged)
  {
    send = true;
    floorChange = true;
    state.alarmSent = false;
    state.baselineChanged = false;
    serialDebugOut.println("Baseline changed. Sending a Note.");
  }
  else if (readings.currentFloor != state.lastFloor)
  {
    send = true;
    floorChange = true;
    state.alarmSent = false;
    serialDebugOut.println("New floor detected. Sending a Note.");
  }
  else if (!state.alarmSent && state.lastFloorChangeAt &&
           state.lastFloorChangeAt < currentMillis &&
           state.noMovementThreshold &&
           ((currentMillis - state.lastFloorChangeAt) >
            (state.noMovementThreshold * NO_MOVEMENT_THRESHOLD_SCALE_MS)))
  {
    serialDebugOut.println(
        "**ALARM**: No movement between floors detected. Sending a Note.");
    send = true;
    alarm = true;
    state.alarmSent = true;
  }
  else if (currentMillis - lastUpdateMs >= updatePeriod)
  {
    send = true;
    serialDebugOut.println("Update period elapsed. Sending a Note.");
  }
  if (send)
  {
    displayReadings(readings);
    sendSensorReadings(readings, alarm);
    lastUpdateMs = currentMillis;
  }
  if (floorChange)
  {
    state.lastFloor = readings.currentFloor;
    state.lastFloorChangeAt = currentMillis;
  }
  return send;
}

void fetchEnvironmentVariables(applicationState &vars)
{
  J *req = notecard.newRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("live"));
  JAddItemToArray(names, JCreateString("baseline_floor"));
  JAddItemToArray(names, JCreateString("floor_height"));
  JAddItemToArray(names, JCreateString("no_movement_threshold"));

  J *rsp = notecard.requestAndResponse(req);
  if (rsp != NULL)
  {
    if (notecard.responseError(rsp))
    {
      notecard.deleteResponse(rsp);
      return;
    }

    // Get the note's body
    J *body = JGetObject(rsp, "body");
    if (body != NULL)
    {
      int newBaselineFloor = atoi(JGetString(body, "baseline_floor"));
      if (newBaselineFloor != vars.baselineFloor)
      {
        setBaselineFloor = true;
        vars.baselineFloor = newBaselineFloor;
        vars.variablesUpdated = true;
      }

      float floorHeight = atof(JGetString(body, "floor_height"));
      if (floorHeight != vars.floorHeight)
      {
        vars.floorHeight = floorHeight;
        vars.variablesUpdated = true;
      }

      int noMovementThreshold = atoi(JGetString(body, "no_movement_threshold"));
      if (noMovementThreshold != vars.noMovementThreshold)
      {
        vars.noMovementThreshold = noMovementThreshold;
        vars.variablesUpdated = true;
      }

      char *liveStr = JGetString(body, "live");
      bool wasLive = vars.live;
      vars.live = (strcmp(liveStr, "true") == 0 || strcmp(liveStr, "1") == 0);
      if (vars.live && vars.live != wasLive)
      {
        // when the device becomes live, calibrate to the floor level given
        setBaselineFloor = true;
      } else if (vars.live != wasLive) {
        vars.variablesUpdated = true;
      }
      vars.alarmSent = false;
      updatePeriod = state.live ? LIVE_UPDATE_PERIOD : IDLE_UPDATE_PERIOD;

      serialDebugOut.printf("\nBaseline Floor: %d\n", vars.baselineFloor);
      serialDebugOut.print("Floor Height: ");
      serialDebugOut.print(vars.floorHeight);
      serialDebugOut.printf("\nMovement Threshold: %d\n",
                            vars.noMovementThreshold);
      serialDebugOut.printf("Live: %s\n\n", vars.live ? "true" : "false");
    }
  }
  notecard.deleteResponse(rsp);
}

bool pollEnvVars()
{
  if (millis() < nextPollMs)
  {
    return false;
  }

  nextPollMs = millis() + (ENV_POLL_SECS * 1000);

  J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));

  if (rsp == NULL)
  {
    return false;
  }

  uint32_t modifiedTime = JGetInt(rsp, "time");
  notecard.deleteResponse(rsp);
  if (lastModifiedTime == modifiedTime)
  {
    return false;
  }

  lastModifiedTime = modifiedTime;

  return true;
}

sensorReadings captureSensorReadings()
{
  sensorReadings readings;

  if (!bmp.performReading())
  {
    serialDebugOut.println("Failed to perform reading from pressure sensor.");
  }
  readings.readingTimestamp = millis();
  readings.temp = bmp.temperature;
  readings.pressure = bmp.pressure / 100.0;

  if (setBaselineFloor)
  {
    state.baselineChanged = true;
    resetFloorFilter(state.baselineFloor);
    state.baselineFloorPressure = readings.pressure;
    state.lastFloor =
        state.baselineFloor; // forget about the previous floor since this isn't
                             // an actual change in floor/altitude
    state.lastFloorChangeAt = millis();
    state.alarmSent = false;
    setBaselineFloor = false;
    serialDebugOut.println("Setting baseline floor / pressure");
    serialDebugOut.print("baseline floor: ");
    serialDebugOut.println(state.baselineFloor);
    serialDebugOut.print("baseline pressure:");
    serialDebugOut.print(state.baselineFloorPressure);
    serialDebugOut.println(" hPa");
  }

  readings.altitude = bmp.readAltitude(state.baselineFloorPressure);
  readings.floor =
      ((readings.altitude / state.floorHeight)) + state.baselineFloor;

  readings.currentFloor = (int)(readings.floor + FLOOR_OFFSET);

  return readings;
}

void sendSensorReadings(const sensorReadings &readings, bool alarm)
{
  J *req = notecard.newRequest("note.add");
  if (req != NULL)
  {
    JAddBoolToObject(req, "sync", true);
    JAddStringToObject(req, "file", "floor.qo");
    J *body = JCreateObject();
    if (body != NULL)
    {
      JAddNumberToObject(body, "floor", readings.currentFloor);
      JAddNumberToObject(body, "prevFloor", state.lastFloor);
      JAddNumberToObject(body, "altitude", readings.altitude);
      JAddNumberToObject(body, "pressure", readings.pressure);
      JAddNumberToObject(body, "temp", readings.temp);
      JAddNumberToObject(body, "direction",
                         readings.currentFloor - state.lastFloor);
      JAddStringToObject(body, "app", "nf1");
      JAddItemToObject(req, "body", body);
      notecard.sendRequest(req);

      serialDebugOut.println("Sending floor.qo");
    }
  }

  if (alarm)
  {
    J *req = notecard.newRequest("note.add");
    if (req != NULL)
    {
      JAddBoolToObject(req, "sync", true);
      JAddStringToObject(req, "file", "alarm.qo");
      J *body = JCreateObject();
      if (body != NULL)
      {
        JAddBoolToObject(body, "alarm", true);
        JAddStringToObject(body, "app", "nf1");
        JAddItemToObject(req, "body", body);
        notecard.sendRequest(req);

        serialDebugOut.println("Sending alarm.qo");
      }
    }
  }
}

void displayReadings(const sensorReadings &readings)
{
  serialDebugOut.print("Temperature = ");
  serialDebugOut.print(readings.temp);
  serialDebugOut.println(" *C");

  serialDebugOut.print("Pressure = ");
  serialDebugOut.print(readings.pressure);
  serialDebugOut.println(" hPa");

  serialDebugOut.print("Approx. Altitude = ");
  serialDebugOut.print(readings.altitude);
  serialDebugOut.println(" m");

  serialDebugOut.print("Previous Floor = ");
  serialDebugOut.println(state.lastFloor);

  serialDebugOut.print("Floor = ");
  serialDebugOut.println(readings.currentFloor);

  serialDebugOut.print("Floor = ");
  serialDebugOut.println(readings.floor);

  serialDebugOut.println();

#ifdef USE_DISPLAY
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Press. ");
  display.println(readings.pressure);
  display.print("Alt.   ");
  display.println(readings.altitude);
  display.print("Floor: ");
  display.print(readings.currentFloor);
  display.display();
#endif
}
