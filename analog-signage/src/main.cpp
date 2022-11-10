#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "notecard_config.h"
#include "display_manager.h"

// WiFi network name and password:
const char * networkName = "5at70m_devices";
const char * networkPw = "Sarah0812!";

// Vestaboard URL and Key
//const char * serverName = "https://rw.vestaboard.com/";
//const char * vestaboardKey = "Yjg3NDgwMDAtYzJhYi00YWU5LWJkMGUtNmU5YTYzNTAxMjcx";
const char * serverName = "http://192.168.1.63:7000/local-api/message";
const char * vestaboardKey = "ZDVmZjk3NzItMDNlZS00MTIyLTlmODktZDU1MGRlMzNjZTNj";

const int LED_PIN = 5;

#define serialDebugOut Serial
// Uncomment to view Note requests from the Host
#define DEBUG_NOTECARD

#define ENV_POLL_SECS	1

Notecard notecard;

// Variables for Env Var polling
static unsigned long nextPollMs = 0;
static uint32_t lastModifiedTime = 0;

applicationState state;

// Forward declarations
void fetchEnvironmentVariables(applicationState);
bool pollEnvVars(void);
void connectToWiFi(const char *, const char *);
void clearBoard(void);
void setUpBoard(void);

void setup() {
  serialDebugOut.begin(115200);

  while (!serialDebugOut) { delay(10); }
  serialDebugOut.println("Analog Signage Demo");
  serialDebugOut.println("===================");

  connectToWiFi(networkName, networkPw);

  Wire.begin();
#ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
  notecard.begin();

  configureNotecard();

  // Check Environment Variables
  fetchEnvironmentVariables(state);

  //clearBoard();
  //delay(500);
  setUpBoard();
}

void loop() {
  // put your main code here, to run repeatedly:
}

void fetchEnvironmentVariables(applicationState vars) {
  J *req = notecard.newRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("text"));

  J *rsp = notecard.requestAndResponse(req);
  if (rsp != NULL) {
      if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return;
      }

      // Get the note's body
      J *body = JGetObject(rsp, "body");
      if (body != NULL) {
          char *text = JGetString(body, "text");
          if (strcmp(vars.text.c_str(), text) != 0)
          {
            vars.text = String(text);
            vars.variablesUpdated = true;
          }

          serialDebugOut.print("\nText: ");
          serialDebugOut.println(vars.text);

          state = vars;
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

void printLine()
{
  Serial.println();
  for (int i=0; i<30; i++)
    Serial.print("-");
  Serial.println();
}

void connectToWiFi(const char * ssid, const char * pwd)
{
  int ledState = 0;

  printLine();
  Serial.println("Connecting to WiFi network: " + String(ssid));

  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED)
  {
    // Blink LED while we're connecting:
    digitalWrite(LED_PIN, ledState);
    ledState = (ledState + 1) % 2; // Flip ledState
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void clearBoard()
{
  if(WiFi.status() == WL_CONNECTED)
  {
    WiFiClient client;
    HTTPClient http;

    // Your Domain name with URL path or IP address with path
    http.begin(client, serverName);

    // Specify content-type header
    http.addHeader("X-Vestaboard-Local-Api-Key", vestaboardKey);

    // Data to send with HTTP POST
    String httpRequestData = "[[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]]";
    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);

    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    http.end();
  } else
  {
    Serial.println("Wi-Fi Not Connected!");
  }
}

void setUpBoard() {
 if(WiFi.status() == WL_CONNECTED)
 {
    WiFiClient client;
    HTTPClient http;

    // Your Domain name with URL path or IP address with path
    http.begin(client, serverName);

    // Specify content-type header
    http.addHeader("X-Vestaboard-Local-Api-Key", vestaboardKey);

    // Data to send with HTTP POST
    String httpRequestData = "[[67,67,67,67,2,12,21,5,19,0,0,18,1,9,12,23,1,25,67,67,67,67],[0,0,3,9,20,25,0,0,20,18,1,3,11,0,0,20,9,13,5,0,0],[0,0,1,21,19,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,2,15,19,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,1,20,12,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,2,15,9,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]]";

    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);

    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    http.end();
  } else
  {
    Serial.println("Wi-Fi Not Connected!");
  }
}
