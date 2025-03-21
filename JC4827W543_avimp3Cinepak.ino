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
// const char *root = "/root";
const char *root = "/root";
char *avi_filename = (char *)"/root/andor.avi";
// char *avi_filename = (char *)"/root/AviMp3Cinepak272p30fps.avi";

// Dev Device Pins: <https://github.com/moononournation/Dev_Device_Pins.git>
#include <PINS_JC4827W543.h> // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.5)

// #include <FFat.h>
// #include <LittleFS.h>
// #include <SPIFFS.h>
#include <SD.h>
#include <SD_MMC.h>

size_t output_buf_size;
uint16_t *output_buf;

#include "AviFunc.h"
#include "esp32_audio.h"

void setup()
{
  // #ifdef DEV_DEVICE_INIT
  //   DEV_DEVICE_INIT();
  // #endif

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial);
  // Serial.println("AviPcmu8Mjpeg");

  // If display and SD shared same interface, init SPI first
  // #ifdef SPI_SCK
  //   SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  // #endif

  // Init Display
  // if (!gfx->begin())
  if (!gfx->begin())
  {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

#ifdef GFX_BL
  // Set the backlight of the screen to High intensity
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  // gfx->setTextColor(WHITE, BLACK);
  // gfx->setTextBound(60, 60, 240, 240);

#ifdef AUDIO_EXTRA_PRE_INIT
  AUDIO_EXTRA_PRE_INIT();
#endif

  i2s_init();

#ifdef AUDIO_MUTE
  pinMode(AUDIO_MUTE, OUTPUT);
  digitalWrite(AUDIO_MUTE, HIGH);
#endif

// #if defined(SD_D1)
//   SD_MMC.setPins(SD_SCK, SD_MOSI /* CMD */, SD_MISO /* D0 */, SD_D1, SD_D2, SD_CS /* D3 */);
//   if (!SD_MMC.begin(root, false /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_HIGHSPEED))
// #elif defined(SD_SCK)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SD_MMC.setPins(SD_SCK, SD_MOSI /* CMD */, SD_MISO /* D0 */);
  if (!SD_MMC.begin(root, true /* mode1bit */, false /* format_if_mount_failed */, SDMMC_FREQ_DEFAULT))
// #elif defined(SD_CS)
//   if (!SD.begin(SD_CS, SPI, 80000000, "/root"))
// #else
  // if (!SD.begin())
  // if (!LittleFS.begin(false, root))
  // if (!SPIFFS.begin(false, root))
// #endif
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
    listDir(SD, "/", 10);  // 10 levels deep recursion
  }
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
      Serial.println("Failed to open directory");
      return;
  }
  if (!root.isDirectory()) {
      Serial.println("Not a directory");
      return;
  }

  File file = root.openNextFile();
  while (file) {
      if (file.isDirectory()) {
          Serial.printf("  DIR : %s\n", file.name());
          if (levels) {
              listDir(fs, file.name(), levels - 1);
          }
      } else {
          Serial.printf("  FILE: %s  SIZE: %d bytes\n", file.name(), file.size());
      }
      file = root.openNextFile();
  }
}

void loop()
{
  if (avi_open(avi_filename))
  {
    Serial.println("AVI start");
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
  } else {
    Serial.println(AVI_strerror());
  }

  delay(5000);
}
