#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BMP581.h"

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BMP581 bmp;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Adafruit BMP581 test");

  if (!bmp.begin_I2C())
  {
    Serial.println("Could not find a valid BMP581 sensor, check wiring!");
    while (1);
  }

  // Set up oversampling and filter initialization
  bmp.setTemperatureOversampling(BMP5_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP5_OVERSAMPLING_4X);
  bmp.setIIRFilterCoeff(BMP5_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP5_ODR_50_HZ);
}

void loop() {
  if (! bmp.performReading()) {
    Serial.println("Failed to perform reading :(");
    return;
  }
  Serial.print("Temperature = ");
  Serial.print(bmp.temperature);
  Serial.println(" *C");

  Serial.print("Pressure = ");
  Serial.print(bmp.pressure / 100.0);
  Serial.println(" hPa");

  Serial.print("Approx. Altitude = ");
  Serial.print(bmp.readAltitude(SEALEVELPRESSURE_HPA));
  Serial.println(" m\n");

  Serial.println();
  delay(2000);
}