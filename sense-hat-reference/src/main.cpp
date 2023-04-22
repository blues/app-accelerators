#include <Arduino.h>
#include <Notecard.h>
#include "Adafruit_BMP581.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_VCNL4040.h>
#include <vl53l4cx_class.h>
#include <Wire.h>
#include <MEMSAudioCapture.h>

#define serialDebugOut Serial
// Uncomment to view Note requests from the Host
#define DEBUG_NOTECARD

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.your-company:your-product-name"
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

#define UPDATE_PERIOD (15000) // (1000 * 60 * 5)
#define VOLUME_UPDATE_PERIOD (1000)
#define LED_MONITOR_MIC 1       // set to 0 to have the LED monitor the notecard LED
static unsigned long lastUpdateMs;
static unsigned long lastVolumeUpdateMs;

// Forward Declarations
void getBMP581Readings(J *body);
void getAccelerometerReadings(J *body);
void getBME280Readings(J *body);
void getVCNL4040Readings(J *body);
void getVL53L4Readings(J *body);

Notecard notecard;
Adafruit_BMP581 bmp;
Adafruit_BME280 bme;
Adafruit_VCNL4040 vcnl4040 = Adafruit_VCNL4040();
VL53L4CX vl53l4cx(&Wire, D10);
MEMSAudioCapture mic;

/**
 * @brief Computes the root mean square of a number of PCM samples.
 */
class RMS {
  double square;
  uint32_t sampleCount;
public:
  void accumulate(pcm_sample_t* samples, size_t sampleCount) {
    this->sampleCount += sampleCount;
    while (sampleCount-->0) {
      int32_t sample = samples[sampleCount];
      square += sample * sample;
    }
  }

  double computeRMS() {
    noInterrupts();
    double mean = square/sampleCount;
    square = 0;
    sampleCount = 0;
    interrupts();
    double root = sqrt(mean);
    return root;
  }
};

void displayRMS(double rmsValue);

RMS rms;

void updateAudioVolume(MemsAudio* audio, pcm_sample_t* pcmSamples, size_t pcmSamplesLength) {
  rms.accumulate(pcmSamples, pcmSamplesLength);
}

void setup()
{
  #ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
  // Configure Serial1 for Notecard AUX communication
  Serial1.begin(115200);
  delay(1500);
  serialDebugOut.println("Sense Hat Tester");
  serialDebugOut.println("================");

  Wire.begin();
  notecard.begin();
  if (!mic.begin(updateAudioVolume)) {
      serialDebugOut.println("Unable to start audio capture.");
  }
  if (!bmp.begin_I2C())
  {
    serialDebugOut.println(
        "Could not find a valid BMP581 sensor, check wiring!");
  }
  bmp.setTemperatureOversampling(BMP5_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP5_OVERSAMPLING_128X);
  bmp.setIIRFilterCoeff(BMP5_IIR_FILTER_COEFF_127);
  bmp.setOutputDataRate(BMP5_ODR_10_HZ);

  if (!bme.begin())
  {
    serialDebugOut.println(
        "Could not find a valid BME280 sensor, check wiring!");
  }

  if (!vcnl4040.begin()) {
    serialDebugOut.println(
        "Couldn't find VCNL4040, check wiring!");
  }

  vl53l4cx.begin();
  vl53l4cx.VL53L4CX_Off();
  vl53l4cx.InitSensor(0x52);
  vl53l4cx.VL53L4CX_StartMeasurement();

  J *req = notecard.newRequest("hub.set");
  if (req != NULL)
  {
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
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

  // Turn on Neopixel Monitoring mode. Will enable the COMM LED
  // And COMM TEST button. Pressing the button will send a note
  // to the Notecard.
  // When LED_MONITOR_MIC is 1, the LED monitors the ambient noise level
  req = notecard.newRequest("card.aux");
  if (req != NULL)
  {
    JAddStringToObject(req, "mode", LED_MONITOR_MIC ? "neo-monitor" : "neo");
    notecard.sendRequest(req);
  }

  // Stream Data from the Notecard Accelerometer
  req = notecard.newRequest("card.aux.serial");
  if (req != NULL) {
    JAddStringToObject(req, "mode", "notify,accel");
    JAddNumberToObject(req, "duration", UPDATE_PERIOD);
    notecard.sendRequest(req);
  }

  lastVolumeUpdateMs = lastUpdateMs = millis();
}

void loop()
{
  const uint32_t currentMillis = millis();

  if (currentMillis - lastVolumeUpdateMs >= VOLUME_UPDATE_PERIOD) {
      double rmsValue = rms.computeRMS();
      displayRMS(rmsValue);
      lastVolumeUpdateMs = currentMillis;
  }

  if (currentMillis - lastUpdateMs >= UPDATE_PERIOD)
  {
    J *req = notecard.newRequest("note.add");
    if (req != NULL)
    {
      JAddBoolToObject(req, "sync", true);
      J *body = JCreateObject();

      if (body != NULL)
      {
        getBMP581Readings(body);
        getAccelerometerReadings(body);
        getBME280Readings(body);
        getVCNL4040Readings(body);
        getVL53L4Readings(body);

        JAddItemToObject(req, "body", body);
        notecard.sendRequest(req);
      }
    }

    lastUpdateMs = currentMillis;
  }
}

void getBMP581Readings(J *body)
{
  if (!bmp.performReading())
  {
    serialDebugOut.println("Failed to perform reading from pressure sensor.");
  }

  J *bmp_body = JCreateObject();

  if (bmp_body != NULL)
  {
    JAddNumberToObject(bmp_body, "pressure", bmp.pressure / 100.0);
    JAddNumberToObject(bmp_body, "temp", bmp.temperature);

    JAddItemToObject(body, "bmp_readings", bmp_body);
  }
}

void getAccelerometerReadings(J *body)
{
  if (Serial1.available())
  {
    String reading = Serial1.readStringUntil('\n');

    // Print out raw Accel reading from Notecard
    // serialDebugOut.println(reading);

    J *rsp = JParse(reading.c_str());

    J *xval = JGetObjectItem(rsp, "x");
    J *yval = JGetObjectItem(rsp, "y");
    J *zval = JGetObjectItem(rsp, "z");

    J *accel_body = JCreateObject();

    if (accel_body != NULL)
    {
      JAddNumberToObject(accel_body, "x", xval->valueint);
      JAddNumberToObject(accel_body, "y", yval->valueint);
      JAddNumberToObject(accel_body, "z", zval->valueint);

      JAddItemToObject(body, "accel_readings", accel_body);
    }
  }
}

void getBME280Readings(J *body)
{
  J *bme_body = JCreateObject();

  if (bme_body != NULL)
  {
    JAddNumberToObject(bme_body, "temp", bme.readTemperature());
    JAddNumberToObject(bme_body, "humidity", bme.readHumidity());

    JAddItemToObject(body, "bme_readings", bme_body);
  }
}

void getVCNL4040Readings(J *body)
{
  J *vcnl_body = JCreateObject();

  if (vcnl_body != NULL)
  {
    JAddNumberToObject(vcnl_body, "proximity", vcnl4040.getProximity());
    JAddNumberToObject(vcnl_body, "ambient_light", vcnl4040.getLux());
    JAddNumberToObject(vcnl_body, "white_light", vcnl4040.getWhiteLight());

    JAddItemToObject(body, "vcnl_readings", vcnl_body);
  }
}

void getVL53L4Readings(J *body)
{
  VL53L4CX_MultiRangingData_t MultiRangingData;
  VL53L4CX_MultiRangingData_t *pMultiRangingData = &MultiRangingData;
  uint8_t NewDataReady = 0;
  int objects_found = 0, j;
  char report[64];
  int status;

  do
  {
    status = vl53l4cx.VL53L4CX_GetMeasurementDataReady(&NewDataReady);
  } while (!NewDataReady);

  if ((!status) && (NewDataReady != 0))
  {
    status = vl53l4cx.VL53L4CX_GetMultiRangingData(pMultiRangingData);
    objects_found = pMultiRangingData->NumberOfObjectsFound;

    snprintf(report, sizeof(report), "VL53L4CX Satellite: Count=%d, #Objs=%1d \n", pMultiRangingData->StreamCount, objects_found);
    serialDebugOut.print(report);

    if (objects_found > 0)
    {
      J *objects = JCreateArray();

      if (objects != NULL)
      {
        for (j = 0; j < objects_found; j++)
        {
          /*
          serialDebugOut.print("status=");
          serialDebugOut.print(pMultiRangingData->RangeData[j].RangeStatus);
          serialDebugOut.print(", Distance=");
          serialDebugOut.print(pMultiRangingData->RangeData[j].RangeMilliMeter);
          serialDebugOut.print("mm");
          serialDebugOut.print(", Signal=");
          serialDebugOut.print((float)pMultiRangingData->RangeData[j].SignalRateRtnMegaCps / 65536.0);
          serialDebugOut.print(" Mcps, Ambient=");
          serialDebugOut.print((float)pMultiRangingData->RangeData[j].AmbientRateRtnMegaCps / 65536.0);
          serialDebugOut.print(" Mcps\n");
          */

          J *tof_object = JCreateObject();

          if (tof_object != NULL)
          {
            JAddNumberToObject(tof_object, "distance_in_mm", pMultiRangingData->RangeData[j].RangeMilliMeter);
            JAddNumberToObject(tof_object, "signal_in_mcps", (float)pMultiRangingData->RangeData[j].SignalRateRtnMegaCps / 65536.0);
            JAddNumberToObject(tof_object, "ambient_noise_in_mcps", (float)pMultiRangingData->RangeData[j].AmbientRateRtnMegaCps / 65536.0);

            JAddItemToArray(objects, tof_object);
          }
        }

        JAddItemToObject(body, "tof_objects", objects);
      }
    }
    if (status == 0)
    {
      status = vl53l4cx.VL53L4CX_ClearInterruptAndStartMeasurement();
    }
  }
}

const uint8_t VOLUME_COLORS_COUNT = 5;
const char* volumeColors[] = {
  "white",
  "green",
  "yellow",
  "orange",
  "red"
};

void displayRMS(double rmsValue) {
  static int8_t lastColor = -1;
  int noise = 20;
  int color = sqrt((rmsValue-noise)*VOLUME_COLORS_COUNT*VOLUME_COLORS_COUNT/2000);    // values above 2000 are rare so shorten the scale
  if (color>=VOLUME_COLORS_COUNT) {
    color = VOLUME_COLORS_COUNT-1;
  }
  serialDebugOut.print("Audio RMS: ");
  serialDebugOut.println(rmsValue);
  if (true || color!=lastColor) {
    lastColor = color;
    J* req = notecard.newRequest("card.led");
    if (req != NULL)
    {
      JAddStringToObject(req, "mode", volumeColors[color]);
      JAddBoolToObject(req, "on", true);
      notecard.sendRequest(req);
    }
  }
}


