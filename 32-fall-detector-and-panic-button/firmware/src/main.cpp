#include <Arduino.h>
#include <Wire.h>
#include "SparkFun_BMA400_Arduino_Library.h"

// Create a new sensor object
BMA400 accelerometer;
// I2C address selection
uint8_t i2cAddress = BMA400_I2C_ADDRESS_DEFAULT; // 0x14
bool bmaInterruptOccurred = false;
int bmaInterruptPin = 13;
bool buttonInterruptOccurred = false;
int buttonInterruptPin = 12;

//variables to keep track of the timing of recent interrupts
unsigned long button_time = 0;  
unsigned long last_button_time = 0; 

#ifndef BMA400_INTERRUPT_DURATION_DEFAULT
#define BMA400_INTERRUPT_DURATION_DEFAULT 30
#endif

void bma400InterruptHandler()
{
    bmaInterruptOccurred = true;
}

void buttonInterruptHandler()
{
    button_time = millis();
    if (button_time - last_button_time > 250)   // Simple debounce 250ms
    {
      buttonInterruptOccurred = true;
      last_button_time = button_time;
    }
}
 

void initAccel(void)
{
    // Check if sensor is connected and initialize
    // Address is optional (defaults to 0x14)
    while(accelerometer.beginI2C(i2cAddress) != BMA400_OK)
    {
        // Not connected, inform user
        Serial.println("Error: BMA400 not connected, check wiring and I2C address!");

        // Wait a bit to see if connection is established
        delay(1000);
    }

    Serial.println("BMA400 connected!");
}

void setAccelParams(void)
{
    accelerometer.setODR(BMA400_ODR_200HZ);
    accelerometer.setRange(BMA400_RANGE_4G);
    accelerometer.setDataSource(BMA400_DATA_SRC_ACCEL_FILT_1);
}

void setInterruptParams(void)
{
    int8_t ret = BMA400_OK;
    bma400_gen_int_conf intConfig = {

    // The threshold is in units of 8mg (milli g's). 63 * 8mg = 504 mg.
    .gen_int_thres = 63,
      // The duration is in units of 10ms. Using the default value of 30:
      //
      //     30 * 10ms = 300ms.
      //
      // The fall height (H) implied by this duration is:
      //
      //     H = 0.5 * g * t^2 = 0.5 * 9.81m/s^2 * (0.30s)^2 = 0.708m.
      //
      // The accerlometer needs to be in free fall for 300 ms (or 0.708 meters) to
      // trigger the interrupt.
      .gen_int_dur = BMA400_INTERRUPT_DURATION_DEFAULT,
      // Use all three axes.
      .axes_sel = BMA400_AXIS_XYZ_EN,
      // From the datasheet: "Using acc_filt2 is recommended."
      .data_src = BMA400_DATA_SRC_ACCEL_FILT_2,
      // An accelerometer will read ~0 acceleration on all axes when in free fall.
      // Thus, we want to configure our interrupt threshold range to be a small
      // interval around 0. Setting the interrupt criterion to inactivity means
      // the interrupt condition is acceleration < threshold, rather than
      // > threshold. With an inactivity threshold of 504mg on all axes, the
      // interrupt condition is satisfied when all three acceleration values are
      // in the range (-504mg, 504mg). If that condition persists for >= 300ms,
      // the interrupt line will pulse.
      .criterion_sel = BMA400_INACTIVITY_INT,
            // The acceleration readings on all 3 axes must be below the threshold to
      // cause an interrupt.
      .evaluate_axes = BMA400_ALL_AXES_INT,
      // Reference acceleration values are manually set once and never updated.
      .ref_update = BMA400_UPDATE_MANUAL,
      // No hysteresis.
      .hysteresis = BMA400_HYST_0_MG,

      // All acceleration values are referenced with respect to 0. Practically,
      // this means the values will be compared directly with the threshold,
      // rather than being subtracted from some reference value.
      .int_thres_ref_x = 0,
      .int_thres_ref_y = 0,
      .int_thres_ref_z = 0,

      // There are two generic interrupts. We're using number 1.
      .int_chan = BMA400_INT_CHANNEL_1
    };

    accelerometer.setGeneric1Interrupt(&intConfig);

    // Here we configure the INT1 pin to push/pull mode, active high
    accelerometer.setInterruptPinMode(BMA400_INT_CHANNEL_1, BMA400_INT_PUSH_PULL_ACTIVE_1);
    
    // Enable generic 1 interrupt condition
    accelerometer.enableInterrupt(BMA400_GEN1_INT_EN, true);

    attachInterrupt(digitalPinToInterrupt(bmaInterruptPin), bma400InterruptHandler, RISING);

}

void setup() {
  Serial.begin(115200);
  Serial.println("Setup Start");
  Wire.begin();
  initAccel();
  setAccelParams();
  setInterruptParams();

  // Add Panic Button
  pinMode(buttonInterruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(buttonInterruptPin), buttonInterruptHandler, FALLING);

  Serial.println("Setup End");

}

void loop() {
  // put your main code here, to run repeatedly:
      // Wait for interrupt to occur
    if(bmaInterruptOccurred)
    {
        // Reset flag for next interrupt
        bmaInterruptOccurred = false;

        Serial.print("Interrupt occurred!");
        Serial.print("\t");

        // Get the interrupt status to know which condition triggered
        uint16_t interruptStatus = 0;
        accelerometer.getInterruptStatus(&interruptStatus);
        
        // Check if this is the generic 1 interrupt condition
        if(interruptStatus & BMA400_ASSERTED_GEN1_INT)
        {
            Serial.println("Fall detected!");
        }
    }
    if(buttonInterruptOccurred){
        // Reset flag for next interrupt
        buttonInterruptOccurred = false;
        Serial.println("Panic detected!");
    }
    delay(50);
}
