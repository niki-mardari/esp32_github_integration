/*
 * Date: 2026-07-15
 *
 * Purpose:
 * Download a batch of binary RLE RGB565 images from GitHub,
 * store them in PSRAM,
 * decode each image into a 135x240 buffer,
 * and cycle through them locally on the Tenstar ESP32-S3 ST7789.
 *
 * Binary RLE format:
 * [count uint16 little-endian][colour uint16 little-endian]
 *
 * Example:
 * 4 pixels of 0x0000:
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
#include "esp_heap_caps.h"

// -------------------- User settings --------------------

// How many images the ESP32 should download from the manifest.
// If your manifest has 12 images and this is 5, it downloads the first 5.
// If your manifest has 12 images and this is 12, it downloads all 12.
#define IMAGES_TO_DOWNLOAD 12

// Automatic timing spreads all cached images across the GitHub check window.
//
// Example:
// GITHUB_CHECK_US = 5 minutes
// IMAGES_TO_DOWNLOAD = 12
//
// 300 seconds / 12 images = 25 seconds per image
#define USE_AUTOMATIC_IMAGE_TIMING false

// Custom local image timing.
// This is only used when USE_AUTOMATIC_IMAGE_TIMING is false.
//
// 10000000 us = 10 seconds
// 25000000 us = 25 seconds
// 60000000 us = 60 seconds
#define CUSTOM_LOCAL_CYCLE_US 5000000LL

// How often the ESP32 checks GitHub for a new batch.
//
// 300000000 us = 5 minutes
// 60000000 us  = 1 minute
#define GITHUB_CHECK_US 300000000LL

// -------------------- WiFi --------------------

const char* ssid     = "";
const char* password = "";

// -------------------- GitHub batch URLs --------------------

const char* MANIFEST_URL =
  "https://raw.githubusercontent.com/niki-mardari/esp32_github_integration/refs/heads/image-data/data/current_batch/manifest.txt";

const char* BATCH_BASE_URL =
  "https://raw.githubusercontent.com/niki-mardari/esp32_github_integration/refs/heads/image-data/data/current_batch/";

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

// Maximum supported cache size.
// Keep this equal to or bigger than IMAGES_TO_DOWNLOAD.
#define MAX_CACHED_IMAGES 12

// Safety check.
// IMAGES_TO_DOWNLOAD should not be bigger than MAX_CACHED_IMAGES.
#if IMAGES_TO_DOWNLOAD > MAX_CACHED_IMAGES
#error "IMAGES_TO_DOWNLOAD cannot be bigger than MAX_CACHED_IMAGES"
#endif

// -------------------- Image buffer --------------------

// This is the decoded image buffer.
// Only one decoded image is needed at a time.
//
// 135 x 240 = 32400 pixels
// 32400 x 2 bytes = 64800 bytes
uint16_t image[TOTAL_PIXELS];

// -------------------- Cached RLE image storage --------------------

struct CachedImage {
  String name;
  uint8_t* data;
  size_t size;
};

CachedImage cachedImages[MAX_CACHED_IMAGES];

size_t cachedCount = 0;
size_t currentImageIndex = 0;

uint32_t lastManifestHash = 0;

int64_t lastLocalCycleTime = 0;
int64_t lastGithubCheckTime = 0;

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

// -------------------- Hash helper --------------------

uint32_t hashString(const String& text) {
  uint32_t hash = 2166136261UL;

  for (size_t i = 0; i < text.length(); i++) {
    hash ^= (uint8_t)text[i];
    hash *= 16777619UL;
  }

  return hash;
}

// -------------------- Little-endian decoder --------------------

uint16_t read_u16_le(const uint8_t* data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

// -------------------- Image checksum --------------------

uint32_t calculateImageChecksum() {
  uint32_t checksum = 2166136261UL;

  for (size_t i = 0; i < TOTAL_PIXELS; i++) {
    checksum ^= image[i] & 0xFF;
    checksum *= 16777619UL;

    checksum ^= image[i] >> 8;
    checksum *= 16777619UL;
  }

  return checksum;
}

// -------------------- Dynamic display timing --------------------

// Returns how long each image should stay on the screen.
//
// Automatic mode:
// Divides the GitHub check time by the number of cached images.
//
// Example:
// 300 seconds / 12 images = 25 seconds per image
//
// Manual mode:
// Uses CUSTOM_LOCAL_CYCLE_US directly.
int64_t getLocalCycleTime() {
  if (!USE_AUTOMATIC_IMAGE_TIMING) {
    return CUSTOM_LOCAL_CYCLE_US;
  }

  if (cachedCount == 0) {
    return CUSTOM_LOCAL_CYCLE_US;
  }

  return GITHUB_CHECK_US / cachedCount;
}

// -------------------- Free cached images --------------------

void freeCachedImages() {
  for (size_t i = 0; i < cachedCount; i++) {
    if (cachedImages[i].data != nullptr) {
      heap_caps_free(cachedImages[i].data);
      cachedImages[i].data = nullptr;
    }

    cachedImages[i].name = "";
    cachedImages[i].size = 0;
  }

  cachedCount = 0;
  currentImageIndex = 0;
}

// -------------------- Download text file --------------------

bool downloadTextFile(const String& url, String& output) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String fetchUrl = url + "?t=" + String(millis());

  Serial.println();
  Serial.println("Downloading text:");
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

  output = https.getString();

  https.end();
  return true;
}

// -------------------- Download binary file into PSRAM --------------------

bool downloadBinaryToPsram(const String& url, uint8_t** outputData, size_t* outputSize) {
  *outputData = nullptr;
  *outputSize = 0;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String fetchUrl = url + "?t=" + String(millis());

  Serial.println();
  Serial.println("Downloading binary:");
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

  if (contentLength <= 0) {
    Serial.printf("Error: bad content length: %d\n", contentLength);
    https.end();
    return false;
  }

  uint8_t* buffer = (uint8_t*)heap_caps_malloc(contentLength, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (buffer == nullptr) {
    Serial.println("Warning: PSRAM allocation failed, trying normal RAM.");
    buffer = (uint8_t*)malloc(contentLength);
  }

  if (buffer == nullptr) {
    Serial.printf("Error: could not allocate %d bytes.\n", contentLength);
    https.end();
    return false;
  }

  WiFiClient* stream = https.getStreamPtr();

  size_t totalRead = 0;
  uint8_t tempBuffer[READ_BUFFSIZE];

  while (https.connected() && totalRead < (size_t)contentLength) {
    size_t availableBytes = stream->available();

    if (availableBytes > 0) {
      size_t toRead = min((size_t)READ_BUFFSIZE, availableBytes);
      int bytesRead = stream->readBytes(tempBuffer, toRead);

      if (bytesRead > 0) {
        memcpy(buffer + totalRead, tempBuffer, bytesRead);
        totalRead += bytesRead;
      }
    }

    delay(1);
  }

  https.end();

  if (totalRead != (size_t)contentLength) {
    Serial.printf("Error: downloaded %u / %d bytes.\n",
                  (unsigned int)totalRead,
                  contentLength);

    heap_caps_free(buffer);
    return false;
  }

  if (totalRead % 4 != 0) {
    Serial.println("Error: RLE file size is not divisible by 4.");
    heap_caps_free(buffer);
    return false;
  }

  *outputData = buffer;
  *outputSize = totalRead;

  Serial.printf("Downloaded %u bytes.\n", (unsigned int)totalRead);
  return true;
}

// -------------------- Manifest parser --------------------

// Reads image names from manifest.txt.
//
// It ignores:
// empty lines
// comment lines starting with #
//
// It accepts:
// image_0.rle
// image_1.rle
//
// maxNames controls how many images the ESP32 will use.
int parseManifest(const String& manifest, String names[], int maxNames) {
  int count = 0;
  int start = 0;

  while (start < manifest.length() && count < maxNames) {
    int end = manifest.indexOf('\n', start);

    if (end == -1) {
      end = manifest.length();
    }

    String line = manifest.substring(start, end);
    line.trim();

    if (line.length() > 0 && !line.startsWith("#")) {
      if (line.endsWith(".rle")) {
        names[count] = line;
        count++;
      }
    }

    start = end + 1;
  }

  return count;
}

// -------------------- Decode RLE data into image buffer --------------------

bool decodeRleImage(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    Serial.println("Error: empty RLE data.");
    return false;
  }

  if (size % 4 != 0) {
    Serial.println("Error: RLE size is not divisible by 4.");
    return false;
  }

  size_t imageIndex = 0;

  memset(image, 0, sizeof(image));

  for (size_t i = 0; i < size; i += 4) {
    uint16_t count  = read_u16_le(data + i);
    uint16_t colour = read_u16_le(data + i + 2);

    if (count == 0) {
      Serial.println("Error: RLE run has count 0.");
      return false;
    }

    if (imageIndex + count > TOTAL_PIXELS) {
      Serial.println("Error: RLE data would overflow image buffer.");
      Serial.printf("Image index: %u\n", (unsigned int)imageIndex);
      Serial.printf("Run count:   %u\n", count);
      Serial.printf("Total:       %u\n", TOTAL_PIXELS);
      return false;
    }

    for (uint32_t j = 0; j < count; j++) {
      image[imageIndex] = colour;
      imageIndex++;
    }
  }

  if (imageIndex != TOTAL_PIXELS) {
    Serial.printf("Error: decoded %u / %u pixels.\n",
                  (unsigned int)imageIndex,
                  TOTAL_PIXELS);
    return false;
  }

  return true;
}

// -------------------- Display cached image --------------------

bool displayCachedImage(size_t index) {
  if (index >= cachedCount) {
    Serial.println("Error: cached image index out of range.");
    return false;
  }

  Serial.println();
  Serial.printf("Displaying cached image %u / %u\n",
                (unsigned int)(index + 1),
                (unsigned int)cachedCount);

  Serial.println(cachedImages[index].name);

  if (!decodeRleImage(cachedImages[index].data, cachedImages[index].size)) {
    Serial.println("Error: failed to decode cached image.");
    return false;
  }

  uint32_t checksum = calculateImageChecksum();

  Serial.printf("Checksum: 0x%08lX\n", (unsigned long)checksum);
  Serial.printf("Next image in: %lld seconds\n", getLocalCycleTime() / 1000000LL);

  tft.drawRGBBitmap(0, 0, image, SCREEN_WIDTH, SCREEN_HEIGHT);
  Serial.println("Display updated.");

  return true;
}

// -------------------- Update cached batch from GitHub --------------------

bool updateBatchIfNeeded() {
  String manifest;

  if (!downloadTextFile(MANIFEST_URL, manifest)) {
    Serial.println("Error: could not download manifest.");
    return false;
  }

  uint32_t manifestHash = hashString(manifest);

  if (manifestHash == lastManifestHash && cachedCount > 0) {
    Serial.println("Manifest unchanged. Keeping current cached batch.");
    return true;
  }

  Serial.println("New manifest detected. Downloading new batch...");

  String names[MAX_CACHED_IMAGES];

  // This is where IMAGES_TO_DOWNLOAD controls how many images the ESP32 downloads.
  int imageCount = parseManifest(manifest, names, IMAGES_TO_DOWNLOAD);

  if (imageCount <= 0) {
    Serial.println("Error: manifest has no RLE image files.");
    return false;
  }

  Serial.printf("Manifest images selected: %d\n", imageCount);

  CachedImage newCache[MAX_CACHED_IMAGES];

  for (int i = 0; i < MAX_CACHED_IMAGES; i++) {
    newCache[i].name = "";
    newCache[i].data = nullptr;
    newCache[i].size = 0;
  }

  int downloadedCount = 0;

  for (int i = 0; i < imageCount; i++) {
    String fileUrl = String(BATCH_BASE_URL) + names[i];

    uint8_t* data = nullptr;
    size_t size = 0;

    if (!downloadBinaryToPsram(fileUrl, &data, &size)) {
      Serial.println("Error: failed to download one batch image.");

      for (int j = 0; j < downloadedCount; j++) {
        if (newCache[j].data != nullptr) {
          heap_caps_free(newCache[j].data);
        }
      }

      return false;
    }

    newCache[downloadedCount].name = names[i];
    newCache[downloadedCount].data = data;
    newCache[downloadedCount].size = size;
    downloadedCount++;
  }

  // New batch downloaded successfully.
  // Now replace the old cache.
  freeCachedImages();

  for (int i = 0; i < downloadedCount; i++) {
    cachedImages[i] = newCache[i];
  }

  cachedCount = downloadedCount;
  currentImageIndex = 0;
  lastManifestHash = manifestHash;

  Serial.println();
  Serial.printf("Cached images: %u\n", (unsigned int)cachedCount);
  Serial.printf("Automatic timing: %s\n", USE_AUTOMATIC_IMAGE_TIMING ? "ON" : "OFF");
  Serial.printf("Image display time: %lld seconds\n", getLocalCycleTime() / 1000000LL);
  Serial.printf("GitHub check time: %lld seconds\n", GITHUB_CHECK_US / 1000000LL);
  Serial.printf("Free heap:  %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());

  displayCachedImage(currentImageIndex);

  return true;
}

// -------------------- Print settings --------------------

void printSettings() {
  Serial.println();
  Serial.println("Settings:");
  Serial.printf("Images to download: %d\n", IMAGES_TO_DOWNLOAD);
  Serial.printf("Max cached images:  %d\n", MAX_CACHED_IMAGES);
  Serial.printf("Auto timing:        %s\n", USE_AUTOMATIC_IMAGE_TIMING ? "ON" : "OFF");
  Serial.printf("GitHub check:       %lld seconds\n", GITHUB_CHECK_US / 1000000LL);

  if (USE_AUTOMATIC_IMAGE_TIMING) {
    Serial.println("Image timing:       automatic");
  } else {
    Serial.printf("Image timing:       %lld seconds\n", CUSTOM_LOCAL_CYCLE_US / 1000000LL);
  }
}

// -------------------- Setup --------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Tenstar ESP32-S3 RLE Batch Viewer");
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
  Serial.printf("Free heap:  %u bytes\n", ESP.getFreeHeap());

  printSettings();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tftSPI->begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);

  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);

  // If the image glitches, reduce this to 20000000.
  tft.setSPISpeed(40000000);

  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);

  connectWiFi();

  updateBatchIfNeeded();

  lastLocalCycleTime = esp_timer_get_time();
  lastGithubCheckTime = esp_timer_get_time();
}

// -------------------- Main loop --------------------

void loop() {
  int64_t currentTime = esp_timer_get_time();

  // Calculate how long each cached image should stay on screen.
  int64_t localCycleTime = getLocalCycleTime();

  // Change displayed image from local cache.
  if (cachedCount > 0 && currentTime - lastLocalCycleTime >= localCycleTime) {
    currentImageIndex = (currentImageIndex + 1) % cachedCount;
    displayCachedImage(currentImageIndex);

    lastLocalCycleTime = esp_timer_get_time();
  }

  // Check GitHub for a new batch.
  if (currentTime - lastGithubCheckTime >= GITHUB_CHECK_US) {
    Serial.println();
    Serial.println("Checking GitHub for new batch...");

    updateBatchIfNeeded();

    // Reset both timers after the GitHub check.
    // This prevents the ESP32 from instantly skipping images
    // right after a new batch is downloaded.
    lastGithubCheckTime = esp_timer_get_time();
    lastLocalCycleTime = esp_timer_get_time();
  }
}