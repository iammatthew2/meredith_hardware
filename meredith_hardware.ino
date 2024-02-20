/*
  Meredith is a timed trigger management tool. The timed triggers fire https requests after the trigger has been
  activated longer than a given threadhold. The timed triggers can be put into a sleep mode by long-pressing on the big button.

  This util is built with two included triggers (trigger_left and trigger_right). More triggers can be added by defining new
  TimedTriggerConfig structures
*/
#include <ArduinoJson.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"
#include "CuteBuzzerSounds.h"

int status = WL_IDLE_STATUS;

WiFiSSLClient client;

const int buzzer = 15;
const int button_health_check = 13;
const int led_big_button = 14;
const int big_button_long_press_limit = 5000;
const int default_trigger_alert_limit = 5000;
const int default_retrigger_alert_limit = 9000;
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
  int primaryAlertId;
  bool isAlerting;
  unsigned long last_alert_send_time;
  int retrigger_alert_limit;
};

// Configurations for existing triggers
TimedTriggerConfig trigger_left = {3, 19, 21, 0, default_trigger_alert_limit, 1, false, 0, default_retrigger_alert_limit};
TimedTriggerConfig trigger_right = {4, 16, 20, 0, default_trigger_alert_limit, 2, false, 0, default_retrigger_alert_limit};

unsigned long healthCheckLastSendTime = 0;
unsigned long bigButtonPushDuration = 0;
unsigned long triggerOverrideStartTime = 0;

bool shouldOverrideTimedTriggers = false;

// setup prototypes for functions with default params
void sendHealthCheck(bool override = false);
void send_http_request(int primaryAlertId = 0, bool isResetingAlert = false);

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
  sendHealthCheck(true);

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

/**
 * Manage the ending of the timer and blinking the lights while timer is running
 */
void manageTriggerOverrideTimer() {
  // check if the override timer has reached its max. If so, end the override
  if (shouldOverrideTimedTriggers && millis() - triggerOverrideStartTime > override_length) {
    triggerOverrideStartTime = 0;
    shouldOverrideTimedTriggers = false;
    Serial.print("kill the override");
  }

  // if we are within the override timeframe blink the lights
  if (shouldOverrideTimedTriggers && millis() - triggerOverrideStartTime < override_length) {
    Serial.print("maintain the override");
    digitalWrite(trigger_left.led_success, HIGH);
    digitalWrite(trigger_right.led_success, HIGH);
    
    // use the common loop delay so our blinking looks nice and consistent
    delay(common_loop_delay);
    
    digitalWrite(trigger_left.led_success, LOW);
    digitalWrite(trigger_right.led_success, LOW);
    delay(common_loop_delay);
  }
}

/**
 * The big button triggers the health check for short presses and triggers the override with long presses
 */
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
      Serial.println("limit reached - first");
      cute.play(S_DISGRUNTLED);
      shouldOverrideTimedTriggers = true;
      triggerOverrideStartTime = millis();
      digitalWrite(led_big_button, LOW);
    }

    // if the button has been held down for the long press limit (or one second longer to account for other delays) and we've already overridden the timmedTriggers
    // then we need to kill the override. But we also need to make sure that the user has not just finished
    // trying to trigger the override 1/2 a second earlier
    if (millis() - bigButtonPushDuration > (big_button_long_press_limit + 3000) && shouldOverrideTimedTriggers) {
      Serial.println("limit reached - second");
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

/**
 * Manage the firing of the https health check
 * @param override=false - flag for forcing the triggering of the health check
 */
void sendHealthCheck(bool override) {
  if (override || (millis() - healthCheckLastSendTime > health_check_time_between)) {
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
    // newly detected trigger press
    if (config.trigger_press_start_time == 0) {
      config.trigger_press_start_time = millis();
    } else if (config.isAlerting) {
      digitalWrite(config.led_success, HIGH);
      digitalWrite(config.led_error, HIGH);
      delay(common_loop_delay);
      digitalWrite(config.led_success, LOW);
      digitalWrite(config.led_error, LOW);
      // send a new alert if retrigger_alert_limit time has passed since the previous message
      if (millis() - config.last_alert_send_time > config.retrigger_alert_limit) {
        Serial.println("retriggering alert");
        config.last_alert_send_time = millis();
        send_http_request(config.primaryAlertId);
      }
    } else if (millis() - config.trigger_press_start_time > config.trigger_alert_limit) {
      // time limit has passed and an initial alert needs to be triggered
      Serial.println("triggering initial alert");
      config.isAlerting = true;
      config.last_alert_send_time = millis();
      send_http_request(config.primaryAlertId);
    }
  } else if (config.isAlerting){
    // Trigger can cease alerting
    config.trigger_press_start_time = 0;
    config.last_alert_send_time = 0;
    config.isAlerting = false;
    send_http_request(config.primaryAlertId, true);
  }
}

/**
 * Beging and wait for the wifi connection to be completed
 */
void connect_to_wifi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module comm failure");

    while (true);
  }

  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to wifi network: ");
    Serial.println(SECRET_SSID);
    status = WiFi.begin(SECRET_SSID, SECRET_PASS);
    delay(10000);
  }
  print_wifi_status();
}

/**
 * Print out the wifi status
 */
void print_wifi_status() {
  Serial.println("Connected to WiFi");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print("IP: ");
  Serial.println(ip);

  long rssi = WiFi.RSSI();
  Serial.print("Signal strength: ");
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.println("Pausing to allow time for attaching monitor");
  delay(3000);
  Serial.println("Pause complete");
}

void send_http_request(int primaryAlertId, bool isResetingAlert) {
  Serial.println("\nStarting connection to server...");
  
  if (WL_IDLE_STATUS != WL_CONNECTED) {
    Serial.println("oh! Not connected. Try reconnect");
    connect_to_wifi();
  }

  if (status != WL_CONNECTED) {
    // trigger three S_DISGRUNTLED so we can notify about not-connected error
    // TODO prob need to add WIFI reconnect logic here - see https://forum.arduino.cc/t/arduino-wifi-rev2-reconnecting-to-wifi-after-disconnected/1022789
    cute.play(S_DISGRUNTLED);
    cute.play(S_DISGRUNTLED);
    cute.play(S_DISGRUNTLED);
    return;
  }

  if (client.connect(SECRET_SERVER, 443)) {
    cute.play(S_BUTTON_PUSHED);
    Serial.println("connected to server");

    client.print("GET ");
    client.print(SECRET_PATH_WITH_INTENT);
    if (!primaryAlertId) {
      client.print("health");
    } else {
      client.print("primary");
      client.print("&primaryAlertId=");
      client.print(primaryAlertId);
      if (isResetingAlert) {
        client.print("&isResetingAlert=true");
      }
    }

    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(SECRET_HOST_NAME);
    client.println("Connection: close");
  
    if (client.println() == 0) {
      Serial.println(F("Failed to send request"));
      cute.play(S_CONFUSED);
      return;
    }

    char reqStatus[32] = {0};
    client.readBytesUntil('\r', reqStatus, sizeof(reqStatus));

    // Check if the status starts with "HTTP/1.1"
    if (strncmp(reqStatus, "HTTP/1.1", 8) == 0) {
        // Extract the status code
        char* statusCodePtr = reqStatus + 9; // Skip "HTTP/1.1 "
        int statusCode = atoi(statusCodePtr);

        // Check if the status code is in the range of success codes (200-299)
        if (statusCode >= 200 && statusCode < 300) {
            // Success response
            // Continue with your code logic here
        } else {
            // Unexpected response
            Serial.print(F("Unexpected response: "));
            Serial.println(reqStatus);
            cute.play(S_DISGRUNTLED);
            return;
        }
    } else {
        // Response doesn't start with "HTTP/1.1"
        Serial.print(F("Invalid HTTP response: "));
        Serial.println(reqStatus);
        cute.play(S_DISGRUNTLED);
        return;
    }

    char end_of_headers[] = "\r\n\r\n";
    if (!client.find(end_of_headers)) {
      Serial.println(F("Invalid response1"));
      cute.play(S_DISGRUNTLED);
      return;
    }

    // Happy path. Print the entire server response, including headers and body
    Serial.println("Server Response:");
    while (client.available()) {
      char c = client.read();
      Serial.write(c);
    }

    cute.play(S_MODE1);
    cute.play(S_CONNECTION);
    cute.play(S_MODE1);
  } else {
    Serial.print("Connection failed for unknown reason. Debugging needed. WL_IDLE_STATUS: ");
    Serial.println(WL_IDLE_STATUS);
    // trigger two S_DISGRUNTLED so we can notify about the unknown error
    cute.play(S_DISGRUNTLED);
    cute.play(S_DISGRUNTLED);
  }

  client.stop();
}
