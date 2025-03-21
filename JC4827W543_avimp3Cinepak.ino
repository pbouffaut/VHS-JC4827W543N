/*******************************************************************************
 * AVI Player example
 *
 * Dependent libraries:
 * Arduino_GFX: https://github.com/moononournation/Arduino_GFX.git
 * avilib: https://github.com/lanyou1900/avilib.git
 * libhelix: https://github.com/pschatzmann/arduino-libhelix.git
 *
 * Setup steps:
 * 1. Change your LCD parameters in Arduino_GFX setting
 * 2. Upload AVI file
 *   FFat/LittleFS:
 *     upload FFat (FatFS) data with ESP32 Sketch Data Upload:
 *     ESP32: https://github.com/lorol/arduino-esp32fs-plugin
 *   SD:
 *     Copy files to SD card
 ******************************************************************************/
const char *root = "/root";
char *avi_filename = (char *)"/root/andor_24fps_v10.avi";
char *avi_filename2 = (char *)"/root/andor_24fps_v20.avi";

#include <PINS_JC4827W543.h> // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.5)

// Touch Controller
#include "TAMC_GT911.h" // Install "TAMC_GT911" with the Library Manager (last tested on v1.0.2)
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38
#define TOUCH_WIDTH 480
#define TOUCH_HEIGHT 272
// Enum for swipe types.
enum SwipeType
{
  NO_SWIPE,
  SWIPE_RIGHT_TO_LEFT,
  SWIPE_LEFT_TO_RIGHT
};

TAMC_GT911 touchController = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

#include <SD.h>
#include <SD_MMC.h>

size_t output_buf_size;
uint16_t *output_buf;

#include "AviFunc.h"
#include "esp32_audio.h"

void setup()
{
  Serial.begin(115200);
  if (!gfx->begin())
  {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);
  touchController.begin();

#ifdef GFX_BL
  // Set the backlight of the screen to High intensity
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

#ifdef AUDIO_EXTRA_PRE_INIT
  AUDIO_EXTRA_PRE_INIT();
#endif

  i2s_init();

#ifdef AUDIO_MUTE
  pinMode(AUDIO_MUTE, OUTPUT);
  digitalWrite(AUDIO_MUTE, HIGH);
#endif

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
#if defined(RGB_PANEL) | defined(DSI_PANEL)
    output_buf = gfx->getFramebuffer();
#else
    output_buf = (uint16_t *)aligned_alloc(16, output_buf_size);
#endif
    if (!output_buf)
    {
      Serial.println("output_buf aligned_alloc failed!");
    }

    avi_init();

    delay(2000);
    listDir(SD, "/", 10); // 10 levels deep recursion
  }
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.printf("  DIR : %s\n", file.name());
      if (levels)
      {
        listDir(fs, file.name(), levels - 1);
      }
    }
    else
    {
      Serial.printf("  FILE: %s  SIZE: %d bytes\n", file.name(), file.size());
    }
    file = root.openNextFile();
  }
}

void loop()
{
  playAviFile(avi_filename);
  delay(5000);
  playAviFile(avi_filename2);
  delay(5000);
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

    // avi_show_stat();
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