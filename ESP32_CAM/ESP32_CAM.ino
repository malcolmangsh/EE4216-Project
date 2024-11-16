#include <WiFi.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <esp_camera.h>

// Wi-Fi credentials
const char* ssid = "jhhotspot"; // Wi-Fi SSID
const char* password = "YOUR_WIFI_PASSWORD"; // Wi-Fi password

// Telegram bot credentials
String BOTtoken = "7623693354:AAEiTtDrNrLXw-AlzRU3WGM9bjzDv1v16Lk"; // Telegram bot token
String chat_id = "1675067152"; // Chat ID for Telegram messages

// Camera and motion detection pins
#define PIR_PIN 13 // Pin for PIR motion sensor
#define BUZZER_PIN 12 // Pin for Buzzer

// Global variables
WiFiClientSecure client; // Secure client for Telegram bot communication
UniversalTelegramBot bot(BOTtoken, client); // Telegram bot instance
camera_fb_t* fb = NULL; // Frame buffer for capturing camera images
bool motionDetected = false; // Flag for motion detection
bool picture_ready = false; // Flag to indicate picture capture is ready
bool buzzerActive = false; // Flag to track if buzzer is active
bool reboot_request = false; // Flag for reboot request
unsigned long buzzerStartTime = 0; // Start time for buzzer
unsigned long lastBuzzerToggleTime = 0; // Time of last buzzer toggle
bool buzzerState = false; // Buzzer state (on/off)

// Global Wi-Fi Manager instance
#include <WiFiManager.h>
WiFiManager wm; 


String devstr = "ESP32CAM"; // Device name
String timezone = "UTC"; // Time zone for device
bool hdcam = false; // Flag for HD camera mode
bool pir_enabled = true; // Flag for PIR sensor
bool avi_enabled = false; // Flag for AVI recording
bool tim_enabled = false; // Flag for time-lapse enabled

int max_frames = 10; // Max number of frames to capture
int frame_interval = 200; // Interval between frames
int speed_up_factor = 1; // Speed-up factor for capturing frames

int idx_buf_size = 0; // Index buffer size for storing frames
int avi_buf_size = 0; // AVI buffer size for video storage
framesize_t configframesize; // Camera frame size configuration
framesize_t framesize; // Frame size used for capturing

// Camera configuration
camera_config_t config = {
    .ledc_channel = LEDC_CHANNEL_0,
    .ledc_timer = LEDC_TIMER_0,
    .pin_d0 = 0,
    .pin_d1 = 1,
    .pin_d2 = 2,
    .pin_d3 = 3,
    .pin_d4 = 4,
    .pin_d5 = 5,
    .pin_d6 = 6,
    .pin_d7 = 7,
    .pin_xclk = 21,
    .pin_pclk = 22,
    .pin_vsync = 23,
    .pin_href = 24,
    .pin_sscb_sda = 26,
    .pin_sscb_scl = 27,
    .pin_pwdn = 32,
    .pin_reset = -1,
    .xclk_freq_hz = 20000000,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_SVGA,
    .jpeg_quality = 12,
    .fb_count = 1
};


unsigned long Bot_lasttime; // Last time the bot was used
int loopcount = 0; // Loop counter for execution


void setup() {
  Serial.begin(115200); // Initialize serial communication

  // Camera configuration
  camera_config_t config;

  // Initialize the camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    // Capture a frame
    camera_fb_t *fb = esp_camera_fb_get();  // Get the frame buffer
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    uint8_t *fb_buffer = fb->buf;  // Frame buffer pointer
    size_t fb_length = fb->len;     // Frame length
    int currentByte = 0;            // Byte tracker for the frame buffer

    // Example of processing the frame buffer (printing the first 10 bytes)
    for (int i = 0; i < 10; i++) {
        Serial.print(fb_buffer[currentByte + i], HEX);
        Serial.print(" ");
    }

    esp_camera_fb_return(fb);  // Free the frame buffer after processing
    

  // Set up Wi-Fi connection
  WiFi.begin(ssid, password); // Start Wi-Fi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to Wi-Fi...");
  }
  Serial.println("Connected to Wi-Fi!");

  // Configure PIR motion sensor and buzzer pins
  pinMode(PIR_PIN, INPUT); // Set PIR pin as input
  pinMode(BUZZER_PIN, OUTPUT); // Set buzzer pin as output
  digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is initially off

  Serial.println("Setup complete!");
}

void loop() {
  loopcount++; // Increment loop counter

  // Handle reboot request
  if (reboot_request) {
    String stat = "Rebooting on request\nDevice: ESP32-CAM\nVer: 1.0\nRSSI: " + String(WiFi.RSSI()) +
                  "\nIP: " + WiFi.localIP().toString();
    bot.sendMessage(chat_id, stat, ""); // Send reboot status to Telegram
    delay(10000); // Wait before reboot
    ESP.restart(); // Reboot the ESP32
  }

  // Send picture when ready
  if (picture_ready) {
    picture_ready = false; // Reset flag
    send_the_picture(); // Send the captured picture
  }

  // Check for new Telegram messages
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages) {
    handleNewMessages(numNewMessages); // Handle new messages
    numNewMessages = bot.getUpdates(bot.last_message_received + 1); // Get next batch of messages
  }

  Bot_lasttime = millis(); // Update last bot activity time

  // Motion detection handling
  int pirState = digitalRead(PIR_PIN); // Read PIR sensor state
  if (pirState == HIGH && !motionDetected) { // Motion detected
    motionDetected = true;

    // Capture image and send message on motion detection
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      String message = "Motion detected! Check the web server at: http://" + WiFi.localIP().toString() + "/capture";
      bot.sendMessage(chat_id, message, "Markdown"); // Send motion detected message
      esp_camera_fb_return(fb); // Return frame buffer
    } else {
      Serial.println("Camera capture failed on motion detection.");
    }

    // Activate buzzer
    buzzerActive = true;
    buzzerStartTime = millis(); // Set buzzer start time
    lastBuzzerToggleTime = millis(); // Initialize buzzer toggle time
    buzzerState = true;
    digitalWrite(BUZZER_PIN, HIGH); // Turn on buzzer
  }

  // Buzzer operation
  if (buzzerActive) {
    unsigned long currentTime = millis(); // Get current time

    // Toggle buzzer state every 500ms
    if (currentTime - lastBuzzerToggleTime >= 500) {
      buzzerState = !buzzerState; // Toggle buzzer state
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW); // Set buzzer state
      lastBuzzerToggleTime = currentTime; // Update last toggle time
    }

    // Turn off buzzer after 30 seconds
    if (currentTime - buzzerStartTime >= 30000) {
      buzzerActive = false; // Deactivate buzzer
      digitalWrite(BUZZER_PIN, LOW); // Turn off buzzer
    }
  }

  // Reset motion detection flag when no motion is detected
  if (pirState == LOW) {
    motionDetected = false;
  }
}

// Handle incoming Telegram messages
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String text = bot.messages[i].text; // Get the message text
    String from_name = bot.messages[i].from_name; // Get the sender's name
    if (from_name == "") from_name = "Guest"; // Default name if not available

    String response = "Got: " + text; // Prepare response
    bot.sendMessage(chat_id, response, "Markdown"); // Send response to Telegram

    // Handle photo capture command
    if (text == "/photo") {
      fb = esp_camera_fb_get(); // Capture a photo
      if (!fb) {
        Serial.println("Camera capture failed.");
        bot.sendMessage(chat_id, "Camera capture failed.", ""); // Send failure message
        return;
      }

      // Send photo to Telegram
      bot.sendPhotoByBinary(chat_id, "image/jpeg", fb->len, isMoreDataAvailable, getNextByte, nullptr, nullptr);
      esp_camera_fb_return(fb); // Return frame buffer
    }

    // Handle start command to show instructions
    if (text == "/start") {
      String welcome = "ESP32-CAM Telegram Bot\n\n";
      welcome += "/photo - Take a photo\n";
      bot.sendMessage(chat_id, welcome, "Markdown");
    }
  }
}

// Helper functions for sending binary photo data to Telegram
int currentByte; // Byte tracker for frame buffer
uint8_t* fb_buffer; // Frame buffer pointer
size_t fb_length; // Frame length

// Check if more data is available to send
bool isMoreDataAvailable() {
  return (fb_length - currentByte);
}

// Get next byte of data to send
uint8_t getNextByte() {
  currentByte++;
  return (fb_buffer[currentByte - 1]);
}
