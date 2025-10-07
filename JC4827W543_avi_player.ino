// Tutorial : https://youtu.be/mnOzfRFQJIM
// AVI Player for the JC4827W543 development board
// Code adapted from moononournation (https://github.com/moononournation/aviPlayer)
//
// Use board "ESP32S3 Dev Module" (last tested on v3.2.0)
//
// Libraries that you need to install as Zip in the IDE:
// avilib: https://github.com/lanyou1900/avilib.git install as zip in the Arduino IDE
//
const char *AVI_FOLDER = "/avi";
size_t output_buf_size;
uint16_t *output_buf;


// Seamless loop mode: loop a single AVI without closing/reopening to avoid black flash
const bool SEAMLESS_LOOP = true;

#include <PINS_JC4827W543.h>    // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.5.6)
                                // Install "Dev Device Pins" with the Library Manager (last tested on v0.0.2)
#include <SD.h>                 // Included with the Espressif Arduino Core (last tested on v3.2.0)
#include "AviFunc.h"            // Included in this project

static SPIClass spiSD{HSPI};
const char *sdMountPoint = "/sdcard"; 


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
    
    // Auto-play the first AVI file found
    File aviDir = SD.open(AVI_FOLDER);
    if (!aviDir)
    {
      Serial.println("Failed to open AVI folder");
      return;
    }
    
    String firstAviFile = "";
    while (true)
    {
      File file = aviDir.openNextFile();
      if (!file) break;
      
      if (!file.isDirectory())
      {
        String name = file.name();
        // Skip macOS resource fork files and hidden files
        if (!name.startsWith("._") && !name.startsWith(".") && 
            (name.endsWith(".avi") || name.endsWith(".AVI")) && file.size() > 0)
        {
          firstAviFile = name;
          file.close();
          break;
        }
      }
      file.close();
    }
    aviDir.close();
    
    if (firstAviFile.length() == 0)
    {
      Serial.println("No AVI files found in /avi");
      return;
    }
    
    // Play the first AVI file in seamless loop
    String fullPath = String(sdMountPoint) + String(AVI_FOLDER) + "/" + firstAviFile;
    char aviFilename[128];
    fullPath.toCharArray(aviFilename, sizeof(aviFilename));
    playAviFile(aviFilename); // This call will not return in seamless mode
  }
}

void loop()
{
  // Empty loop - video playback runs in seamless mode from setup()
  delay(1000);
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


    avi_start_ms = millis();

    Serial.println("Start play loop");
    bool first_loop = true;
    while (true)
    {
      while (avi_curr_frame < avi_total_frames)
      {
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
