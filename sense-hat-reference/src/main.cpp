#include <Arduino.h>
#include <Notecard.h>
#include "Adafruit_BMP581.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_VCNL4040.h>
#include <vl53l4cx_class.h>
#include <Wire.h>

#define serialDebugOut Serial
// Uncomment to view Note requests from the Host
#define DEBUG_NOTECARD
#define PRODUCT_UID "com.blues.sense_hat_sample"

#define UPDATE_PERIOD (15000) // (1000 * 60 * 5)
static unsigned long lastUpdateMs;

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
  req = notecard.newRequest("card.aux");
  if (req != NULL)
  {
    JAddStringToObject(req, "mode", "neo-monitor");
    notecard.sendRequest(req);
  }

  // Stream Data from the Notecard Accelerometer
  req = notecard.newRequest("card.aux.serial");
  if (req != NULL) {
    JAddStringToObject(req, "mode", "notify,accel");
    JAddNumberToObject(req, "duration", UPDATE_PERIOD);
    notecard.sendRequest(req);
  }

  lastUpdateMs = millis();
}

void loop()
{
  const uint32_t currentMillis = millis();

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
