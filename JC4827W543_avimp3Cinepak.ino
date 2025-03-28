// AVI Player for the JC4827W543 development board 
// Code adapted from moononournation (https://github.com/moononournation/aviPlayer)
//
// Dependent libraries:
// "GFX Library for Arduino" install with the Library Manager (last tested on v1.5.6)
// avilib: https://github.com/lanyou1900/avilib.git install as zip in the Arduino IDE
// libhelix: https://github.com/pschatzmann/arduino-libhelix.git install as zip in the Arduino IDE
//
const char *root = "/root";
const char *AVI_FOLDER = "/avi";
size_t output_buf_size;
uint16_t *output_buf;

#include <PINS_JC4827W543.h> // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.6)
#include "TAMC_GT911.h" // Install "TAMC_GT911" with the Library Manager (last tested on v1.0.2)
#include <SD.h>
#include <SD_MMC.h>
#include "AviFunc.h"
#include "esp32_audio.h"

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
  }
  gfx->fillScreen(RGB565_BLACK);
  touchController.begin();

  // Set the backlight of the screen to High intensity
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  i2s_init();

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SD_MMC.setPins(SD_SCK, SD_MOSI /* CMD */, SD_MISO /* D0 */);
  if (!SD_MMC.begin(root, true /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_DEFAULT))
  {
    Serial.println("ERROR: SD Card mount failed!");
  }
  else
  {
    output_buf_size = gfx->width() * gfx->height() * 2;
    output_buf = (uint16_t *)aligned_alloc(16, output_buf_size);
    if (!output_buf)
    {
      Serial.println("output_buf aligned_alloc failed!");
    }

    avi_init();
  }
}

void loop() {
  File aviDir = SD_MMC.open(AVI_FOLDER);
  if (!aviDir) {
    Serial.println("Failed to open AVI folder");
    delay(1000);
    return;
  }
  while (true) {
    File file = aviDir.openNextFile();
    if (!file) {
      aviDir.rewindDirectory();
      break;
    }
    if (!file.isDirectory()) {
      String fileName = file.name();
      if (fileName.endsWith(".avi") || fileName.endsWith(".AVI")) {
        String fullPath = String(root) + String(AVI_FOLDER) + "/" + fileName;
        Serial.printf("Playing file: %s\n", fullPath.c_str());
        char aviFilename[128];
        fullPath.toCharArray(aviFilename, sizeof(aviFilename));
        playAviFile(aviFilename);
        delay(5000);
      }
    }
    file.close();
  }
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