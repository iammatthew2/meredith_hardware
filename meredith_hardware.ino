/*
  Meredith is a timed trigger management tool. The timed triggers fire https requests after the trigger has been
  activated longer than a given threadhold. The timed triggers can be overridden by long-pressing on the big button.

  This util is built with two included triggers (trigger_left and trigger_right). More triggers can be added by defining new
  TimedTriggerConfig structures
*/
#include <ArduinoJson.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"
#include "CuteBuzzerSounds.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
int status = WL_IDLE_STATUS;
char server[] = "meredith.vercel.app";

WiFiSSLClient client;

const int buzzer = 15;
const int button_health_check = 13;
const int led_big_button = 14;
const int big_button_long_press_limit = 5000;
const int default_trigger_alert_limit = 10000;
const int health_check_time_between = 3600000;
const int override_length = 60000;
const int common_loop_delay = 250;

// Struct to hold configuration for timed triggers
struct TimedTriggerConfig {
  int trigger;
  int led_success;
  int led_error;
  unsigned long trigger_press_start_time;
  int trigger_alert_limit;
};

// Configurations for existing triggers
TimedTriggerConfig trigger_left = {3, 19, 21, 0, default_trigger_alert_limit};
TimedTriggerConfig trigger_right = {4, 16, 20, 0, default_trigger_alert_limit};


unsigned long healthCheckLastSendTime = 0;
unsigned long bigButtonPushDuration = 0;
unsigned long triggerOverrideStartTime = 0;

bool shouldOverrideTimedTriggers = false;

// setup prototypes for functions with default params
void sendHealthCheck(bool override = false);

/**
 * Initialize with base setup
 */
void setup() {
  Serial.begin(9600);
  cute.init(buzzer);
  pinMode(button_health_check, INPUT); 
  pinMode(led_big_button, OUTPUT);

  setupTrigger(trigger_left);
  setupTrigger(trigger_right);

  connect_to_wifi();
}

/**
 * Handle setup for all timed triggers and paired LEDs
 * @param config - configuration for the timed trigger
 */
void setupTrigger(TimedTriggerConfig config) {
  pinMode(config.trigger, INPUT);
  pinMode(config.led_success, OUTPUT);
  pinMode(config.led_error, OUTPUT);
}

/**
 * Primary loop
 */
void loop() {
  handleBigButton();
  checkTrigger(trigger_left);
  checkTrigger(trigger_right);
  sendHealthCheck();
  manageTriggerOverrideTimer();

  delay(common_loop_delay);
}

void manageTriggerOverrideTimer() {
  if (shouldOverrideTimedTriggers && millis() - triggerOverrideStartTime > override_length) {
    triggerOverrideStartTime = 0;
    shouldOverrideTimedTriggers = false;
  }
  if (shouldOverrideTimedTriggers && millis() - triggerOverrideStartTime < override_length) {
    digitalWrite(trigger_left.led_success, HIGH);
    digitalWrite(trigger_right.led_success, HIGH);
    
    delay(common_loop_delay);
    
    digitalWrite(trigger_left.led_success, LOW);
    digitalWrite(trigger_right.led_success, LOW);
    delay(common_loop_delay);
  }
}

void handleBigButton() {
  if (digitalRead(button_health_check) == HIGH) {
    Serial.print("big button pushed and bigButtonPushDuration is: ");
    Serial.print(bigButtonPushDuration);
    Serial.print(". And shouldOverrideTimedTriggers is: ");
    Serial.println(shouldOverrideTimedTriggers);
    digitalWrite(led_big_button, HIGH);
    if (bigButtonPushDuration == 0) {
      bigButtonPushDuration = millis();
    }

    // if the button has been held down for the long press limit (or one second longer to account for other delays) and we've not yet overridden the timmedTriggers
    // then initiate the override
    if (millis() - bigButtonPushDuration > big_button_long_press_limit && !shouldOverrideTimedTriggers) {
      Serial.println("limit reached");
      cute.play(S_DISGRUNTLED);
      shouldOverrideTimedTriggers = true;
      triggerOverrideStartTime = millis();
      digitalWrite(led_big_button, LOW);
    }

    // if the button has been held down for the long press limit (or one second longer to account for other delays) and we've already overridden the timmedTriggers
    // then we need to kill the override. But we also need to make sure that the user has not just finished
    // trying to trigger the override 1/2 a second earlier
    if (millis() - bigButtonPushDuration > big_button_long_press_limit && shouldOverrideTimedTriggers) {
      Serial.println("limit reached");
      cute.play(S_DISGRUNTLED);
      shouldOverrideTimedTriggers = false;
      triggerOverrideStartTime = 0;
      digitalWrite(led_big_button, LOW);
    }
  }

  // if the button is not being pressed, but it was pressed
  if (digitalRead(button_health_check) == LOW && bigButtonPushDuration > 0) {
    Serial.print("big red button is LOW and bigButtonPushDuration is: ");
    Serial.print(bigButtonPushDuration);
    Serial.print(". And big_button_long_press_limit is: ");
    Serial.println(big_button_long_press_limit);
    // only send the health check if we're not long-pressing the button for the override request
    if ((millis() - bigButtonPushDuration) < big_button_long_press_limit) {
      Serial.println("33");
      sendHealthCheck(true);
    }
    digitalWrite(led_big_button, LOW);
    bigButtonPushDuration = 0;
  }
}

void sendHealthCheck(bool override) {
  if (override || (millis() - healthCheckLastSendTime > health_check_time_between)) {
    Serial.print("health check fired and override was: ");
    Serial.println(override);
    
    cute.play(S_CONNECTION);

    digitalWrite(trigger_left.led_success, HIGH);
    digitalWrite(trigger_right.led_success, HIGH);

    send_http_request();
    
    digitalWrite(trigger_left.led_success, LOW);
    digitalWrite(trigger_right.led_success, LOW);
    
    healthCheckLastSendTime = millis();
  }
}

/**
 * Check the state of a timed trigger
 * 
 * Note the use of "&" (reference specifier) in the function argument. This means we have a
 * copy by reference so operations on the config will update the global structure, not just a clone copied here
 * @param config - configuration for the timed trigger.
 */
void checkTrigger(TimedTriggerConfig& config) {
  if (digitalRead(config.trigger) == HIGH) {
    if (config.trigger_press_start_time == 0) {
      config.trigger_press_start_time = millis();
    }
    if (millis() - config.trigger_press_start_time > config.trigger_alert_limit) {
      digitalWrite(config.led_success, HIGH);
      digitalWrite(config.led_error, HIGH);
      cute.play(S_CONNECTION);
      delay(2000); // simulate an http request delay
    }
  } else {
    // Trigger is not pressed, reset the start time
    config.trigger_press_start_time = 0;
  }
}

void connect_to_wifi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    cute.play(S_DISCONNECTION);
    cute.play(S_DISGRUNTLED);
    cute.play(S_DISGRUNTLED);
    while (true);
  }

  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(10000);
  }

  Serial.println("Connected to WiFi");
  cute.play(S_CONNECTION);
  delay(250);
  cute.play(S_DISCONNECTION);
  delay(250);
  cute.play(S_CONNECTION);
  print_wifi_status();
}

void print_wifi_status() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  long rssi = WiFi.RSSI();
  Serial.print("Signal Strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void send_http_request() {
  while (client.available()) {
    char c = client.read();
    Serial.write(c);
  }
  client.stop();

  Serial.println("\nStarting connection to server...");
  if (client.connect(server, 443)) {
    cute.play(S_BUTTON_PUSHED);
    Serial.println("connected to server");

    client.println("GET /api/example?query=123 HTTP/1.1");
    client.println("Host: meredith.vercel.app");
    client.println("Connection: close");
    client.println("Content-Type: application/json");
  
    if (client.println() == 0) {
      Serial.println(F("Failed to send request"));
      cute.play(S_CONFUSED);
      return;
    }

    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
      Serial.print(F("Unexpected response: "));
      Serial.println(status);
      cute.play(S_DISGRUNTLED);
      return;
    }

    char end_of_headers[] = "\r\n\r\n";
    if (!client.find(end_of_headers)) {
      Serial.println(F("Invalid response1"));
      cute.play(S_DISGRUNTLED);
      return;
    }

    char end_of_headers2[] = "\r";
    if (!client.find(end_of_headers2)) {
      Serial.println(F("Invalid response2"));
      cute.play(S_DISGRUNTLED);
      return;
    }

    const size_t capacity = JSON_OBJECT_SIZE(12) + 170;
    StaticJsonDocument<capacity> doc;

    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      cute.play(S_DISGRUNTLED);
      return;
    }

    Serial.println("no errors. Assume success");
    cute.play(S_MODE1);
    cute.play(S_CONNECTION);
    cute.play(S_MODE1);
  } else {
    Serial.println("connection failed");
    cute.play(S_DISGRUNTLED);
    cute.play(S_DISGRUNTLED);
  }
}
