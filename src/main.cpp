#include <string.h>

#include <Arduino.h>

#include <WiFi.h>
#include <HttpClient.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <DHT20.h>

#include <TFT_eSPI.h>

// Program Constants
#define WIFI_CONNECTION_ENABLED 1
#define MY_SERIAL_PORT 115200
#define true 1
#define false 0

// State Variables
#define MOLD_HUMIDITY_LEVEL 70
#define MOLD_TEMPERTURE_LEVEL 25
#define SHOWER_BEGIN_HUMIDITY_THRESHOLD 60
#define SHOWER_STOP_HUMIDITY_THRESHOLD 53
#define SHOWER_STATE 1
#define IDLE_STATE 0
#define BUZZING_HIGH 2
#define BUZZING_LOW 1
#define NOT_BUZZING 0

// Wifi Variables
#define K_NETWORK_DELAY 1000
#define K_NETWORK_TIMEOUT 30*1000
#define SSID "Alexis Jr" //"Error404"
#define PASS "poopoohead" //"f58jWS4NE"
#define K_PORT 5000
#define F_STRING "/?h=%d&t=%d"

// Buzzer Operation Constants
#define BUZZER_PIN 17
#define FREQUENCY_ON 1000
#define FREQUENCY_OFF 0
#define CHANNEL 0
#define RESOLUTION 8
#define BUZZER_CYCLE 1000*20
#define BUZZER_PERIOD 1000

// Buzzer Noise Alternative
#define DEBUG0 2
#define DEBUG1 13

/** WiFi connection *****************************************************************
 * (1) from downloads run:
 * 
 * ssh -i "lab3_cs147.pem" ubuntu@ec2-50-18-39-245.us-west-1.compute.amazonaws.com
 * 
 * (2) from myproject:
 * 
 * . venv/bin/activate
 * export FLASK_APP=server
 * python3 -m flask run --host=0.0.0.0
 *
 * (3) access server through browser with this:
 * http://50.18.39.245:5000/?h=0&t=0
 * http://50.18.39.245:5000/vis/
 *
 * (4) recover data file with following command from downloads:
 * scp -i "lab3_cs147.pem" ubuntu@ec2-50-18-39-245.us-west-1.compute.amazonaws.com:~/myproject/data.txt ./dataEC2.txt
*/

Adafruit_AHTX0 aht;

TFT_eSPI TFT = TFT_eSPI();

#if WIFI_CONNECTION_ENABLED
const IPAddress kHostname = IPAddress(50, 18, 39, 245);
#endif

uint8_t sh_state = IDLE_STATE;
uint8_t buzzing = false;
uint8_t bz_buz = 0;
uint8_t bz_count = 0;
uint8_t bz_state = NOT_BUZZING; 
unsigned long bz_timer;
unsigned long bz_period;

int hum = 0;
int temp = 0;

void setup() {
  Serial.begin(MY_SERIAL_PORT);

  // Setup humidity sensor
  if (!aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");
    while (1) delay(10);
  }
  Serial.println("AHT10 or AHT20 found");

  // Setup WiFi connection
  #if WIFI_CONNECTION_ENABLED
  delay(1000);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("MAC address: ");
  Serial.println(WiFi.macAddress());
  #endif

  // Setup display
  Serial.println("Connecting display ...");
  TFT.init();
  TFT.setRotation(1);
  TFT.fillScreen(TFT_BLACK);
  Serial.println("Connected to display!");

  // Setup buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  bz_timer = 0;
  bz_period = 0;
  buzzing = false;
  ledcSetup(CHANNEL, FREQUENCY_ON, RESOLUTION);
  ledcAttachPin(BUZZER_PIN, CHANNEL);
  ledcWriteTone(CHANNEL, FREQUENCY_OFF);

  // Setup debugger
  pinMode(DEBUG0, OUTPUT);
  digitalWrite(DEBUG0, LOW);
  pinMode(DEBUG1, OUTPUT);
  digitalWrite(DEBUG1, LOW);
}

#if WIFI_CONNECTION_ENABLED
void send_msg(int humidity, int temperature) {
  char sPath[15];
  sprintf(sPath, F_STRING, humidity, temperature);
  
  int err = 0;
  
  WiFiClient c;
  HttpClient http(c);
  
  err = http.get(kHostname, NULL, K_PORT, sPath);
  if (err == 0)
  {
    Serial.println("startedRequest ok");

    err = http.responseStatusCode();
    if (err >= 0)
    {
      Serial.print("Got status code: ");
      Serial.println(err);

      // Usually you'd check that the response code is 200 or a
      // similar "success" code (200-299) before carrying on,
      // but we'll print out whatever response we get

      err = http.skipResponseHeaders();
      if (err >= 0)
      {
        int bodyLen = http.contentLength();
        Serial.print("Content length is: ");
        Serial.println(bodyLen);
        Serial.println();
        Serial.println("Body returned follows:");
      
        // Now we've got to the body, so we can print it out
        unsigned long timeoutStart = millis();
        char c;
        // Whilst we haven't timed out & haven't reached the end of the body
        while ( (http.connected() || http.available()) &&
               ((millis() - timeoutStart) < K_NETWORK_TIMEOUT) )
        {
            if (http.available())
            {
                c = http.read();
                // Print out this character
                Serial.print(c);
               
                bodyLen--;
                // We read something, reset the timeout counter
                timeoutStart = millis();
            }
            else
            {
                // We haven't got any data, so let's pause to allow some to
                // arrive
                delay(K_NETWORK_DELAY);
            }
        }
        Serial.println("");
      }
      else
      {
        Serial.print("Failed to skip response headers: ");
        Serial.println(err);
      }
    }
    else
    {    
      Serial.print("Getting response failed: ");
      Serial.println(err);
    }
  }
  else
  {
    Serial.print("Connect failed: ");
    Serial.println(err);
  }
  http.stop();
}
#endif

void execute() {
  // sample sensors
  sensors_event_t humidity;
  sensors_event_t temperature;
  aht.getEvent(&humidity, &temperature);
  hum = humidity.relative_humidity;
  temp = temperature.temperature;

  // send sensor data
  #if WIFI_CONNECTION_ENABLED
  send_msg(hum, temp);
  #endif
}

void shower_state() {
  switch (sh_state)
  {
  case SHOWER_STATE:
    if (hum < SHOWER_STOP_HUMIDITY_THRESHOLD) {
      /* reset bz_count to prepare for next shower */
      sh_state = IDLE_STATE;
      bz_count = 0;
    }
    break;
  
  case IDLE_STATE:
    if (hum >= SHOWER_BEGIN_HUMIDITY_THRESHOLD) {
      /* we set bz_timer for 5 minutes into future since the shower has started */
      sh_state = SHOWER_STATE;
      bz_timer = millis() + BUZZER_CYCLE;
    }
    break;
  }
}

void display_action() {
  switch (sh_state)
  {
  case SHOWER_STATE:
    if (hum >= MOLD_HUMIDITY_LEVEL && temp >= MOLD_TEMPERTURE_LEVEL) {
      TFT.fillScreen(TFT_RED);
    } else {
      TFT.fillScreen(TFT_YELLOW);
    }
    break;
  
  case IDLE_STATE:
    TFT.fillScreen(TFT_BLACK);
    break;
  }
  TFT.drawNumber(hum, 90, 10, 7);
  TFT.drawNumber(temp, 90, 70, 7);
}

void buzzer_state() {
  switch (sh_state)
  {
  case SHOWER_STATE:
    /* initialize buzzing state if timer goes off and set new timer to avoid buzzer resets */
    if (millis() >= bz_timer) {
      bz_state = BUZZING_HIGH;
      bz_period = millis() + BUZZER_PERIOD;
      bz_count++;
      bz_buz = bz_count;
      bz_timer = millis() + BUZZER_CYCLE;
    }
    break;
  
  case IDLE_STATE:
    break;
  }
}

void buzzer_action() {
  switch (bz_state)
  {
  case BUZZING_HIGH:
    digitalWrite(DEBUG0, HIGH);
    digitalWrite(DEBUG1, LOW);
    ledcWriteTone(CHANNEL, FREQUENCY_ON);

    if (millis() >= bz_period) {
      bz_state = BUZZING_LOW;
      bz_period = millis() + BUZZER_PERIOD;
      bz_buz--;
    }
    break;

  case BUZZING_LOW:
    digitalWrite(DEBUG0, LOW);
    digitalWrite(DEBUG1, HIGH);
    ledcWriteTone(CHANNEL, FREQUENCY_OFF);

    if (bz_buz == 0) {
      bz_state = NOT_BUZZING;
    } else if (millis() >= bz_period) {
      bz_state = BUZZING_HIGH;
      bz_period = millis() + BUZZER_PERIOD;
    }
    break;
  
  case NOT_BUZZING:
    digitalWrite(DEBUG0, LOW);
    digitalWrite(DEBUG1, LOW);
    ledcWriteTone(CHANNEL, FREQUENCY_OFF);
    break;
  }
}

void loop() {
  execute();
  shower_state();
  display_action();
  buzzer_action();
  buzzer_state();
  
  // buffer for one second
  delay(1000);
}
