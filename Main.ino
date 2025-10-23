#include "esp_camera.h"
#include <WiFi.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// Pin assignments 
int Lock = 4;      
int Red = 13;
int Green = 12;
int buzzer = 15;
#define pbutton 2

extern void load_face_id_list();

const char *ssid = "***************";    // Your Wi-Fi SSID
const char *password = "*****************";  // Your Wi-Fi password

// Telegram Bot Credentials
const char *BOT_TOKEN = "**************************"; //Your Telegram Bot Token
const char *CHAT_ID = "******************";  // Your Telegram Bot Chat ID

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

volatile bool sendTelegramFlag = false;
char telegramMessage[128];
unsigned long bot_lasttime = 0;
const unsigned long BOT_MTBS = 200;

volatile bool isRecognizingFace = false;
boolean activateLock = false;
volatile bool activebuzzer = false;
long prevMillis = 0;
int interval = 3000;
long buzzerStart = 0;
int buzzerDuration = 1000;

static unsigned long lastPress = 0;

void startCameraServer();

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    Serial.println("Message from chat_id: " + chat_id);
    Serial.println("Text: " + text);

    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    if (text == "/start") {
      const char *welcome = "Welcome! Available commands:\n/unlock - Unlock the door\n";
      bot.sendMessage(CHAT_ID, welcome, "");
    }

    if (text == "/unlock") {
      activateLock = true;
      prevMillis = millis();
      digitalWrite(Lock, HIGH);
      digitalWrite(Green, HIGH);
      digitalWrite(Red, LOW);
      bot.sendMessage(CHAT_ID, "Unlocking door remotely...", "");
      Serial.println("Door unlocked remotely via Telegram.");
    }
  }
}

void setup() {
  pinMode(Lock, OUTPUT);
  pinMode(Red, OUTPUT);
  pinMode(Green, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(pbutton, INPUT_PULLUP);

  digitalWrite(Lock, LOW);
  digitalWrite(Red, HIGH);
  digitalWrite(Green, LOW);
  digitalWrite(buzzer, LOW);

  Serial.begin(115200);
  Serial.println();

  // --- SPIFFS FIRST ---
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }

  // --- CAMERA CONFIG ---
  camera_config_t cam_config;
  cam_config.ledc_channel = LEDC_CHANNEL_0;
  cam_config.ledc_timer = LEDC_TIMER_0;
  cam_config.pin_d0 = Y2_GPIO_NUM;
  cam_config.pin_d1 = Y3_GPIO_NUM;
  cam_config.pin_d2 = Y4_GPIO_NUM;
  cam_config.pin_d3 = Y5_GPIO_NUM;
  cam_config.pin_d4 = Y6_GPIO_NUM;
  cam_config.pin_d5 = Y7_GPIO_NUM;
  cam_config.pin_d6 = Y8_GPIO_NUM;
  cam_config.pin_d7 = Y9_GPIO_NUM;
  cam_config.pin_xclk = XCLK_GPIO_NUM;
  cam_config.pin_pclk = PCLK_GPIO_NUM;
  cam_config.pin_vsync = VSYNC_GPIO_NUM;
  cam_config.pin_href = HREF_GPIO_NUM;
  cam_config.pin_sscb_sda = SIOD_GPIO_NUM;
  cam_config.pin_sscb_scl = SIOC_GPIO_NUM;
  cam_config.pin_pwdn = PWDN_GPIO_NUM;
  cam_config.pin_reset = RESET_GPIO_NUM;
  cam_config.xclk_freq_hz = 20000000;
  cam_config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    cam_config.frame_size = FRAMESIZE_QVGA;
    cam_config.jpeg_quality = 10;
    cam_config.fb_count = 1;
  } else {
    cam_config.frame_size = FRAMESIZE_QVGA;
    cam_config.jpeg_quality = 12;
    cam_config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&cam_config);
    if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 0);
    s->set_saturation(s, 0);
  }
  s->set_framesize(s, FRAMESIZE_QVGA);

  // --- WIFI ---
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  WiFi.setSleep(false);

  // --- NTP ---
  Serial.println("Syncing time with NTP server...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long start = millis();
  while (now < 24 * 3600 && (millis() - start) < 8000) {
    delay(100);
    now = time(nullptr);
  }
  if (now >= 24 * 3600) {
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));
  } else {
    Serial.println("NTP time not acquired (continuing).");
 
  }


  startCameraServer();
  Serial.printf("[DEBUG] Heap after startCameraServer: %d\n", ESP.getFreeHeap());
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  delay(200);


  char readyMessage[128];
  snprintf(readyMessage, sizeof(readyMessage), "Camera Ready! Use http://%s", WiFi.localIP().toString().c_str());
  bot.sendMessage(CHAT_ID, readyMessage, "");
}

void loop() {
  // Manual unlock button

  if (digitalRead(pbutton) == LOW && (millis() - lastPress > 200)) {
    lastPress = millis(); 
    if (!activateLock) {  
      activateLock = true;
      prevMillis = millis();
      digitalWrite(Lock, HIGH);
      digitalWrite(Green, HIGH);
      digitalWrite(Red, LOW);
      Serial.println("Door unlocked manually.");
      strcpy(telegramMessage, "Door unlocked manually.");
      sendTelegramFlag = true;
  }
}

  // Buzzer timeout
  if (activebuzzer && (millis() - buzzerStart > buzzerDuration)) {
    digitalWrite(buzzer, LOW);
    activebuzzer = false;
  }
  // Relay timeout
  if (activateLock && millis() - prevMillis > interval) {
    activateLock = false;
    digitalWrite(Lock, LOW);
    digitalWrite(Green, LOW);
    digitalWrite(Red, HIGH);
  }

  // Telegram polling 
  if (!isRecognizingFace && (millis() - bot_lasttime > BOT_MTBS)) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    bot_lasttime = millis();
  }
}
