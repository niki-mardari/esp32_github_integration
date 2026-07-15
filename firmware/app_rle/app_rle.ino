/*
 * Programmer: Matas Noreika
 * Modified by: Niki Mardari
 * Date: 2026-07-15
 *
 * Purpose:
 * Download a binary RLE RGB565 image from GitHub,
 * decode it back into a 135x240 image buffer,
 * and display it on the Tenstar ESP32-S3 ST7789 screen.
 *
 * Binary RLE format:
 * [count uint16 little-endian][colour uint16 little-endian]
 *
 * Example:
 * 4 pixels of 0x0000 is stored as:
 *
 * count  = 4      -> 04 00
 * colour = 0x0000 -> 00 00
 *
 * One RLE run is always 4 bytes.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <esp_timer.h>

// -------------------- WiFi --------------------

const char* ssid     = "";
const char* password = "";

const char* url = "https://raw.githubusercontent.com/niki-mardari/esp32_github_integration/refs/heads/image-data/data/Encoded/miku1.rle";

// -------------------- Display size --------------------

#define SCREEN_WIDTH  135
#define SCREEN_HEIGHT 240
#define TOTAL_PIXELS  (SCREEN_WIDTH * SCREEN_HEIGHT)

// -------------------- Tenstar ESP32-S3 ST7789 pins --------------------

#define TFT_MOSI     35
#define TFT_SCLK     36
#define TFT_MISO     37
#define TFT_CS        7
#define TFT_DC       39
#define TFT_RST      40
#define TFT_BL       45

// -------------------- Download settings --------------------

#define READ_BUFFSIZE 1024

// 5 minutes in microseconds.
// esp_timer_get_time() uses microseconds.
#define UPDATE_INTERVAL_US 300000000LL

// -------------------- Image buffer --------------------

// Important:
// Do NOT multiply by 2 here.
// uint16_t already uses 2 bytes per pixel.
uint16_t image[TOTAL_PIXELS];

// Tracks where the next decoded pixel goes.
size_t image_index = 0;

// Binary RLE run buffer.
// A complete run is 4 bytes:
// byte 0: count low
// byte 1: count high
// byte 2: colour low
// byte 3: colour high
uint8_t runBuf[4];
size_t runByteIndex = 0;

int64_t updateTime = 0;

// -------------------- TFT object --------------------

SPIClass *tftSPI = new SPIClass(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(tftSPI, TFT_CS, TFT_DC, TFT_RST);

// -------------------- WiFi connection --------------------

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connected, IP: " + WiFi.localIP().toString());
}

// -------------------- Little-endian decoder --------------------

// Converts two bytes into one uint16_t.
//
// Example:
// low  = 0x17
// high = 0x53
//
// result = 0x5317
uint16_t read_u16_le(uint8_t low, uint8_t high) {
  return (uint16_t)low | ((uint16_t)high << 8);
}

// -------------------- Checksum --------------------

// Simple checksum so you can see if the downloaded image changed.
uint32_t calculateChecksum() {
  uint32_t checksum = 2166136261UL;

  for (size_t i = 0; i < TOTAL_PIXELS; i++) {
    checksum ^= image[i] & 0xFF;
    checksum *= 16777619UL;

    checksum ^= image[i] >> 8;
    checksum *= 16777619UL;
  }

  return checksum;
}

// -------------------- RLE run handler --------------------

// Takes one decoded run and expands it into the image buffer.
//
// Example:
// count  = 4
// colour = 0x0000
//
// Writes:
// image[x] = 0x0000
// image[x + 1] = 0x0000
// image[x + 2] = 0x0000
// image[x + 3] = 0x0000
bool handleRun(uint16_t count, uint16_t colour) {
  if (count == 0) {
    Serial.println("Error: RLE run has count 0.");
    return false;
  }

  if (image_index + count > TOTAL_PIXELS) {
    Serial.println("Error: RLE data would overflow image buffer.");
    Serial.printf("Image index: %u\n", (unsigned int)image_index);
    Serial.printf("Run count:   %u\n", count);
    Serial.printf("Total:       %u\n", TOTAL_PIXELS);
    return false;
  }

  for (uint16_t i = 0; i < count; i++) {
    image[image_index] = colour;
    image_index++;
  }

  return true;
}

// -------------------- Binary RLE byte processor --------------------

// Processes raw bytes from the HTTP stream.
//
// Every 4 bytes makes one complete RLE run:
//
// byte 0 and 1 = count
// byte 2 and 3 = colour
bool processRleBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    runBuf[runByteIndex] = data[i];
    runByteIndex++;

    // Once we have 4 bytes, decode one complete RLE run.
    if (runByteIndex == 4) {
      uint16_t count  = read_u16_le(runBuf[0], runBuf[1]);
      uint16_t colour = read_u16_le(runBuf[2], runBuf[3]);

      if (!handleRun(count, colour)) {
        return false;
      }

      runByteIndex = 0;
    }
  }

  return true;
}

// -------------------- Download and decode image --------------------

bool fetchImageData() {
  image_index = 0;
  runByteIndex = 0;

  memset(image, 0, sizeof(image));
  memset(runBuf, 0, sizeof(runBuf));

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // Cache-busting so GitHub does not return an old cached file.
  String fetchUrl = String(url) + "?t=" + String(millis());

  Serial.println();
  Serial.println("Fetching binary RLE image...");
  Serial.println(fetchUrl);

  if (!https.begin(client, fetchUrl)) {
    Serial.println("Error: unable to start HTTPS request.");
    return false;
  }

  int httpCode = https.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("GET failed, HTTP code: %d\n", httpCode);
    https.end();
    return false;
  }

  int contentLength = https.getSize();
  WiFiClient* stream = https.getStreamPtr();

  Serial.printf("Download size: %d bytes\n", contentLength);

  uint8_t readBuffer[READ_BUFFSIZE];

  while (https.connected() && (contentLength > 0 || contentLength == -1)) {
    size_t availableBytes = stream->available();

    if (availableBytes > 0) {
      size_t toRead = min((size_t)READ_BUFFSIZE, availableBytes);
      int bytesRead = stream->readBytes(readBuffer, toRead);

      if (bytesRead > 0) {
        if (!processRleBytes(readBuffer, bytesRead)) {
          https.end();
          return false;
        }

        if (contentLength > 0) {
          contentLength -= bytesRead;
        }
      }
    }

    delay(1);
  }

  https.end();

  // If this is not zero, the file ended halfway through a 4-byte RLE run.
  if (runByteIndex != 0) {
    Serial.println("Error: RLE file ended with incomplete 4-byte run.");
    Serial.printf("Remaining partial bytes: %u\n", (unsigned int)runByteIndex);
    return false;
  }

  Serial.printf("Decoded pixels: %u / %u\n", (unsigned int)image_index, TOTAL_PIXELS);

  if (image_index != TOTAL_PIXELS) {
    Serial.println("Error: decoded pixel count is wrong.");
    return false;
  }

  uint32_t checksum = calculateChecksum();
  Serial.printf("Checksum: 0x%08lX\n", (unsigned long)checksum);

  return true;
}

// -------------------- Draw image --------------------

void drawImage() {
  if (image_index != TOTAL_PIXELS) {
    Serial.println("Image not drawn because pixel count is wrong.");
    return;
  }

  tft.drawRGBBitmap(0, 0, image, SCREEN_WIDTH, SCREEN_HEIGHT);
  Serial.println("Display updated.");
}

// -------------------- Setup --------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tftSPI->begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);

  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);

  // 40 MHz is fast. If the screen glitches, change this to 20000000.
  tft.setSPISpeed(40000000);

  // Try 0 or 2 for portrait depending on your screen direction.
  tft.setRotation(2);

  tft.fillScreen(ST77XX_BLACK);

  connectWiFi();

  Serial.println("Initial image load...");

  if (fetchImageData()) {
    drawImage();
  } else {
    Serial.println("Initial image load failed.");
  }

  updateTime = esp_timer_get_time();
}

// -------------------- Main loop --------------------

void loop() {
  int64_t currentTime = esp_timer_get_time();

  if (currentTime - updateTime >= UPDATE_INTERVAL_US) {
    Serial.println();
    Serial.println("5 minutes passed. Updating image...");

    if (fetchImageData()) {
      drawImage();
    } else {
      Serial.println("Update failed. Keeping previous image.");
    }

    updateTime = esp_timer_get_time();
  }
}