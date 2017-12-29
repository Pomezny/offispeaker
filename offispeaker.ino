#include <bitswap.h>
#include <chipsets.h>
#include <color.h>
#include <colorpalettes.h>
#include <colorutils.h>
#include <controller.h>
#include <cpp_compat.h>
#include <dmx.h>
#include <FastLED.h>
#include <fastled_config.h>
#include <fastled_delay.h>
#include <fastled_progmem.h>
#include <fastpin.h>
#include <fastspi.h>
#include <fastspi_bitbang.h>
#include <fastspi_dma.h>
#include <fastspi_nop.h>
#include <fastspi_ref.h>
#include <fastspi_types.h>
#include <hsv2rgb.h>
#include <led_sysdefs.h>
#include <lib8tion.h>
#include <noise.h>
#include <pixelset.h>
#include <pixeltypes.h>
#include <platforms.h>
#include <power_mgt.h>

#include <ESP32WebServer.h> 
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>

#define BUFF_SIZE 1024
//there seems to be some delay in addition to set value. Lower number by a little bit until it sounds right.
#define TIME_BETWEEN_SAMPLES 55 // (int) 1/sample_rate 
const char * ssid = "Seth_AP";
const char * password = "mojewifina";

ESP32WebServer server(80);
String wavFile;
TaskHandle_t fileReaderTask;
uint8_t soundBuffer1[BUFF_SIZE];
uint8_t soundBuffer2[BUFF_SIZE];
bool keepPlaying = false;
bool isSDOK = false;

CRGB leds[1];
CRGB webColor=CRGB::White;
byte webBrightness=255;

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, 18, GRB>(leds, 1);

  leds[0] = CRGB::Red;
  FastLED.show();
  Serial.print("Connecting to:");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  leds[0] = CRGB::Black;
  FastLED.show();

  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/play", handlePlay);
  server.on("/mountSD", handleMountSD);
  server.on("/setLed", handleSetLed);
  server.on("/getLed", handleGetLed);
  server.onNotFound(handleNotFound);
  server.begin();

  initSD();

  createClientHandlerTask();
}

void loop() {
  vTaskDelete(NULL);
}

void handleRoot() {
  if (!isSDOK) {
    handleSDNotMounted();
    return;
  }  
  File file = SD_MMC.open("/index.html");
  if (file) {
    server.streamFile(file, "text/html");
  } else {
    handleNotFound();
  }
}

void handleConfig() {
  if (!isSDOK) {
    handleSDNotMounted;
    return;
  }
  File file = SD_MMC.open("/config.json");
  if (file) {
    server.streamFile(file, "application/json");
  } else {
    handleNotFound();
  }
}

void handleOK(){
  server.send(200, "application/json", "{Result:\"OK\"}");
}

void handleNOK(){
  server.send(200, "application/json", "{Result:\"NOK\"}");
}

void handlePlay() {
  if (!isSDOK) {
    handleSDNotMounted();
    return;
  }
  if (server.hasArg("name")) {
    playSound("/sounds/"+server.arg("name"));
  }
}

void handleMountSD() {
  if (isSDOK) {
    server.send(200, "text/plain", "SD card is already mounted");
  } else {
    initSD();
    server.send(200, "text/plain", isSDOK ? "SD card mounted successfully" : "Cannot mount SD card");
  }
}

void handleSDNotMounted() {
  if (!isSDOK) {
    Serial.println("SD card is not mounted!");
    server.send(404, "text/plain", "SD card is not mounted :-( \n\n");
  }
}

void handleSetLed() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b") && server.hasArg("a")) {
    webColor.r=server.arg("r").toInt();
    webColor.g=server.arg("g").toInt();
    webColor.b=server.arg("b").toInt();
    webBrightness=map(server.arg("a").toInt(),0,100,0,255);
    handleOK();
    FastLED.setBrightness(webBrightness);
    leds[0]=webColor;
    FastLED.show();
  }
}

void handleGetLed() {
  server.send(200, "application/json", "{\"r\":\""+String(webColor.r)+"\",\"g\":\""+String(webColor.g)+"\",\"b\":\""+String(webColor.b)+"\",\"a\":\""+String(map(webBrightness,0,255,0,100))+"\"}");
}

void handleNotFound() {
  server.send(404, "text/plain", "Ooops. File Not Found\n\n");
}

void initSD() {
  if (!SD_MMC.begin("/sdcard", true)) { //at the moment using 1-bit SD interface
    Serial.println("Card Mount Failed");
    isSDOK = false;
  } else {
    isSDOK = true;
  }
}

void playSound(String fileName) {
  if(keepPlaying){
    Serial.println("Sound is already playing. Skipping");
    handleNOK();
    return;
  }
  handleOK();
  wavFile = fileName;
  createFileReaderTask();
  delay(10);
  createWavPlayerTask();
}

void wavPlayer(void * param) {
  bool firstBuffer = true;
  leds[0] = CRGB::Green;
  FastLED.show();
  while (keepPlaying) {
    if (firstBuffer) {
      for (int i = 0; i < BUFF_SIZE; i++) {
        dacWrite(25, soundBuffer1[i]);
        delayMicroseconds(TIME_BETWEEN_SAMPLES);
      }
      firstBuffer = false;
    } else {
      for (int i = 0; i < BUFF_SIZE; i++) {
        dacWrite(25, soundBuffer2[i]);
        delayMicroseconds(TIME_BETWEEN_SAMPLES);
      }
      firstBuffer = true;
    }
    vTaskResume(fileReaderTask);
  }
  leds[0] = webColor;
  FastLED.show();
  vTaskDelete(NULL);
}

void fileReader(void * param) {
  if (!isSDOK) {
    Serial.println("SD card is not mounted. Deleting task.");
    vTaskDelete(NULL);
  }
  Serial.println("Opening file");
  File file = SD_MMC.open(wavFile);
  if (file) {
    keepPlaying = true;
    size_t len = file.size();
    bool firstBuffer = true;
    bool firstRun = true;
    while (len) {
      size_t toRead = len;
      if (toRead > BUFF_SIZE) {
        toRead = BUFF_SIZE;
      }

      if (firstBuffer) {
        firstBuffer = false;
        file.read(soundBuffer1, toRead);
      } else {
        firstBuffer = true;
        file.read(soundBuffer2, toRead);
      }
      len -= toRead;
      if (!firstRun) {
        vTaskSuspend(NULL); //pause after filling buffer
      } else {
        firstRun = false;
      }
    }
    file.close();
    Serial.println("File closed");
    keepPlaying = false;
  }else{
    Serial.println("failed to open:"+wavFile);
  }
  vTaskDelete(NULL);
}

void clientHandler(void * param) {
  for(;;){
    server.handleClient();
    delay(100);
  }
}

void createWavPlayerTask() {
  xTaskCreatePinnedToCore(
    wavPlayer,
    "wavPlayer Task",
    10000,
    NULL,
    1,
    NULL,
    1);
}

void createFileReaderTask() {
  xTaskCreatePinnedToCore(
    fileReader,
    "fileReader Task",
    10000,
    NULL,
    2,
    & fileReaderTask,
    0);
}

void createClientHandlerTask() {
  xTaskCreatePinnedToCore(
    clientHandler,
    "clientHandler Task",
    10000,
    NULL,
    1,
    NULL,
    0);
}
