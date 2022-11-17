#include <Arduino.h>
#include <Notecard.h>

#define serialDebug Serial
#define productUID "com.blues.valve_monitor_nf9"

Notecard notecard;

void setup() {
  while (!serialDebug)
  serialDebug.begin(115200);

  notecard.begin();
  notecard.setDebugOutputStream(serialDebug);

  J *req = notecard.newRequest("hub.set");
  JAddStringToObject(req, "product", productUID);
  JAddStringToObject(req, "mode", "continuous");
  notecard.sendRequest(req);
}

void loop() {
  static const float FLOW_RATE_FLOOR = 45;
  const float RANDOM_OFFSET = ((rand() % 50000) / 10000.0f);

  J *req = notecard.newRequest("note.add");
  if (req) {
    JAddStringToObject(req, "file", "data.qo");
    J *body = JCreateObject();
    if (body) {
        JAddNumberToObject(body, "flow_rate", FLOW_RATE_FLOOR + RANDOM_OFFSET);
        JAddStringToObject(body, "valve_state", (rand() % 100) > 50 ? "open" : "closed");
        JAddStringToObject(body, "app", "nf9");
        JAddItemToObject(req, "body", body);
    }
    notecard.sendRequest(req);
  }
  
  // Send a mock reading every 2 minutes for now
  delay(1000 * 60 * 2);
}