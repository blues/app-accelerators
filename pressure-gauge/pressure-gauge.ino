#include "OLED_FeatherWing.h"

#define DEBUG 0

float calculatePsi (size_t analog_pressure);
void displayResults (size_t sample_data);

float calculatePsi (size_t analog_pressure_) {

    #define ANALOG_MAX_PRESSURE 921
    #define ANALOG_MIN_PRESSURE 102
    #define ANALOG_VALUE_RANGE (ANALOG_MAX_PRESSURE - ANALOG_MIN_PRESSURE)

    /*
    |  V   | ADC | PSI |
    |:----:|:---:|:---:|
    | 0V33 | 102 |   0 |
    | 1V65 | 512 |  50 |
    | 2V97 | 921 | 100 |
    */

    float psi;

    // Calculate Bounded PSI (0-100)
    if (analog_pressure_ < ANALOG_MIN_PRESSURE) {
        psi = 0;
    } else if (analog_pressure_ > ANALOG_MAX_PRESSURE) {
        psi = 100.1; // Unique result to indicate out of bounds value
    } else {
        const size_t adj_analog_pressure = (analog_pressure_ - ANALOG_MIN_PRESSURE);
        psi = (static_cast<float>(adj_analog_pressure * 100) / ANALOG_VALUE_RANGE);
    }

    return psi;
}

void displayResults (size_t analog_sample_) {
  // Clear Current Display
  resetDisplay();

  // Generate Information to Display
  display.setTextSize(1);
  display.println("Pressure:");
  display.setTextSize(2);
  display.print(calculatePsi(analog_sample_));
  display.println(" PSI");
#if DEBUG
  display.setTextSize(1);
  display.print("sample data: ");
  display.println(analog_sample_);
#endif

  // Render on Screen
  display.display();
}

  /**********/
 /* SKETCH */
/**********/

void setup() {
  // Initialize OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
  display.setTextColor(SSD1306_WHITE);

  // Display Blues Splash Screen
  displayBluesSplash(1500);

  // Configure OLED Buttons
  ::pinMode(OFW_BUTTON_A, INPUT_PULLUP);
  ::pinMode(OFW_BUTTON_B, INPUT_PULLUP);
  ::pinMode(OFW_BUTTON_C, INPUT_PULLUP);
}

void loop() {
  // Sample Sensor
  size_t analog_sample = ::analogRead(A5);

  // Display Results
  displayResults(analog_sample);
    
  J *req = NoteNewRequest("note.add");
  JAddStringToObject(req, "file", "pressure.qo");
  JAddBoolToObject(req, "sync", true);

  J *body = JCreateObject();
  JAddNumberToObject(body, "psi", calculatePsi(analog_sample_));
  JAddItemToObject(req, "body", body);

  NoteRequest(req);

  // Slow Screen Refresh
  // to Facilitate Reading
  ::delay(500);
  ::yield();
}
