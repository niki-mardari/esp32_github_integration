/*
 * Programmer: Matas Noreika, adjusted by Niki Mardari
 * Date: 2026-07-07
 * Purpose: Get hardware talking to github
 * Adjusted for current hardware 
 * 
 * Hardware: 
 * Esp32S3 N16R8 Devkit C --> ST7789 GMT20-02-7P SPI display 
 * 
 * Github repository: https://github.com/matas-noreika/esp32_github_integration
 * File link on repository: https://raw.githubusercontent.com/matas-noreika/esp32_github_integration/refs/heads/image-data/data/current.csv
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <esp_timer.h>

#define CARRY_BUFFSIZE 10 // max size of partial token needed to be carried (6 (max token size) + 4 (overhead))
#define READ_BUFFSIZE 1024 // max size of chunk to read from HTTP stream
#define TOKEN_SIZE 7 // size of hex code expected 0x0000\0 <- format

const char* ssid     = "yep";
const char* password = "123456789";

const char* url = "https://raw.githubusercontent.com/niki-mardari/esp32_github_integration/refs/heads/image-data/data/current.csv";

#define SCREEN_HEIGHT 320
#define SCREEN_WIDTH 240

// Genuine Tenstar Robot ESP32-S3 TFT Pinout
#define TFT_MOSI     11
#define TFT_SCLK     12
#define TFT_MISO     -1  // Not physically used by screen but assigned to SPI hardware
#define TFT_CS        10
#define TFT_DC       6
#define TFT_RST      7
#define TFT_BL       5  // Backlight pin

//create image data map
uint16_t image[240*320] = {0};
uint8_t carryBuf[CARRY_BUFFSIZE];
size_t carryLen;
size_t imagebuf_index = 0;

int64_t updateTime = 0;

SPIClass *tftSPI = new SPIClass(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(tftSPI, TFT_CS, TFT_DC, TFT_RST);

void connectWiFi(){
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nConnected, IP: " + WiFi.localIP().toString());
}

// Called once per complete token (already null-terminated)
void handleToken(const char* token) {
  if (strlen(token) == 0) return;  // skip empty tokens (e.g. from "\n" right after ",")
  // if (strlen(token) < TOKEN_SIZE){
  //   Serial.println("Moving to carry buf");
  //   Serial.println(token);
  //   memset(carryBuf, 0, sizeof(carryBuf));
  //   memcpy(carryBuf,token, strlen(token));
  //   carryLen = strlen(token);
  //   return;
  // }

  char* end; // pointer used for error handling
  long value = strtol(token, &end, 16);
  
  if (end == token || *end != '\0') { //if non of the characters were evaluated or only partially
    Serial.printf("Malformed token: \"%s\"\n", token);
  } else {
    Serial.printf("Parsed byte: 0x%02lX\n", value);
    image[imagebuf_index] = value;
    imagebuf_index++;
  }
}

// Processes one chunk of raw bytes, handling tokens that span chunk boundaries
void processChunk(const uint8_t* data, size_t len) {
  size_t tokenStart = 0;
  carryLen = 0;  // consumed; will be repopulated below if this chunk also ends mid-token

  char tokenBuf[TOKEN_SIZE];
  // data pre-processing (take all valid tokens in chunk)
  for(size_t i = 0; i < len; i++){
    // current character in stream
    char c = data[i];

    //check if the delimiter characters are hit
    if(c == ',' || c == '\n' || c == '\r'){
      size_t tokenLen = i - tokenStart;
      
      //check if the token is of valid length
      if(tokenLen < TOKEN_SIZE){
        //copy token to token buffer
        memcpy(tokenBuf, (uint8_t *)data+tokenStart, tokenLen);
        tokenBuf[tokenLen] = '\0'; // null terminate to form string
        handleToken(tokenBuf); // process the token
      }else{
        Serial.println("Token too large discarding");
      }
      //reference the new token start
      tokenStart = i + 1;
    }
  }

  //check for remaining characters (carry over material)
  size_t remaining = len - tokenStart;
  if(remaining > 0){
    if(remaining < TOKEN_SIZE){
      memcpy(carryBuf, data+tokenStart, remaining);
      carryLen = remaining;
    }else {
      Serial.println("Carry over too large, discarding");
      carryLen = 0;
    }
  }else {
    carryLen = 0;
  }
  // char *token = strtok(data, ",");

  // while(token != NULL){
  //   handleToken(token);
  //   token = strtok(NULL, ",");
  // }

}

void fetchImageData(){
  imagebuf_index = 0;
  WiFiClientSecure client; // create socket handle
  client.setInsecure(); // disable certificate validation
  HTTPClient https; // create a http handle

  if (https.begin(client, url)) { // http pipe established
    int httpCode = https.GET();

    if (httpCode == HTTP_CODE_OK) {
      int contentLength = https.getSize();
      WiFiClient* stream = https.getStreamPtr();

      // Combined buffer: carry-over space + fresh read space
      uint8_t combinedBuf[CARRY_BUFFSIZE + READ_BUFFSIZE];

      while (https.connected() && (contentLength > 0 || contentLength == -1)) {
        size_t availableBytes = stream->available();

        if (availableBytes) {
          memset(combinedBuf, 0, sizeof(combinedBuf));
          // 1. Copy any carried-over partial token to the front of the buffer
          memcpy(combinedBuf, carryBuf, carryLen);

          // 2. Read new bytes directly after the carry-over
          size_t toRead = min((size_t)READ_BUFFSIZE, availableBytes);
          int readBytes = stream->readBytes((uint8_t*)(combinedBuf + carryLen), toRead);

          size_t totalLen = carryLen + readBytes;
          carryLen = 0;  // consumed; processBuffer() will repopulate if needed

          Serial.println("Carry data: ");
          Serial.write(carryBuf, carryLen);
          Serial.println("Chunk data + carry: ");
          Serial.write(combinedBuf, totalLen);
          Serial.println();
          // 3. Process the combined buffer (carry + new data) as one unit
          processChunk(combinedBuf, totalLen);

          if (contentLength > 0) contentLength -= readBytes;
        }
        delay(1);
      }

      // flushRemaining();  // handle trailing token with no final delimiter
    } else {
      Serial.printf("GET failed, HTTP code: %d\n", httpCode);
    }

    https.end();
  } else {
    Serial.println("Unable to connect");
  }
}

void setup() {
  Serial.begin(115200); // start serial debugging
  delay(1000); // give some time for device to initialise
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tftSPI->begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  // put your setup code here, to run once:
  // Initialize the screen hardware
  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  // Boost the hardware SPI clock to 40MHz for faster image transfers
  tft.setSPISpeed(40000000); 
  // Set rotation (Try 1 or 3 for landscape, 0 or 2 for portrait)
  tft.setRotation(2); 
  
  // Clear the screen with a solid background
  tft.fillScreen(ST77XX_BLACK);
  
  connectWiFi(); // attempt to connect to wifi
  fetchImageData();
  tft.drawRGBBitmap(0,0,image,SCREEN_WIDTH,SCREEN_HEIGHT);
}

void loop() {
  int64_t currentTime = esp_timer_get_time();
  //Serial.println(currentTime);
  //1,200,000,000ms in 20mins
  // if(updateTime - currentTime >= 1200000000){
  //   fetchImageData();
  //   tft.drawRGBBitmap(0,0,image,SCREEN_WIDTH,SCREEN_HEIGHT);
  //   updateTime = currentTime;
  // }
  //300,000,000ms in 5mins
  if(currentTime - updateTime >= 300000000){
    Serial.println("Updating");
    updateTime = esp_timer_get_time();
    fetchImageData();
    tft.drawRGBBitmap(0,0,image,SCREEN_WIDTH,SCREEN_HEIGHT);
  }
}