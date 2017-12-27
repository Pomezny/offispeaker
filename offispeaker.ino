#include <ESP32WebServer.h> 
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>

#define BUFF_SIZE 1024
#define TIME_BETWEEN_SAMPLES 62 // (int) 1/sample_rate
const char * ssid = "Seth_AP";
const char * password = "mojewifina";

ESP32WebServer server(80);
String wavFile;
TaskHandle_t fileReaderTask;
uint8_t soundBuffer1[BUFF_SIZE];
uint8_t soundBuffer2[BUFF_SIZE];
bool keepPlaying = false;
bool isSDOK = false;

void setup() {
  Serial.begin(115200);

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

  server.on("/", handleRoot);
  server.on("/config.json", handleConfig);
  server.on("/play", handlePlay);
  server.on("/mountSD", handleMountSD);
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
  File file = SD_MMC.open("index.html");
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
  File file = SD_MMC.open("config.json");
  if (file) {
    server.streamFile(file, "application/json");
  } else {
    handleNotFound();
  }
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
    return;
  }
  wavFile = fileName;
  createFileReaderTask();
  delay(10);
  createWavPlayerTask();
}

void wavPlayer(void * param) {
  bool firstBuffer = true;
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
    vTaskDelete(NULL);
  }
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
