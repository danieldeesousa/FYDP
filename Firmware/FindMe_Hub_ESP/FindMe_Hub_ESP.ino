// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>
// https://github.com/mobizt/Firebase-ESP8266/tree/branch-1
// FirebaseESP8266.h must be included before ESP8266WiFi.h
#include "FirebaseESP8266.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

// Disable/enable printlns
//#define Sprintln(a) (Serial.println(a)) // uncomment to ENABLE printlns
#define Sprintln(a) // uncomment to DISABLE printlns
//#define Sprintf(...) (Serial.printf(...)) // uncomment to ENABLE printf
#define Sprintf(...) void(0) // uncomment to DISABLE printf

// Application Variables
#define LED_PIN (0)
static char input[40];
static int idx = 0;
bool forcedRequestFlag = 0; // set when server has requested data
bool forcedRequestReject = 0; // set when we just replied to forced request but flag has not yet been updated // a temp fix to double FRCD-RQST
const char* kSSID = "Bosvark";
const char* kPassword = "77368272BG";

const String kFirebaseFunctions = "https://us-central1-fyndme-420a9.cloudfunctions.net/app";
const char* kFirebaseHost = "https://fyndme-420a9.firebaseio.com";
const char* kFirebaseAuth = "8WEoLSk7KMpyNOtYBa6Pv72XffWjorDcf1ObU7eg";

enum Status { ERROR_CODE, SUCCESS_CODE };

struct StatusResponse {
  Status status_code;
  String response;
};

class ServerRequestService {
 private:
  // Helper function to send post request
  // end_point is where to send the post request
  // post_body is what to put in the post request
  static StatusResponse post(const String& end_point, const String& post_body) {
    StatusResponse status_response = {SUCCESS_CODE, ""};

    // We need to do this for https (setInsecure)
    BearSSL::WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, kFirebaseFunctions + end_point);
    http.addHeader("Authorization", String("Bearer ") + access_token_);
    http.addHeader("Content-Type", "application/json");

    Sprintf("[HTTP] post_body: %s\n", post_body.c_str());
    Sprintf("WiFi status: %d %d\n", WiFi.status(), WL_CONNECTED);

    const int& http_code = http.POST(post_body);
    Sprintf("[HTTP] http_code: %d\n", http_code);

    // http_code will be negative on error
    if (http_code > 0) {
      // HTTP header has been send and Server response header has been handled
      if (http_code == HTTP_CODE_OK) {
        const String& payload = http.getString();
        status_response.response = payload;
        Sprintf("received payload: %s\n", payload.c_str());
      } else {
        status_response.status_code = ERROR_CODE;
      }
    } else {
      status_response.status_code = ERROR_CODE;
    }

    if (status_response.status_code == ERROR_CODE)
      Sprintln("[HTTP] failed");

    http.end();
    return status_response;
  }

  static Status requestAccessToken() {
    Sprintln("requestAccessToken");
    StatusResponse status_response =
        post("/getAccessToken",
             String("{\"refreshToken\":\"") + refresh_token_ + String("\"}"));

    if (status_response.status_code == ERROR_CODE) return ERROR_CODE;

    // 1500 is the capacity of the memory pool in bytes.
    DynamicJsonDocument doc(1500);
    DeserializationError error = deserializeJson(doc, status_response.response);

    // Test if parsing succeeds.
    if (error) {
      Sprintf("deserializeJson() failed: %s\n", error.c_str());
      return ERROR_CODE;
    }

    access_token_ = doc["access_token"].as<String>();
    long expires_in = doc["expires_in"];  // seconds
    long buffer_time = 600000;            // 10 minutes
    expiry_time_ = millis() + expires_in * 1000 - buffer_time;

    Sprintf("access_token_: %s\n", access_token_.c_str());
    Sprintf("expires_in: %d\n", expiry_time_);

    return SUCCESS_CODE;
  }

 public:
  // token from BLE in setup
  static String refresh_token_;

  // token from server
  static String access_token_;

  // when access token expires (milliseconds)
  static unsigned long expiry_time_;

  static Status getAccessToken() {
    Status status_code = SUCCESS_CODE;
    Sprintf("getAccessToken currentTime %d, expiry_time_ %d\n", millis(),
                  expiry_time_);
    if (millis() >= expiry_time_) {
      // refetch from server
      status_code = requestAccessToken();
      if (status_code == ERROR_CODE) {
        access_token_ = "";
        expiry_time_ = 0;
      }
    }
    return status_code;
  }

  // same as post except updates access token if needed
  static StatusResponse safePost(const String& end_point,
                                 const String& post_body) {
    if (getAccessToken() == ERROR_CODE) return {ERROR_CODE, ""};
    return post(end_point, post_body);
  }

  static StatusResponse getUID() {
    Sprintln("getUID");
    StatusResponse status_response = safePost("/user", "{}");

    if (status_response.status_code == ERROR_CODE) return status_response;

    // 1500 is the capacity of the memory pool in bytes.
    DynamicJsonDocument doc(1500);
    DeserializationError error = deserializeJson(doc, status_response.response);

    // Test if parsing succeeds.
    if (error) {
      status_response.status_code = ERROR_CODE;
      Sprintf("deserializeJson() failed: %s\n", error.c_str());
      return status_response;
    }

    status_response.response = doc["uid"].as<String>();
    return status_response;
  }

  static StatusResponse updateData(const String& location, const String& battery,
                                   const bool& alert) {
    Sprintln("updateData");
    return safePost("/updateData", "{\"location\": \"" + location +
                                       "\", \"battery\": \"" + battery +
                                       "\", \"alert\": \"" +
                                       (alert ? "true" : "false") + "\"}");
  }

  static StatusResponse endForceRequest() {
    Sprintln("endForceRequest");
    return safePost("/endForceRequest", "{}");
  }
};

String ServerRequestService::refresh_token_ =
    "AEu4IL1wMhvTkLv4YGTX8XxgTxdVw8fR8XEent1_Cq-"
    "Xkm4zYRgs56132CEGA7xu4F6nsJVUMlRV7M7N9-RH2tQeLn8_"
    "NN1WgVRMFDMUSlHqgyoby8W0b94PgWKBI1vTDiFYd8te7RPCD6dWaqkkqEqDy8H2Me_"
    "yhkmeJLi_ROyCehYppRnKBYM";
unsigned long ServerRequestService::expiry_time_ = 0;
String ServerRequestService::access_token_ = "";

// Define FirebaseESP8266 data object
FirebaseData firebase_data;
long timeouts = 0;

// Function which is called when database is updated
void streamCallback(StreamData data) 
{
  // Print response to serial
  const int& force_request = data.intData();
  Sprintf("streamCallback %d timeouts %d \n", data.intData(), timeouts);

  // Check if forced request is sent, if not, we do not care
  // don't want infinite loop so only request when needed
  if (force_request == 0 || forcedRequestReject)
  {
    forcedRequestReject = 0; // clear flag
    return;
  }

  // Send request to hub and set flag
  Serial.write("FRCD-RQST\n"); // Send a forced request to nRF
  forcedRequestFlag = true;
  testPulse();
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    timeouts++;
  }
}

void setup() 
{
  Serial.begin(9600);

  // Connect to Wi-Fi
  Sprintln(kSSID);
  WiFi.begin(kSSID, kPassword);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // Wait until Wi-Fi connected
  pinMode(LED_PIN, OUTPUT);   // Setup LED 
  bool ledState = false;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    digitalWrite(LED_PIN, ledState);
    ledState = !ledState;
  }
  digitalWrite(LED_PIN, LOW); // Note: LED is active LOW
  Sprintln("");
  Sprintln("WiFi connected");
  Sprintln("IP address: ");
  Sprintln(WiFi.localIP());

  // Clear nRF-ESP UART buffer
  Serial.write("\n");

  // Setup Firebase connection
  StatusResponse uid_response = ServerRequestService::getUID();
  if (uid_response.status_code == ERROR_CODE) return;
  const String& uid = uid_response.response;
  Sprintf("uid: %s\n", uid.c_str());

  Firebase.begin(kFirebaseHost, kFirebaseAuth);
  Firebase.reconnectWiFi(true);

  if (!Firebase.beginStream(firebase_data, String("/") + uid + String("/forceRequest"))) 
  {
    Sprintln("------------------------------------");
    Sprintln("Can't begin stream connection...");
    Sprintln("REASON: " + firebase_data.errorReason());
    Sprintln("------------------------------------");
    Sprintln();
  }

  // Setup Firebase callback - called whenever database is written to
  Firebase.setStreamCallback(firebase_data, streamCallback, streamTimeoutCallback);
}

void loop() 
{ 
  // Check if Wi-Fi is connected
  if(WiFi.status() != WL_CONNECTED)
  {
      bool ledState2 = false;
      // starting low -> off/on -> off/on -> low
      for (int i = 0; i < 4; i++) 
      {
        delay(200);
        digitalWrite(LED_PIN, ledState2);
        ledState2 = !ledState2;
      }
      digitalWrite(LED_PIN, LOW);
  }
  
  // Wait on Serial message
  while(Serial.available() > 0)
  {
    // append to array
    input[idx] = Serial.read();
    idx++;

    // check if end of message
    if(input[idx-1] == '\n')
    {
      input[idx-1] = '\0';

      // Parse message
      // example: +4347300-8053948 99 N
      // incoming packet should either be 20, 21, 22 chars long (battery percent can be 0-100)
      uint8_t dataSize = strlen(input);
      if(dataSize >= 20 && dataSize <= 22)
      {
        String data = input; // convert input to a string
        String location = data.substring(0,16);
        String battery = data.substring(17,dataSize-2); // 17->18,19,20
        bool alert = ((data.substring(dataSize-1, dataSize)) == "Y");
        ServerRequestService::updateData(location, battery, alert);
        
        if(forcedRequestFlag)
        {
           // clear forced request 
           forcedRequestFlag = 0;
           forcedRequestReject = 1;
           ServerRequestService::endForceRequest();
           testPulse();
        }
      }

      // reset array
      idx = 0;
      input[0] = '\0';
    }
  }
}

void testPulse()
{
  digitalWrite(0,1);
  delay(1000);
  digitalWrite(0,0);
}
