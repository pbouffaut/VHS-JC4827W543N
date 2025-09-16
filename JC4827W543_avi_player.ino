// Tutorial : https://youtu.be/mnOzfRFQJIM
// AVI Player for the JC4827W543 development board
// Code adapted from moononournation (https://github.com/moononournation/aviPlayer)
//
// Use board "ESP32S3 Dev Module" (last tested on v3.2.0)
//
// Libraries that you need to intall as Zip in the IDE:
// avilib: https://github.com/lanyou1900/avilib.git install as zip in the Arduino IDE
// libhelix: https://github.com/pschatzmann/arduino-libhelix.git install as zip in the Arduino IDE
//
const char *AVI_FOLDER = "/avi";
size_t output_buf_size;
uint16_t *output_buf;

#define MAX_FILES 10 // Adjust as needed

String aviFileList[MAX_FILES];
int fileCount = 0;
int selectedIndex = 0;

#include <PINS_JC4827W543.h>    // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.6)
                                // Install "Dev Device Pins" with the Library Manager (last tested on v0.0.2)
#include <SD.h>                 // Included with the Espressif Arduino Core (last tested on v3.2.0)
#include "AviFunc.h"            // Included in this project
#include "esp32_audio.h"        // Included in this project
#include "FreeSansBold12pt7b.h" // Included in this project
#include <JPEGDecoder.h>        // Install "JPEGDecoder" with the Library Manager (last tested on v2.0.0)

static SPIClass spiSD{HSPI};
const char *sdMountPoint = "/sdcard"; 

#define TITLE_REGION_Y (gfx->height() / 3 - 30)
#define TITLE_REGION_H 35
#define TITLE_REGION_W (gfx->width())

// Auto-play variables
unsigned long lastVideoEndTime = 0;
const unsigned long videoTransitionDelay = 2000; // 2 seconds between videos

void setup()
{
  Serial.begin(115200);
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
    
    // Display a random image from img/ folder for 3 seconds
    displayRandomImage();
    delay(3000); // Show random image for 3 seconds
    
    // Display startup message
    displayStartupMessage();
    delay(3000); // Show startup message for 3 seconds
    
    // Start auto-play if we have videos
    if (fileCount > 0) {
      lastVideoEndTime = millis();
    }
  }
}

void loop()
{
  // Auto-play logic: cycle through videos automatically
  if (fileCount > 0) {
    unsigned long currentTime = millis();
    
    // Check if it's time to play the next video
    if (currentTime - lastVideoEndTime >= videoTransitionDelay) {
      // Build the full path and play the selected file
      String fullPath = String(sdMountPoint) + String(AVI_FOLDER) + "/" + aviFileList[selectedIndex];
      char aviFilename[128];
      fullPath.toCharArray(aviFilename, sizeof(aviFilename));
      
      // Display current video info
      displayCurrentVideo();
      
      // Play the video
      playAviFile(aviFilename);
      
      // Move to next video
      selectedIndex++;
      if (selectedIndex >= fileCount) {
        selectedIndex = 0; // Loop back to first video
      }
      
      // Update timing for next video
      lastVideoEndTime = millis();
    }
  }
  
  delay(100); // Small delay to prevent excessive CPU usage
}

// Display current video information
void displayCurrentVideo()
{
  // Clear the screen
  gfx->fillScreen(RGB565_BLACK);
  
  // Display current video name
  String title = aviFileList[selectedIndex];
  int16_t x1, y1;
  uint16_t textW, textH;
  gfx->getTextBounds(title.c_str(), 0, 0, &x1, &y1, &textW, &textH);
  
  // Center the text
  int titleX = (gfx->width() - textW) / 2 - x1;
  int titleY = gfx->height() / 2;
  
  gfx->setCursor(titleX, titleY);
  gfx->print(title);
  
  // Display "Now Playing" text
  String nowPlaying = "Now Playing...";
  gfx->getTextBounds(nowPlaying.c_str(), 0, 0, &x1, &y1, &textW, &textH);
  int nowPlayingX = (gfx->width() - textW) / 2 - x1;
  int nowPlayingY = titleY - 40;
  
  gfx->setCursor(nowPlayingX, nowPlayingY);
  gfx->print(nowPlaying);
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

// Display startup message
void displayStartupMessage()
{
  // Clear the screen
  gfx->fillScreen(RGB565_BLACK);
  
  // Display startup message
  String message = "AVI Player Ready";
  int16_t x1, y1;
  uint16_t textW, textH;
  gfx->getTextBounds(message.c_str(), 0, 0, &x1, &y1, &textW, &textH);
  
  // Center the text
  int messageX = (gfx->width() - textW) / 2 - x1;
  int messageY = gfx->height() / 2;
  
  gfx->setCursor(messageX, messageY);
  gfx->print(message);
  
  // Display file count
  String fileCountMsg = "Found " + String(fileCount) + " videos";
  gfx->getTextBounds(fileCountMsg.c_str(), 0, 0, &x1, &y1, &textW, &textH);
  int fileCountX = (gfx->width() - textW) / 2 - x1;
  int fileCountY = messageY + 40;
  
  gfx->setCursor(fileCountX, fileCountY);
  gfx->print(fileCountMsg);
}

// Function to display JPEG image from SD card using JPEGDecoder library
void displayJPEG(const char* filename) {
  // Clear screen first
  gfx->fillScreen(RGB565_BLACK);
  
  // Use the standard JPEGDecoder approach
  if (JpegDec.decodeSdFile(filename) == 1) {
    // Get image dimensions
    uint16_t w = JpegDec.width;
    uint16_t h = JpegDec.height;

    // Calculate scaling to fit screen
    float scale = min((float)gfx->width() / w, (float)gfx->height() / h);
    uint16_t scaledW = w * scale;
    uint16_t scaledH = h * scale;

    // Center the image
    uint16_t x = (gfx->width() - scaledW) / 2;
    uint16_t y = (gfx->height() - scaledH) / 2;

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
  }
}

// Function to display a random image from img/ folder
void displayRandomImage() {
  File imgDir = SD.open("/img");
  if (!imgDir || !imgDir.isDirectory()) {
    return; // No img directory
  }

  // Count image files
  int imageCount = 0;
  String imageFiles[20]; // Max 20 images
  
  File file = imgDir.openNextFile();
  while (file && imageCount < 20) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (name.endsWith(".jpg") || name.endsWith(".jpeg") || 
          name.endsWith(".JPG") || name.endsWith(".JPEG")) {
        imageFiles[imageCount] = "/img/" + name;
        imageCount++;
      }
    }
    file.close();
    file = imgDir.openNextFile();
  }
  imgDir.close();

  // Select and display random image
  if (imageCount > 0) {
    int randomIndex = random(0, imageCount);
    displayJPEG(imageFiles[randomIndex].c_str());
  }
}