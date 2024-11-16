#include <Arduino.h>
#include <WiFi.h>   
#include <Firebase_ESP_Client.h>
#include <DHT.h>  
#include <ESP32Servo.h>
#include <HTTPClient.h>
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

#define DHT_SENSOR_PIN 4 // Pin connected to DHT sensor
#define DHT_SENSOR_TYPE DHT22 // Defining sensor type
#define GAS_SENSOR_PIN 17  // Pin connected to the gas sensor 
#define BUZZER_PIN 12      // Pin connected to the active buzzer

// Thresholds
#define TEMP_THRESHOLD 35.0      // Temperature threshold in Celsius
#define GAS_THRESHOLD 200        // Threshold for gas level 

// Warm-up period for gas sensor (e.g., 1 minute = 60000 milliseconds)
#define WARMUP_DURATION 60000

// Calibration offsets (adjust based on your calibration data) 
#define TEMP_CALIBRATION_OFFSET -1.0   // offset by 1 degree celcius
#define HUMIDITY_CALIBRATION_OFFSET 2.0 // offset by 2% 

// Calibration parameters for MQ2 sensor
#define RL 10000 // Load resistance in ohms (10kΩ)
#define V_SUPPLY 5.0 // Supply voltage to the sensor (5V for MQ-series)

// R0 value in clean air, needs calibration
#define R0 10.0 

// // Define your bot token and chat ID (Tried to use for telegram)
// #define TELEGRAM_BOT_TOKEN "8059348037:AAEoLB_hPj7F7hxB9p5v5fikGLu70uC6pjA"
// #define TELEGRAM_CHAT_ID "1675067152"

//To provide the ESP32 with the connection and the sensor type
DHT dht_sensor(DHT_SENSOR_PIN, DHT_SENSOR_TYPE);
Servo windowServo;  // Create servo object
int servoPin = 13;  // Pin for servo
int servoPos = 90;  // Initial servo position

// Store the start time for the warm-up period
unsigned long warmupStart;

// Insert your network credentials
#define WIFI_SSID "SSID" //replace with your own SSID
#define WIFI_PASSWORD "password" //replace with your own password

// Insert Firebase project API Key
#define API_KEY "AIzaSyD1J49WHDSi1bdKJIAQjSBH1b6Bw7r-gaQ"

// Insert RTDB URLefine the RTDB URL */
#define DATABASE_URL "https://ee4216-39ad7-default-rtdb.asia-southeast1.firebasedatabase.app/" 

//Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;                     //since we are doing an anonymous sign in 

void setup(){
  Serial.begin(115200);
  dht_sensor.begin();
  pinMode(GAS_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Ensure the buzzer is off at the start

  // Initialize servo
  windowServo.setPeriodHertz(50);   // Standard 50Hz servo
  windowServo.attach(servoPin);
  windowServo.write(servoPos); // Initial position (closed)

  // Connecting to wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  delay(2000);

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;  // this is the anonymous sign in
  }
  else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

float calculatePPM(int gasSensorValue) {
  // Convert analog reading to voltage
  float Vout = (gasSensorValue / 4095.0) * V_SUPPLY; // 12-bit ADC for ESP32
  
  // Calculate Rs
  float Rs = RL * (V_SUPPLY - Vout) / Vout;
  
  // Calculate the ratio Rs/R0
  float ratio = Rs / R0;

  // Convert ratio to PPM using sensitivity curve equation
  // Example equation for a specific gas (e.g., LPG for MQ-2):
  float ppm = pow(10, ((log10(ratio) - log10(4.4)) / (-0.42)));

  return ppm;
}

void loop(){

  // Read gas sensor value only if the warm-up period has passed
  int gasValue = 0;
  bool gasCheckReady = (millis() - warmupStart) > WARMUP_DURATION;

  //temperature and humidity measured should be stored in variables so the user
  //can use it later in the database

  // Calibration
  // Read temperature and humidity
  float rawTemperature = dht_sensor.readTemperature(); 
  float rawHumidity = dht_sensor.readHumidity(); 

  // Apply calibration
  float temperature = rawTemperature + TEMP_CALIBRATION_OFFSET;
  float humidity = rawHumidity + HUMIDITY_CALIBRATION_OFFSET;

  int gasAnalog = analogRead(GAS_SENSOR_PIN); // analog reading for gas sensor
  float gasValue = calculatePPM(gasValue); // conversion of analog reading
  
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 1000 || sendDataPrevMillis == 0)){
    //since we want the data to be updated every second
    sendDataPrevMillis = millis();
    
    // Enter Temperature in to the Table
    if (Firebase.RTDB.setInt(&fbdo, "ESP/Temperature", temperature)){
      // This command will be executed even if you dont serial print but we will make sure its working
      Serial.print("Temperature : ");
      Serial.println(temperature);
    }
    else {
      Serial.println("Failed to Read from the Sensor");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    
    // Enter Humidity in to the Table
    if (Firebase.RTDB.setFloat(&fbdo, "ESP/Humidity", humidity)){
      Serial.print("Humidity : ");
      Serial.println(humidity);
    }
    else {
      Serial.println("Failed to Read from the Sensor");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (gasCheckReady) {
      // Enter Gas in to the Table
      if (Firebase.RTDB.setInt(&fbdo, "ESP/Gas", gasValue)){
        // This command will be executed even if you dont serial print but we will make sure its working
        Serial.print("Gas : ");
        Serial.println(gasValue);
      }
      else {
        Serial.println("Failed to Read from the Sensor");
        Serial.println("REASON: " + fbdo.errorReason());
      }
    } else {
      Serial.println("Gas sensor warming up...");
    }

    // Check if thresholds are crossed (ignore gas threshold if not warmed up)
    if (temperature > TEMP_THRESHOLD || (gasCheckReady && gasValue > GAS_THRESHOLD)) {
      Serial.println("ALERT: Threshold crossed!");
      digitalWrite(BUZZER_PIN, HIGH);  // Sound buzzer if threshold is crossed
      servoPos = 0;
      windowServo.write(servoPos);  // Move servo to "open" position
      // Send Telegram alert
      // String alertMessage = "ALERT: Threshold crossed! \nTemperature: " + String(temperature) + "°C\nGas Level: " + String(gasValue);
      // sendTelegramMessage(alertMessage);
    } else {
      digitalWrite(BUZZER_PIN, LOW);   // Turn off buzzer if no thresholds are crossed
      servoPos = 90;
      windowServo.write(servoPos);  // Move servo to "closed" position
    }
  }
}

// function for sending to telegram
// void sendTelegramMessage(String message) {
//   if (WiFi.status() == WL_CONNECTED) {
//     HTTPClient http;
//     String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + message;
    
//     http.begin(url);
//     int httpResponseCode = http.GET();
//     if (httpResponseCode > 0) {
//       Serial.print("Telegram message sent successfully. Response code: ");
//       Serial.println(httpResponseCode);
//     } else {
//       Serial.print("Error sending Telegram message. Error code: ");
//       Serial.println(httpResponseCode);
//     }
//     http.end();
//   } else {
//     Serial.println("Not connected to Wi-Fi");
//   }
// }