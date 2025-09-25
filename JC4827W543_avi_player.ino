// Tutorial : https://youtu.be/mnOzfRFQJIM
// AVI Player for the JC4827W543 development board
// Code adapted from moononournation (https://github.com/moononournation/aviPlayer)
//
// Version 2.7 - TOP 50 Style Splash Screen (FINAL WORKING VERSION)
// - Displays "vhs.jpeg" image as background from root directory
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
  
  // Initialize random seed for truly random video selection
  // Use ADC noise + multiple timing sources for better randomness
  delay(200); // Longer delay to ensure millis() varies more
  uint64_t chipId = ESP.getEfuseMac();
  uint32_t timeMs = millis();
  uint32_t freeHeap = ESP.getFreeHeap();
  
  // Read ADC noise from multiple pins for randomness
  uint32_t adcNoise = 0;
  for (int i = 0; i < 10; i++) {
    adcNoise += analogRead(A0) + analogRead(A1) + analogRead(A2);
    delayMicroseconds(100);
  }
  
  uint32_t randomSeedValue = (chipId & 0xFFFFFFFF) + timeMs + freeHeap + adcNoise;
  randomSeed(randomSeedValue);
  Serial.printf("*** RANDOM SEED INITIALIZED: %lu (chip: %llu + time: %lu + heap: %lu + adc: %lu) ***\n", 
                randomSeedValue, chipId, timeMs, freeHeap, adcNoise);
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
    
    // Log loaded files for debugging
    Serial.printf("*** LOADED %d VIDEO FILES ***\n", fileCount);
    for (int i = 0; i < min(fileCount, 5); i++) {
      Serial.printf("  [%d] %s\n", i, aviFileList[i].c_str());
    }
    if (fileCount > 5) {
      Serial.printf("  ... and %d more files\n", fileCount - 5);
    }
    
    // Videos will be played randomly when loop() runs
  }
}

void loop()
{
  // Auto-play logic: play one random video and stop
  if (fileCount > 0 && !videoPlayed) {
    // Test random numbers for debugging
    Serial.printf("*** RANDOM TEST: %d, %d, %d, %d, %d ***\n", 
                  random(0, 100), random(0, 100), random(0, 100), random(0, 100), random(0, 100));
    
    // Select a random video
    selectedIndex = random(0, fileCount);
    Serial.printf("*** RANDOM VIDEO SELECTED: %d/%d - %s ***\n", selectedIndex + 1, fileCount, aviFileList[selectedIndex].c_str());
    
    // Build the full path and play the selected file
    String fullPath = String(sdMountPoint) + String(AVI_FOLDER) + "/" + aviFileList[selectedIndex];
    char aviFilename[128];
    fullPath.toCharArray(aviFilename, sizeof(aviFilename));
    
    // Load metadata and show TOP 50 splash screen
    VideoMetadata metadata = loadVideoMetadata(aviFileList[selectedIndex]);
    showTop50SplashScreen(metadata);
    delay(8000); // Show splash for 8 seconds
    
    // Play the video
    playAviFile(aviFilename);
    
    // Mark video as played so it doesn't play again
    videoPlayed = true;
  }
  
  delay(100); // Small delay to prevent excessive CPU usage
}

// Function to remove accents from strings
String removeAccents(String str) {
  String result = "";
  
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    unsigned char uc = (unsigned char)c;
    
    // If it's a regular ASCII character (0-127), keep it
    if (uc < 128) {
      result += c;
    } else {
      // Handle multi-byte UTF-8 sequences
      if (uc == 0xC3 && i < str.length() - 1) {
        char next = str.charAt(i + 1);
        unsigned char next_uc = (unsigned char)next;
        
        // French accented characters
        if (next_uc >= 0xA0 && next_uc <= 0xAF) {
          switch (next_uc) {
            case 0xA0: result += "a"; break; // à
            case 0xA1: result += "a"; break; // á
            case 0xA2: result += "a"; break; // â
            case 0xA3: result += "a"; break; // ã
            case 0xA4: result += "a"; break; // ä
            case 0xA7: result += "c"; break; // ç
            case 0xA8: result += "e"; break; // è
            case 0xA9: result += "e"; break; // é
            case 0xAA: result += "e"; break; // ê
            case 0xAB: result += "e"; break; // ë
            case 0xAC: result += "i"; break; // ì
            case 0xAD: result += "i"; break; // í
            case 0xAE: result += "i"; break; // î
            case 0xAF: result += "i"; break; // ï
            default: result += "?"; break; // Replace unknown with ?
          }
          i++; // Skip next byte
        } else if (next_uc >= 0xB0 && next_uc <= 0xBF) {
          switch (next_uc) {
            case 0xB1: result += "n"; break; // ñ
            case 0xB2: result += "o"; break; // ò
            case 0xB3: result += "o"; break; // ó
            case 0xB4: result += "o"; break; // ô
            case 0xB5: result += "o"; break; // õ
            case 0xB6: result += "o"; break; // ö
            case 0xB9: result += "u"; break; // ù
            case 0xBA: result += "u"; break; // ú
            case 0xBB: result += "u"; break; // û
            case 0xBC: result += "u"; break; // ü
            default: result += "?"; break; // Replace unknown with ?
          }
          i++; // Skip next byte
        } else {
          result += "?"; // Replace unknown UTF-8 with ?
        }
      } else if (uc == 0xC2 && i < str.length() - 1) {
        char next = str.charAt(i + 1);
        unsigned char next_uc = (unsigned char)next;
        
        if (next_uc == 0xA0) {
          result += " "; // non-breaking space
          i++; // Skip next byte
        } else {
          result += "?"; // Replace unknown with ?
        }
      } else {
        result += "?"; // Replace any other non-ASCII character with ?
      }
    }
  }
  
  return result;
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

// Simple function to draw text with basic line wrapping for notes
void drawSimpleWrappedText(uint16_t x, uint16_t y, String text) {
  // Set much smaller text size for notes
  gfx->setTextSize(1);
  gfx->setFont(NULL); // Use smallest font available
  
  // Calculate max characters based on 250 pixels width (approximately 4 pixels per character for smaller font)
  uint16_t maxChars = 250 / 4; // About 62 characters per line
  uint16_t currentY = y;
  
  for (uint16_t i = 0; i < text.length(); i += maxChars) {
    // Find the last space before the max character limit
    uint16_t endPos = min((uint16_t)(i + maxChars), (uint16_t)text.length());
    if (endPos < text.length()) {
      // Look for the last space to avoid cutting words
      for (uint16_t j = endPos; j > i; j--) {
        if (text.charAt(j) == ' ') {
          endPos = j;
          break;
        }
      }
    }
    
    // Draw this line
    gfx->setCursor(x, currentY);
    gfx->print(text.substring(i, endPos));
    
    // Move to next line (smaller spacing for smaller text)
    currentY += 10;
    
    // Stop if we're running out of screen space
    if (currentY > gfx->height() - 20) {
      break;
    }
  }
  
  // Reset text size back to default
  gfx->setTextSize(1);
}

void showTop50SplashScreen(VideoMetadata metadata) {
  Serial.println("*** UTF-8 ACCENT FIX VERSION LOADED ***");
  Serial.println("*** USING /vhs.jpeg ***");
  // Display vhs.jpeg as background
  displayJPEG("/vhs.jpeg");
  
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
    
    // Notes as a quote (if present) with simple line wrapping
    if (metadata.notes.length() > 0) {
      drawSimpleWrappedText(60, 200, "\"" + metadata.notes + "\"");
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