// Tutorial : https://youtu.be/mnOzfRFQJIM
// AVI Player for the JC4827W543 development board
// Code adapted from moononournation (https://github.com/moononournation/aviPlayer)
//
// Version 2.7 - TOP 50 Style Splash Screen (FINAL WORKING VERSION)
// - Displays "startup.jpeg" image as background from root directory
// - JSON metadata overlaid on the image (year, artist, title, notes)
// - Handles accents and special characters
// - Hidden files (. and _) are ignored
// - Supports up to 100 videos (MAX_FILES = 100)
// - Single video playback then stop (no loop)
// - Optimized code with debug logs for troubleshooting
// - Successfully tested with Arduino IDE compilation
//
// Use board "ESP32S3 Dev Module" (last tested on v3.2.0)
//
// COMPILATION OPTIONS (CRITICAL - Use these exact parameters):
// FQBN: esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default
//
// KEY PARAMETER: PSRAM=opi (NOT disabled) - Essential for screen to work!
//
// Libraries that you need to intall as Zip in the IDE:
// avilib: https://github.com/lanyou1900/avilib.git install as zip in the Arduino IDE
// libhelix: https://github.com/pschatzmann/arduino-libhelix.git install as zip in the Arduino IDE
//
const char *AVI_FOLDER = "/avi";
size_t output_buf_size;
uint16_t *output_buf;

#define MAX_FILES 100 // Increased to support up to 100 videos

String aviFileList[MAX_FILES];
int fileCount = 0;
int selectedIndex = 0;

// Video playback control
bool videoPlayed = false;

// Metadata structure for JSON files
struct VideoMetadata {
  String year;
  String artist;
  String title;
  String notes;
  bool hasMetadata;
};

#include <PINS_JC4827W543.h>    // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.6)
                                // Install "Dev Device Pins" with the Library Manager (last tested on v0.0.2)
#include <SD.h>                 // Included with the Espressif Arduino Core (last tested on v3.2.0)
#include <ArduinoJson.h>        // Install "ArduinoJson" with the Library Manager
#include "AviFunc.h"            // Included in this project
#include "esp32_audio.h"        // Included in this project
#include "FreeSansBold12pt7b.h" // Included in this project
#include <JPEGDecoder.h>        // Install "JPEGDecoder" with the Library Manager

static SPIClass spiSD{HSPI};
const char *sdMountPoint = "/sdcard"; 

#define TITLE_REGION_Y (gfx->height() / 3 - 30)
#define TITLE_REGION_H 35
#define TITLE_REGION_W (gfx->width())

void setup()
{
  Serial.begin(115200);
  Serial.println("*** ESP32 RESTARTED - NEW CODE LOADED ***");
  if (!gfx->begin())
  {
    Serial.println("gfx->begin() failed!");
    while (true)
    {
      /* no need to continue */
    }
  }
  // Set the backlight of the screen to High intensity
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  gfx->fillScreen(RGB565_BLACK);
  gfx->setFont(&FreeSansBold12pt7b);

  i2s_init();

  // SD Card initialization
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, spiSD, 10000000, sdMountPoint))
  {
    Serial.println("ERROR: SD Card mount failed!");
    while (true)
    {
      /* no need to continue */
    }
  }
  else
  {
    output_buf_size = gfx->width() * gfx->height() * 2;
    output_buf = (uint16_t *)aligned_alloc(16, output_buf_size);
    if (!output_buf)
    {
      Serial.println("output_buf aligned_alloc failed!");
      while (true)
      {
        /* no need to continue */
      }
    }

    avi_init();
    loadAviFiles();
    
    // Videos will be played randomly when loop() runs
  }
}

void loop()
{
  // Auto-play logic: play one random video and stop
  if (fileCount > 0 && !videoPlayed) {
    // Select a random video
    selectedIndex = random(0, fileCount);
    
    // Build the full path and play the selected file
    String fullPath = String(sdMountPoint) + String(AVI_FOLDER) + "/" + aviFileList[selectedIndex];
    char aviFilename[128];
    fullPath.toCharArray(aviFilename, sizeof(aviFilename));
    
    // Load metadata and show TOP 50 splash screen
    VideoMetadata metadata = loadVideoMetadata(aviFileList[selectedIndex]);
    showTop50SplashScreen(metadata);
    delay(5000); // Show splash for 5 seconds
    
    // Play the video
    playAviFile(aviFilename);
    
    // Mark video as played so it doesn't play again
    videoPlayed = true;
  }
  
  delay(100); // Small delay to prevent excessive CPU usage
}

// Function to remove accents from strings
String removeAccents(String str) {
  str.replace("à", "a");
  str.replace("á", "a");
  str.replace("â", "a");
  str.replace("ã", "a");
  str.replace("ä", "a");
  str.replace("è", "e");
  str.replace("é", "e");
  str.replace("ê", "e");
  str.replace("ë", "e");
  str.replace("ì", "i");
  str.replace("í", "i");
  str.replace("î", "i");
  str.replace("ï", "i");
  str.replace("ò", "o");
  str.replace("ó", "o");
  str.replace("ô", "o");
  str.replace("õ", "o");
  str.replace("ö", "o");
  str.replace("ù", "u");
  str.replace("ú", "u");
  str.replace("û", "u");
  str.replace("ü", "u");
  str.replace("ç", "c");
  str.replace("ñ", "n");
  return str;
}

// Load video metadata from JSON file
VideoMetadata loadVideoMetadata(String videoFileName) {
  VideoMetadata metadata;
  metadata.hasMetadata = false;
  
  // Remove .avi extension and add .json
  String jsonFileName = videoFileName;
  if (jsonFileName.endsWith(".avi")) {
    jsonFileName = jsonFileName.substring(0, jsonFileName.length() - 4) + ".json";
  }
  
  // Try to open the JSON file
  String jsonPath = String(AVI_FOLDER) + "/" + jsonFileName;
  File jsonFile = SD.open(jsonPath);
  
  if (!jsonFile) {
    return metadata;
  }
  
  // Parse JSON
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, jsonFile);
  jsonFile.close();
  
  if (error) {
    return metadata;
  }
  
  // Extract metadata
  if (doc.containsKey("year")) {
    metadata.year = doc["year"].as<String>();
  }
  if (doc.containsKey("artist")) {
    metadata.artist = doc["artist"].as<String>();
  }
  if (doc.containsKey("title")) {
    metadata.title = doc["title"].as<String>();
  }
  if (doc.containsKey("notes")) {
    metadata.notes = doc["notes"].as<String>();
  }
  
  metadata.hasMetadata = true;
  return metadata;
}

// Show TOP 50 style splash screen with image background
void showTop50SplashScreen(VideoMetadata metadata) {
  Serial.println("*** CORRECT CODE VERSION LOADED ***");
  Serial.println("*** USING /startup.jpeg ***");
  // Display startup.jpeg as background
  displayJPEG("/startup.jpeg");
  
  // Overlay text information on the image
  if (metadata.hasMetadata) {
    // Set text color to white for visibility over image
    gfx->setTextColor(RGB565_WHITE);
    
    // Year - position it appropriately on the image
    gfx->setCursor(60, 80);
    gfx->print(metadata.year);
    
    // Artist name
    gfx->setCursor(60, 120);
    gfx->print(metadata.artist);
    
    // Song title
    gfx->setCursor(60, 160);
    gfx->print(metadata.title);
    
    // Notes as a quote (if present)
    if (metadata.notes.length() > 0) {
      gfx->setCursor(60, 200);
      gfx->print("\"" + metadata.notes + "\"");
    }
    
  } else {
    // Fallback if no metadata
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(60, 120);
    gfx->print("MUSIC VIDEO");
    gfx->setCursor(60, 160);
    gfx->print("NOW PLAYING...");
  }
}

// Function to display JPEG image from SD card using JPEGDecoder library (working method)
void displayJPEG(const char* filename) {
  // Clear screen first
  gfx->fillScreen(RGB565_BLACK);
  
  Serial.print("Attempting to decode: ");
  Serial.println(filename);
  
  // Use the standard JPEGDecoder approach
  int decodeResult = JpegDec.decodeSdFile(filename);
  Serial.print("JpegDec result: ");
  Serial.println(decodeResult);
  
  if (decodeResult == 1) {
    Serial.println("Decode successful, getting dimensions...");
    // Get image dimensions
    uint16_t w = JpegDec.width;
    uint16_t h = JpegDec.height;
    
    Serial.print("Image dimensions: ");
    Serial.print(w);
    Serial.print("x");
    Serial.println(h);
    Serial.print("Screen dimensions: ");
    Serial.print(gfx->width());
    Serial.print("x");
    Serial.println(gfx->height());

    // Calculate scaling to fit screen
    float scale = min((float)gfx->width() / w, (float)gfx->height() / h);
    uint16_t scaledW = w * scale;
    uint16_t scaledH = h * scale;
    
    Serial.print("Scale factor: ");
    Serial.println(scale);

    // Center the image
    uint16_t x = (gfx->width() - scaledW) / 2;
    uint16_t y = (gfx->height() - scaledH) / 2;
    
    Serial.println("Starting to draw image...");

    // Draw the image using the library's built-in method
    JpegDec.decodeSdFile(filename);
    while (JpegDec.read()) {
      uint16_t* pImg = JpegDec.pImage;
      uint16_t mcu_x = JpegDec.MCUx;
      uint16_t mcu_y = JpegDec.MCUy;

      // Draw each MCU block
      for (uint16_t py = 0; py < JpegDec.MCUHeight; py++) {
        for (uint16_t px = 0; px < JpegDec.MCUWidth; px++) {
          if (mcu_x * JpegDec.MCUWidth + px < w && mcu_y * JpegDec.MCUHeight + py < h) {
            uint16_t color = pImg[py * JpegDec.MCUWidth + px];
            
            // Calculate position with scaling
            uint16_t drawX = x + (mcu_x * JpegDec.MCUWidth + px) * scale;
            uint16_t drawY = y + (mcu_y * JpegDec.MCUHeight + py) * scale;

            if (drawX < gfx->width() && drawY < gfx->height()) {
              gfx->drawPixel(drawX, drawY, color);
            }
          }
        }
      }
    }
    Serial.println("Image drawing completed");
  } else {
    Serial.println("Failed to decode JPEG file");
  }
}

// Play a single avi file store on the SD card
void playAviFile(char *avifile)
{
  if (avi_open(avifile))
  {
    Serial.printf("AVI start %s\n", avifile);
    gfx->fillScreen(BLACK);

    i2s_set_sample_rate(avi_aRate);

    avi_feed_audio();

    Serial.println("Start play audio task");
    BaseType_t ret_val = mp3_player_task_start();
    if (ret_val != pdPASS)
    {
      Serial.printf("mp3_player_task_start failed: %d\n", ret_val);
    }

    avi_start_ms = millis();

    Serial.println("Start play loop");
    while (avi_curr_frame < avi_total_frames)
    {
      avi_feed_audio();
      if (avi_decode())
      {
        avi_draw(0, 0);
      }
    }

    avi_close();
    Serial.println("AVI end");

    avi_show_stat();
  }
  else
  {
    Serial.println(AVI_strerror());
  }
}

// Read the avi file list in the avi folder
void loadAviFiles()
{
  File aviDir = SD.open(AVI_FOLDER);
  if (!aviDir)
  {
    Serial.println("Failed to open AVI folder");
    return;
  }
  fileCount = 0;
  while (true)
  {
    File file = aviDir.openNextFile();
    if (!file)
      break;
    if (!file.isDirectory())
    {
      String name = file.name();
      // Skip hidden files (starting with . or _)
      if (name.startsWith(".") || name.startsWith("_")) {
        file.close();
        continue;
      }
      if (name.endsWith(".avi") || name.endsWith(".AVI"))
      {
        aviFileList[fileCount++] = name;
        if (fileCount >= MAX_FILES)
          break;
      }
    }
    file.close();
  }
  aviDir.close();
}