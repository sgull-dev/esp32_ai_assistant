// Include required libraries
#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include "FS.h"
#include "SPIFFS.h"
#include <driver/i2s.h>

// Define I2S connections for both output and input
#define I2S_DOUT  22
#define I2S_BCLK  33
#define I2S_LRC   32
#define MIC_WS 25
#define MIC_SCK 26
#define MIC_SD 27
#define I2S_PORT I2S_NUM_1

// Define buffer length and buffer
#define bufferLen 64
int16_t sBuffer[bufferLen];

// Button setup
bool isRecording = false;
#define BUTTON_IN 23

// Networking information
String ssid = "YOUR_WIFI_SSID";
String password = "YOUR_WIFI_PASSWORD";
const char* host = "1.1.1.1";  // IP of your Python TCP server
const uint16_t send_port = 7998;          // Port on which your Python server is listening
const uint16_t receive_port = 7999;

WiFiClient client;
File file;
bool receivedAudio = false;
bool playing = false;

// Create audio object
Audio audio;
//Handling state
enum State {
  AwaitRecord,
  Recording,
  Sending,
  AwaitReceive,
  Playing
};
State assist_state = AwaitRecord;

void setup() {
  // Start Serial Monitor
  Serial.begin(115200);
  
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  SPIFFS.format();

  // Setup WiFi in Station mode
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
 
  // WiFi Connected, print IP to serial monitor
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("");

  // Connect MAX98357 I2S Amplifier Module
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

  // The I2S config for microphone
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = i2s_bits_per_sample_t(16),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = bufferLen,
    .use_apll = false
  };

  // The pin config as per the setup
  const i2s_pin_config_t pin_config = {
      .bck_io_num = MIC_SCK,   // Serial Clock (SCK)
      .ws_io_num = MIC_WS,    // Word Select (WS)
      .data_out_num = I2S_PIN_NO_CHANGE, // not used (only for speakers)
      .data_in_num = MIC_SD   // Serial Data (SD)
  };

  esp_err_t err;
  err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed installing driver: %d\n", err);
    while (true);
  }
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed setting pin: %d\n", err);
    while (true);
  }
  Serial.println("I2S driver installed.");

  // Setup Button
  pinMode(BUTTON_IN, INPUT_PULLDOWN);

  //sendRequest();
}

void loop() {
  static unsigned long lastAttemptTime = 0;
  unsigned long currentMillis = millis();
  // Check if 5 seconds have passed since the last attempt, and audio is neither received nor playing
  if (currentMillis - lastAttemptTime > 7500 && assist_state == AwaitReceive) {
    sendRequest();
    lastAttemptTime = currentMillis;  // Reset the timer after a request
  }

  // Check if there is received audio that needs to be played
  if (receivedAudio) {
    receivedAudio = false;
    assist_state = Playing;
    audio.connecttoFS(SPIFFS, "/audio.wav");
  }

  // Handle the audio playback loop
  if (assist_state == Playing) {
    audio.loop();
  }

  int buttonState = digitalRead(BUTTON_IN);
  if (assist_state == AwaitRecord && buttonState == HIGH) {
    assist_state = Recording;
    startRecord();
  } else if (assist_state == Recording && buttonState == LOW) {
    assist_state = Sending;
    stopRecord();
    sendFile("/record.raw");
  }

  if (assist_state == Recording) {
    size_t bytesIn = 0;
    esp_err_t result = i2s_read(I2S_PORT, &sBuffer, sizeof(sBuffer), &bytesIn, portMAX_DELAY);
    if (result == ESP_OK && bytesIn > 0) {
      file.write((const byte*)sBuffer, bytesIn);
    }
  }
}

void sendRequest() {
  if (client.connect(host, receive_port)) {
    file = SPIFFS.open("/audio.wav", FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
      return;
    }

    client.println('A');  // Send initial data request
    Serial.println("[Tx] A");

    unsigned long lastReceiveTime = millis();
    while (client.connected() || millis() - lastReceiveTime < 250) {  // Wait extra time after last received data
      while (client.available()) {
        uint8_t buffer[1024];
        size_t bytesReceived = client.read(buffer, sizeof(buffer));
        if (bytesReceived > 0) {
          file.write(buffer, bytesReceived);
          lastReceiveTime = millis();  // Update last receive time
        }
      }
      delay(20);
    }

    client.stop();
    file.close();
    Serial.println("File downloaded.");
    receivedAudio = true;
  } else {
    Serial.println("Connection failed");
    return;
  }
}

void startRecord() {
  if (SPIFFS.exists("/record.raw")) {
    SPIFFS.remove("/record.raw");
  }
  file = SPIFFS.open("/record.raw", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  Serial.println("Recording started");
}

void stopRecord() {
  file.close();
  Serial.println("Recording stopped");
}

void sendFile(const char* path) {
  if (!client.connect(host, send_port)) {
    Serial.println("Connection to host failed");
    delay(1000);
    return;
  }

  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for sending");
    return;
  }

  uint8_t buf[512];
  while (file.available()) {
    size_t len = file.readBytes((char *) buf, sizeof(buf));
    client.write(buf, len);
  }
  file.close();
  client.stop();
  Serial.println("File sent");
  SPIFFS.remove(path);

  assist_state = AwaitReceive;
}

// Audio status functions
void audio_info(const char *info) {
  Serial.print("info        "); Serial.println(info);
}
void audio_id3data(const char *info) { //id3 metadata
  Serial.print("id3data     "); Serial.println(info);
}
void audio_eof_mp3(const char *info) { //end of file
  assist_state = AwaitRecord;
  Serial.print("eof_mp3     "); Serial.println(info);
  SPIFFS.remove("/audio.wav");
}
void audio_showstation(const char *info) {
  Serial.print("station     "); Serial.println(info);
}
void audio_showstreaminfo(const char *info) {
  Serial.print("streaminfo  "); Serial.println(info);
}
void audio_showstreamtitle(const char *info) {
  Serial.print("streamtitle "); Serial.println(info);
}
void audio_bitrate(const char *info) {
  Serial.print("bitrate     "); Serial.println(info);
}
void audio_commercial(const char *info) { //duration in sec
  Serial.print("commercial  "); Serial.println(info);
}
void audio_icyurl(const char *info) { //homepage
  Serial.print("icyurl      "); Serial.println(info);
}
void audio_lasthost(const char *info) { //stream URL played
  Serial.print("lasthost    "); Serial.println(info);
}
void audio_eof_speech(const char *info) {
  Serial.print("eof_speech  "); Serial.println(info);
}
