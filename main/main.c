#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"


// unicast wifi
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "unicast.h"


// Camera
#include "esp_camera.h"

// SD Card
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"


// NEO-6 GPS defs
#define GPS_TASK_STACK_SIZE (4096)  // Increased stack size
#define GPS_TASK_PRIORITY    (5)    // Medium priority


// Pin definition for CAMERA_MODEL_AI_THINKER
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 // NC
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5 // D0 is different on some boards, check yours
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

// SD Card Mount point
#define MOUNT_POINT "/sdcard"

static const char *TAG = "MAIN";

// SD Card Globals
static sdmmc_card_t *card;
static const char *mount_point = MOUNT_POINT;

// --- Camera Initialization ---
static esp_err_t init_camera() {
    camera_config_t camera_config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        
        //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG, 
        // PIXFORMAT_RGB565  |   PIXFORMAT_JPEG
        .frame_size = FRAMESIZE_UXGA,    // 1600x1200 resolution
                                        // Can try FRAMESIZE_SXGA (1280x1024), FRAMESIZE_XGA (1024x768) etc.
                                        // Ensure PSRAM is enabled for larger resolutions!
        // FRAMESIZE_QVGA   |  FRAMESIZE_UXGA
        .jpeg_quality = 12, // 0-63 lower number means higher quality
        .fb_count = 1,       // If more than one, i2s runs in continuous mode.
        .fb_location = CAMERA_FB_IN_PSRAM, // Store framebuffer in PSRAM for larger images
        .grab_mode = CAMERA_GRAB_LATEST   // Grabs the latest frame
        //CAMERA_GRAB_WHEN_EMPTY   |  CAMERA_GRAB_LATEST 
        };

    // Initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "Camera Initialized");

    // Optional: Get sensor object and configure settings if needed
    // sensor_t * s = esp_camera_sensor_get();
    // s->set_vflip(s, 1); // flip it back
    // s->set_brightness(s, 1); // up the brightness
    // s->set_saturation(s, -2); // lower the saturation

    return ESP_OK;
}

// --- SD Card Initialization ---
static esp_err_t init_sdcard() {
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // Don't format if mount fails initially
        .max_files = 5,                 // Max number of open files
        .allocation_unit_size = 16 * 1024 // Allocation unit size
    };

    ESP_LOGI(TAG, "Initializing SD card using SDMMC peripheral");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // Use slot 1 for 4-bit SDMMC mode
    host.slot = SDMMC_HOST_SLOT_1;
    // Don't use internal pullups if external ones are present on the board
    // host.flags |= SDMMC_HOST_FLAG_PULLUP_DISABLE;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Set width to 4 lines (ensure pins D0-D3 are connected and configured in menuconfig)
    // If you have issues, try setting width to 1 and only connecting CMD, CLK, D0
    slot_config.width = 4;

    // On ESP32-CAM, SDMMC peripheral is usually connected to GPOs:
    // CMD: 15, CLK: 14, D0: 2, D1: 4, D2: 12, D3: 13
    // These are usually configured by default if you enable SDMMC Host in menuconfig.
    // If your board is different, override pins here:
    // slot_config.clk = GPIO_NUM_14;
    // slot_config.cmd = GPIO_NUM_15;
    // slot_config.d0 = GPIO_NUM_2;
    // slot_config.d1 = GPIO_NUM_4; // Check if used by other peripherals (e.g., Flash LED)
    // slot_config.d2 = GPIO_NUM_12;
    // slot_config.d3 = GPIO_NUM_13;

    // Enable internal pullups on CMD, D0-D3 (not CLK)
    // Recommended for most boards unless external pullups are definitely present
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    

    ESP_LOGI(TAG, "before esp_vfs_fat_sdmmc_mount");
    // Mount the filesystem
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    ESP_LOGI(TAG, "after esp vfs fat sdmmc mount");

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else if (ret == ESP_ERR_NO_MEM) {
             ESP_LOGE(TAG, "Failed to mount filesystem. Not enough memory.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors.", esp_err_to_name(ret));
        }
        return ret;
    }
    ESP_LOGI(TAG, "SDCard mounted at %s", mount_point);

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}

// --- Capture Image and Save ---
static esp_err_t capture_and_save_image() {
    ESP_LOGI(TAG, "Taking picture...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera Frame Buffer Get Failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Picture taken! Frame size: %u bytes", fb->len);

    // --- Create unique filename ---
    char file_path[64];
    struct stat st;
    int counter = 0;
    // Try to find a filename that doesn't exist yet
    do {
        sprintf(file_path, "%s/image%03d.jpg", mount_point, counter++);
    } while (stat(file_path, &st) == 0 && counter < 1000);

    if (counter >= 1000) {
         ESP_LOGE(TAG, "Could not find unique filename after 1000 tries.");
         esp_camera_fb_return(fb); // IMPORTANT: return frame buffer
         return ESP_FAIL;
    }
    //vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Saving file to: %s", file_path);


    FILE *file = fopen(file_path, "w"); // Open file for writing
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        esp_camera_fb_return(fb); // IMPORTANT: return frame buffer
        return ESP_FAIL;
    }

    // Write picture data
    size_t written = fwrite(fb->buf, 1, fb->len, file);
    if (written != fb->len) {
         ESP_LOGE(TAG, "Error writing file (written %d, expected %d)", written, fb->len);
         fclose(file); // Close the file even on error
         esp_camera_fb_return(fb); // IMPORTANT: return frame buffer
         return ESP_FAIL;
    }

    // Close the file
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Failed to close file");
         esp_camera_fb_return(fb); // IMPORTANT: return frame buffer
        return ESP_FAIL; // Indicate error on close failure
    }

    ESP_LOGI(TAG, "File saved successfully: %s (%d bytes)", file_path, fb->len);

    // VERY IMPORTANT: Return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);

    return ESP_OK;
}


void app_main(void)
{
    printf("   ✴ ▓ ✴ <<<MADMANINDUSTRIES>>> ✴ ▓ ✴          \n");
    printf(
    "              \\__/_‾__‾‾_‾\\__/   // M\n"
    "              \\__/_‾______\\__/   // A\n"
    "              \\__/_‾__‾___\\__/   // D\n"
    "              \\__/_‾__‾‾_‾\\__/   // M\n"
    "              \\__/_‾______\\__/   // A\n"
    "              \\__/_‾__‾‾‾_\\__/   // N\n"
    "              \\__/_‾__‾‾__\\__/   // I\n"
    "              \\__/_‾__‾‾‾_\\__/   // N\n"
    "              \\__/_‾__‾___\\__/   // D\n"
    "              \\__/‾_‾‾_‾‾_\\__/   // U\n"
    "              \\__/‾_‾‾__‾_\\__/   // S\n"
    "              \\__/‾_‾‾_‾__\\__/   // T\n"
    "              \\__/‾_‾‾__‾_\\__/   // R\n"
    "              \\__/_‾__‾‾__\\__/   // I\n"
    "              \\__/_‾__‾___\\__/   // E\n"
    "              \\__/‾_‾‾__‾_\\__/   // S\n"
    );

    
    // Print reset reason
    esp_reset_reason_t reason = esp_reset_reason();
    printf("Reset reason: %d\n", reason);   


    esp_err_t ret;
    ESP_LOGI(TAG, "MAIN STARTED");
    

    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize SD Card
    ret = init_sdcard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD Card. Halting.");
        return; // Halt if SD card fails
    }

    

    // Create received data log file "receiveddata_HHMM.txt" at boot
    {
        time_t now = time(NULL);
        struct tm tm_info = {0};
        char hhmm[8] = "0000";
        if (now > 1600000000) {
            localtime_r(&now, &tm_info);
            strftime(hhmm, sizeof(hhmm), "%H%M", &tm_info);
        } else {
            // fallback to uptime minutes:seconds if RTC not set
            int64_t us = esp_timer_get_time();
            uint32_t sec = (uint32_t)(us / 1000000);
            snprintf(hhmm, sizeof hhmm, "%02u%02u", (unsigned)((sec / 3600) % 24), (unsigned)((sec / 60) % 60));

        }

        char log_path[96];
        snprintf(log_path, sizeof log_path, "%s/receiveddata_%s.txt", mount_point, hhmm);

        FILE *f = fopen(log_path, "w");
        if (f) {
            fprintf(f, "Boot log started\n");
            fclose(f);
            ESP_LOGI(TAG, "Created receive log: %s", log_path);
        } else {
            ESP_LOGE(TAG, "Failed to create receive log file");
        }

        // Tell unicast where to append samples
        example_espnow_set_log_file(log_path);
    }

    // Initialize WiFi and ESPNOW
    example_wifi_init();
    example_espnow_init();



    


    //vTaskDelay(pdMS_TO_TICKS(3000));


    // Initialize Camera
    ret = init_camera();
    if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Failed to initialize Camera. Halting.");
        // Optional: Cleanup SD card before halting
        // esp_vfs_fat_sdcard_unmount(mount_point, card);
        // sdmmc_host_deinit();
        return; // Halt if camera fails
    }

    // Wait a bit for camera sensor to stabilize (optional)
    vTaskDelay(pdMS_TO_TICKS(3000));



    

    // ---  Loop to capture images every N seconds ---

    while(1) {
        ESP_LOGI(TAG,"--- Taking photo in loop ---");
        ret = capture_and_save_image();
        if (ret != ESP_OK) {
             ESP_LOGE(TAG, "Failed to capture and save image in loop.");
             // Decide how to handle errors in loop (retry, stop, etc.)
        }
        ESP_LOGI(TAG,"Waiting 1 seconds...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 seconds
    }


    // --- Cleanup (if not looping forever) ---
    ESP_LOGI(TAG, "Unmounting SD card...");
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "SD card unmounted");

    // Deinitialize SDMMC host peripheral
    // Note: Check if sdmmc_host_deinit() exists and is needed in your IDF version.
    // It might be handled implicitly by unmount in newer versions.
    // ret = sdmmc_host_deinit();
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to deinitialize SDMMC host");
    // }


    
    ESP_LOGI(TAG, "success!!!");

}
