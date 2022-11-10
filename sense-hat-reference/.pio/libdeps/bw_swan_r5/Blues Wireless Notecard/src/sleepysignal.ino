#include <Notecard.h>

#define serialDebug Serial

Notecard notecard;

void setup() {
  serialDebug.begin(115200);
  while (!serialDebug);
  Serial1.begin(115200);

  notecard.begin();
  notecard.setDebugOutputStream(serialDebug);

  {
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", "com.email.you:project");
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    if (!notecard.sendRequest(req)) {
      notecard.logDebug("hub.set failed\n");
    }
  }

  notecard.logDebug("\nSetup complete\n");

}

void loop() {
  {
    J *req = notecard.newRequest("hub.signal");
    J *resp = notecard.requestAndResponse(req);
    J *body = JGetObject(resp, "body");
    char *json_string = JConvertToJSONString(body);
    JDelete(resp);
    if (json_string) {
      notecard.logDebugf("Signal: %s\n", json_string);
      free(json_string);
      // Dequeue all Signals as fast as possible
      continue;
    }
  }
  notecard.logDebug("No signal detected\n");

  {
    J *req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "arm,signal");
    // This should NEVER return
    if (!notecard.sendRequest(req)) {
        //if resp.err == "" && !resp.Set then a signal has arrived
        //continue
    }
  }

  // Hardware error prevented sleeping
  notecard.logDebug("card.attn failed\n");
  delay(1000);
}
