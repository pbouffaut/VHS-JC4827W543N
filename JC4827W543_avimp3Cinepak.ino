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
#define ITEM_HEIGHT 70
#define ITEM_MARGIN 10

String aviFileList[MAX_FILES];
int fileCount = 0;

#include <PINS_JC4827W543.h>    // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.6)
#include "TAMC_GT911.h"         // Install "TAMC_GT911" with the Library Manager (last tested on v1.0.2)
#include <SD.h>                 // Included with the Espressif Arduino Core (last tested on v3.2.0)
#include <SD_MMC.h>             // Included with the Espressif Arduino Core (last tested on v3.2.0)
#include "AviFunc.h"            // Included in this project
#include "esp32_audio.h"        // Included in this project
#include "FreeSansBold12pt7b.h" // Included in this project

// Touch Controller
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38
#define TOUCH_WIDTH 480
#define TOUCH_HEIGHT 272
TAMC_GT911 touchController = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

// Enum for swipe types.
enum SwipeType
{
  NO_SWIPE,
  SWIPE_RIGHT_TO_LEFT,
  SWIPE_LEFT_TO_RIGHT
};

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
    displayFileList();
  }
}

void loop()
{
  touchController.read();
  if (touchController.touches > 0)
  {
    Serial.printf("Touch detected: %d, (%d, %d)\n", touchController.touches, touchController.points[0].x, touchController.points[0].y);
    int touchY = touchController.points[0].y;
    int selectedIndex = touchY / ITEM_HEIGHT;
    if (selectedIndex >= 0 && selectedIndex < fileCount)
    {
      int rectX = ITEM_MARGIN;
      int rectY = selectedIndex * ITEM_HEIGHT + ITEM_MARGIN;
      int rectW = gfx->width() - 2 * ITEM_MARGIN;
      int rectH = ITEM_HEIGHT - 2 * ITEM_MARGIN;
      // Highlight the selected rectangle with a blue background:
      gfx->fillRect(rectX, rectY, rectW, rectH, RGB565_BLUE);
      gfx->drawRect(rectX, rectY, rectW, rectH, RGB565_WHITE);

      // Center the text within the highlighted rectangle:
      int16_t x1, y1;
      uint16_t textW, textH;
      gfx->getTextBounds(aviFileList[selectedIndex].c_str(), 0, 0, &x1, &y1, &textW, &textH);
      int textX = rectX + (rectW - textW) / 2 - x1;
      int textY = rectY + (rectH - textH) / 2 + textH;
      gfx->setCursor(textX, textY);
      gfx->print(aviFileList[selectedIndex]);
      delay(500); // Debounce delay

      // Build full file path and play the selected AVI:
      String fullPath = String(root) + String(AVI_FOLDER) + "/" + aviFileList[selectedIndex];
      char aviFilename[128];
      fullPath.toCharArray(aviFilename, sizeof(aviFilename));
      playAviFile(aviFilename);

      // Wait until the touch is released to avoid re-triggering playback
      do
      {
        touchController.read();
        delay(50);
      } while (touchController.touches > 0);

      // Redisplay the file list UI after playback:
      displayFileList();
    }
  }
  delay(50);
}

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

// Touch functions

/// @brief Detects if a swipe gesture occurred and returns its type.
/// @return SwipeType indicating the detected swipe (or NO_SWIPE if none occurred).
SwipeType detectSwipe()
{
  // Note: touchController.read() must be called before this function.
  if (detectRightToLeftGesture_NoRead())
  {
    return SWIPE_RIGHT_TO_LEFT;
  }
  else if (detectLeftToRightGesture_NoRead())
  {
    return SWIPE_LEFT_TO_RIGHT;
  }
  return NO_SWIPE;
}

/// @brief Checks for a right-to-left swipe gesture using the current touch data.
/// @return true if a right-to-left swipe is detected.
bool detectRightToLeftGesture_NoRead()
{
  const int swipeThreshold = 50; // Minimum horizontal movement (in pixels) to qualify as a swipe
  static bool tracking = false;  // Indicates if a gesture is being tracked
  static int startX = 0;         // x coordinate when the touch began
  static int lastX = 0;          // Most recent x coordinate during the touch

  // Use the touch state that was updated in loop()
  if (touchController.isTouched)
  {
    int currentX = touchController.points[0].x;
    if (!tracking)
    {
      tracking = true;
      startX = currentX;
    }
    lastX = currentX;
  }
  else
  {
    if (tracking)
    {
      int deltaX = startX - lastX; // Positive if moved from right to left
      tracking = false;
      if (deltaX > swipeThreshold)
      {
        return true;
      }
    }
  }
  return false;
}

/// @brief Checks for a left-to-right swipe gesture using the current touch data.
/// @return true if a left-to-right swipe is detected.
bool detectLeftToRightGesture_NoRead()
{
  const int swipeThreshold = 50; // Minimum horizontal movement (in pixels) to qualify as a swipe
  static bool tracking = false;  // Indicates if a gesture is being tracked
  static int startX = 0;         // x coordinate when the touch began
  static int lastX = 0;          // Most recent x coordinate during the touch

  // Use the touch state that was updated in loop()
  if (touchController.isTouched)
  {
    int currentX = touchController.points[0].x;
    if (!tracking)
    {
      tracking = true;
      startX = currentX;
    }
    lastX = currentX;
  }
  else
  {
    if (tracking)
    {
      int deltaX = lastX - startX; // Positive if moved from left to right
      tracking = false;
      if (deltaX > swipeThreshold)
      {
        return true;
      }
    }
  }
  return false;
}

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

void displayFileList()
{
  gfx->fillScreen(RGB565_BLACK);
  for (int i = 0; i < fileCount; i++)
  {
    int y = i * ITEM_HEIGHT;
    int rectX = ITEM_MARGIN;
    int rectY = y + ITEM_MARGIN;
    int rectW = gfx->width() - 2 * ITEM_MARGIN;
    int rectH = ITEM_HEIGHT - 2 * ITEM_MARGIN;
    // Fill the rectangle with a nice background color:
    gfx->fillRect(rectX, rectY, rectW, rectH, RGB565_DARKCYAN);
    // Draw a white border around the rectangle:
    gfx->drawRect(rectX, rectY, rectW, rectH, RGB565_WHITE);

    // Center the AVI filename in the rectangle:
    int16_t x1, y1;
    uint16_t textW, textH;
    gfx->getTextBounds(aviFileList[i].c_str(), 0, 0, &x1, &y1, &textW, &textH);
    int textX = rectX + (rectW - textW) / 2 - x1;
    int textY = rectY + (rectH - textH) / 2 + textH;
    gfx->setCursor(textX, textY);
    gfx->print(aviFileList[i]);
  }
}