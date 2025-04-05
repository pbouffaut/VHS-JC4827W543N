// AVI Player for the JC4827W543 development board
// Code adapted from moononournation (https://github.com/moononournation/aviPlayer)
//
// Libraries that you need to intall as Zip in the IDE:
// avilib: https://github.com/lanyou1900/avilib.git install as zip in the Arduino IDE
// libhelix: https://github.com/pschatzmann/arduino-libhelix.git install as zip in the Arduino IDE
//
const char *root = "/root"; // Do not change this, it is needed to access files properly on the SD card
const char *AVI_FOLDER = "/avi";
size_t output_buf_size;
uint16_t *output_buf;

#define MAX_FILES 10 // Adjust as needed

String aviFileList[MAX_FILES];
int fileCount = 0;
int selectedIndex = 0;

#include <PINS_JC4827W543.h>    // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.6)
                                // Install "Dev Device Pins" with the Library Manager (last tested on v0.0.2)
#include "TAMC_GT911.h"         // Install "TAMC_GT911" with the Library Manager (last tested on v1.0.2)
#include <SD_MMC.h>             // Included with the Espressif Arduino Core (last tested on v3.2.0)
#include "AviFunc.h"            // Included in this project
#include "esp32_audio.h"        // Included in this project
#include "FreeSansBold12pt7b.h" // Included in this project

#define TITLE_REGION_Y (gfx->height()/3 - 30)
#define TITLE_REGION_H 35
#define TITLE_REGION_W (gfx->width())

// Touch Controller
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38
#define TOUCH_WIDTH 480
#define TOUCH_HEIGHT 272
TAMC_GT911 touchController = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

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
  touchController.begin();
  touchController.setRotation(ROTATION_INVERTED); // Change as needed

  i2s_init();

  // SD Card initialization
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SD_MMC.setPins(SD_SCK, SD_MOSI /* CMD */, SD_MISO /* D0 */);
  if (!SD_MMC.begin(root, true /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_DEFAULT))
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
    displaySelectedFile();
  }
}

void loop()
{
  touchController.read();
  if (touchController.touches > 0)
  {
    int tx = touchController.points[0].x;
    int ty = touchController.points[0].y;
    int screenW = gfx->width();
    int screenH = gfx->height();
    int arrowSize = 40;
    int margin = 10;
    int playButtonSize = 50;
    int playX = (screenW - playButtonSize) / 2;
    int playY = screenH - playButtonSize - 20;

    // Check if touch is in the left arrow area.
    if (tx < margin + arrowSize && ty > (screenH / 2 - arrowSize) && ty < (screenH / 2 + arrowSize))
    {
      // Left arrow touched: cycle to previous file.
      selectedIndex--;
      if (selectedIndex < 0)
        selectedIndex = fileCount - 1;
      updateTitle();
      while (touchController.touches > 0)
      {
        touchController.read();
        delay(50);
      }
      delay(300);
    }
    else if (tx > screenW - margin - arrowSize && ty > (screenH / 2 - arrowSize) && ty < (screenH / 2 + arrowSize))
    {
      // Right arrow touched: cycle to next file.
      selectedIndex++;
      if (selectedIndex >= fileCount)
        selectedIndex = 0;
      updateTitle();
      while (touchController.touches > 0)
      {
        touchController.read();
        delay(50);
      }
      delay(300);
    }
    // Check if touch is in the play button area.
    else if (tx >= playX && tx <= playX + playButtonSize &&
             ty >= playY && ty <= playY + playButtonSize)
    {
      // Build the full path and play the selected file.
      String fullPath = String(root) + String(AVI_FOLDER) + "/" + aviFileList[selectedIndex];
      char aviFilename[128];
      fullPath.toCharArray(aviFilename, sizeof(aviFilename));
      playAviFile(aviFilename);
      // Wait until the user fully releases the touch before refreshing the UI.
      waitForTouchRelease();

      // After playback, redisplay the selection screen.
      displaySelectedFile();
      while (touchController.touches > 0)
      {
        touchController.read();
        delay(50);
      }
      delay(300);
    }
  }
  delay(50);
}

// Continuously read until no touches are registered.
void waitForTouchRelease()
{
  while (touchController.touches > 0)
  {
    touchController.read();
    delay(50);
  }
  // Extra debounce delay to ensure that the touch state is fully cleared.
  delay(300);
}

// Update the avi title on the screen
void updateTitle() {
  // Clear the entire title area
  gfx->fillRect(0, TITLE_REGION_Y, TITLE_REGION_W, TITLE_REGION_H, RGB565_BLACK);
  
  // Retrieve the new title
  String title = aviFileList[selectedIndex];
  
  // Get text dimensions for the new title
  int16_t x1, y1;
  uint16_t textW, textH;
  gfx->getTextBounds(title.c_str(), 0, 0, &x1, &y1, &textW, &textH);
  
  // Center the text in the fixed title region:
  int titleX = (TITLE_REGION_W - textW) / 2 - x1;
  int titleY = TITLE_REGION_Y + (TITLE_REGION_H + textH) / 2;
  
  gfx->setCursor(titleX, titleY);
  gfx->print(title);
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
  File aviDir = SD_MMC.open(AVI_FOLDER);
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

// Display the selected avi file
void displaySelectedFile()
{
  // Clear the screen
  gfx->fillScreen(RGB565_BLACK);

  int screenW = gfx->width();
  int screenH = gfx->height();
  int centerY = screenH / 2;
  int arrowSize = 40; // size of the arrow icon (adjust as needed)
  int margin = 10;    // margin from screen edge

  // --- Draw Left Arrow ---
  // The left arrow is drawn as a filled triangle at the left side.
  gfx->fillTriangle(margin, centerY,
                    margin + arrowSize, centerY - arrowSize / 2,
                    margin + arrowSize, centerY + arrowSize / 2,
                    RGB565_WHITE);

  // --- Draw Right Arrow ---
  // Draw the right arrow as a filled triangle at the right side.
  gfx->fillTriangle(screenW - margin, centerY,
                    screenW - margin - arrowSize, centerY - arrowSize / 2,
                    screenW - margin - arrowSize, centerY + arrowSize / 2,
                    RGB565_WHITE);

  // --- Draw the Title ---
  // Get the file title string.
  String title = aviFileList[selectedIndex];
  int16_t x1, y1;
  uint16_t textW, textH;
  gfx->getTextBounds(title.c_str(), 0, 0, &x1, &y1, &textW, &textH);
  // Calculate x so the text is centered.
  int titleX = (screenW - textW) / 2 - x1;
  // Position the title above the play button; here we place it at roughly one-third of the screen height.
  int titleY = screenH / 3;
  gfx->setCursor(titleX, titleY);
  gfx->print(title);

  // --- Draw the Play Button ---
  // Define the play button size and location.
  int playButtonSize = 50;
  int playX = (screenW - playButtonSize) / 2;
  int playY = screenH - playButtonSize - 20; // 20 pixels from bottom
  // Draw a filled circle for the button background.
  gfx->fillCircle(playX + playButtonSize / 2, playY + playButtonSize / 2, playButtonSize / 2, RGB565_DARKGREEN);
  // Draw a play–icon (triangle) inside the circle.
  int triX = playX + playButtonSize / 2 - playButtonSize / 4;
  int triY = playY + playButtonSize / 2;
  gfx->fillTriangle(triX, triY - playButtonSize / 4,
                    triX, triY + playButtonSize / 4,
                    triX + playButtonSize / 2, triY,
                    RGB565_WHITE);
}