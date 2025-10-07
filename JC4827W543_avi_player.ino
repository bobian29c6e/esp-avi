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

// Global switch: set to false to disable audio playback (video-only for smoother FPS)
const bool ENABLE_AUDIO = false;
// Seamless loop mode: loop a single AVI without closing/reopening to avoid black flash
const bool SEAMLESS_LOOP = true;

#include <PINS_JC4827W543.h>    // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.6)
                                // Install "Dev Device Pins" with the Library Manager (last tested on v0.0.2)
#include "TAMC_GT911.h"         // Install "TAMC_GT911" with the Library Manager (last tested on v1.0.2)
#include <SD.h>                 // Included with the Espressif Arduino Core (last tested on v3.2.0)
#include "AviFunc.h"            // Included in this project
#include "esp32_audio.h"        // Included in this project
#include "FreeSansBold12pt7b.h" // Included in this project

static SPIClass spiSD{HSPI};
const char *sdMountPoint = "/sdcard"; 

#define TITLE_REGION_Y (gfx->height() / 3 - 30)
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
    // No touch device: auto-play all AVI files in a loop
    if (fileCount <= 0)
    {
      Serial.println("No AVI files found in /avi");
      return;
    }

    // Auto-play loop
    if (SEAMLESS_LOOP)
    {
      // Play the first AVI continuously without black flash between loops
      String fullPath = String(sdMountPoint) + String(AVI_FOLDER) + "/" + aviFileList[0];
      char aviFilename[128];
      fullPath.toCharArray(aviFilename, sizeof(aviFilename));
      playAviFile(aviFilename); // This call will not return in seamless mode
    }
    else
    {
      while (true)
      {
        for (int i = 0; i < fileCount; ++i)
        {
          String fullPath = String(sdMountPoint) + String(AVI_FOLDER) + "/" + aviFileList[i];
          char aviFilename[128];
          fullPath.toCharArray(aviFilename, sizeof(aviFilename));
          playAviFile(aviFilename);
          // small gap between files
          delay(200);
        }
      }
    }
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
      String fullPath = String(sdMountPoint) + String(AVI_FOLDER) + "/" + aviFileList[selectedIndex];
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
void updateTitle()
{
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
    // For seamless loop we avoid clearing the screen to prevent a black flash
    if (!SEAMLESS_LOOP)
    {
      gfx->fillScreen(BLACK);
    }
    // Sanity checks to avoid bogus files (e.g. macOS resource forks ._*)
    if (avi_total_frames <= 0 || avi_w <= 0 || avi_h <= 0 || avi_fr <= 0)
    {
      Serial.println("Invalid AVI metadata, skip file");
      avi_close();
      return;
    }
    // Only support Cinepak (and optional MJPEG if compiled)
    bool v_ok = false;
#ifdef AVI_SUPPORT_CINEPAK
    v_ok = v_ok || (avi_vcodec == CINEPAK_CODEC_CODE);
#endif
#ifdef AVI_SUPPORT_MJPEG
    v_ok = v_ok || (avi_vcodec == MJPEG_CODEC_CODE);
#endif
    if (!v_ok)
    {
      Serial.println("Unsupported video codec, skip file");
      avi_close();
      return;
    }

    // Audio init based on format (only if enabled)
    if (ENABLE_AUDIO)
    {
      if (avi_aRate > 8000 && avi_aRate <= 192000)
      {
        i2s_set_sample_rate(avi_aRate);
      }
      else
      {
        Serial.printf("Invalid audio sample rate: %ld, force skip audio for this file\n", avi_aRate);
      }
      avi_feed_audio();
    }

    // Start audio task depending on audio codec (only if enabled)
    bool started_audio = false;
    if (ENABLE_AUDIO)
    {
      if (avi_aFormat == 85) // MP3
      {
        Serial.println("Start play audio task (MP3)");
        BaseType_t ret_val = mp3_player_task_start();
        if (ret_val != pdPASS)
        {
          Serial.printf("mp3_player_task_start failed: %d\n", ret_val);
        }
        else
        {
          started_audio = true;
        }
      }
      else if (avi_aFormat == 1 && avi_aBits == 8) // PCM_U8
      {
        Serial.println("Start play audio task (PCM_U8)");
        BaseType_t ret_val = pcm_player_task_start();
        if (ret_val != pdPASS)
        {
          Serial.printf("pcm_player_task_start failed: %d\n", ret_val);
        }
        else
        {
          started_audio = true;
        }
      }
      else
      {
        Serial.printf("Unsupported audio format: format=%ld bits=%ld, continue without audio\n", avi_aFormat, avi_aBits);
      }
    }

    avi_start_ms = millis();

    Serial.println("Start play loop");
    bool first_loop = true;
    while (true)
    {
      while (avi_curr_frame < avi_total_frames)
      {
        if (ENABLE_AUDIO)
        {
          avi_feed_audio();
        }
        if (avi_decode())
        {
          avi_draw(0, 0);
        }
      }

      if (SEAMLESS_LOOP)
      {
        // Reset timing and frame index for seamless looping without clearing screen
        avi_curr_frame = 0;
        avi_skipped_frames = 0;
        avi_total_read_video_ms = 0;
        avi_total_decode_video_ms = 0;
        avi_total_show_video_ms = 0;
        avi_start_ms = millis();
        first_loop = false;
        continue; // decode the same file again without closing
      }
      else
      {
        break; // exit when not in seamless mode
      }
    }

    // Normal (non-seamless) close and stats
    if (!SEAMLESS_LOOP)
    {
      avi_close();
      Serial.println("AVI end");
      avi_show_stat();
    }
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
      // Skip macOS resource fork files and hidden files
      if (name.startsWith("._") || name.startsWith("."))
      {
        file.close();
        continue;
      }
      if ((name.endsWith(".avi") || name.endsWith(".AVI")) && file.size() > 0)
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