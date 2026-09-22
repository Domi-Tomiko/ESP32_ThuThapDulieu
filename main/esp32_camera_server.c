// #include <stdio.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_camera.h"
// #include "esp_log.h"

// static const char *TAG = "CAMERA";

// // ESP32-S3-N16R8 + OV3660
// #define PWDN_GPIO_NUM     -1
// #define RESET_GPIO_NUM    -1

// #define XCLK_GPIO_NUM     15
// #define SIOD_GPIO_NUM      4
// #define SIOC_GPIO_NUM      5

// #define Y9_GPIO_NUM       16
// #define Y8_GPIO_NUM       17
// #define Y7_GPIO_NUM       18
// #define Y6_GPIO_NUM       12
// #define Y5_GPIO_NUM       10
// #define Y4_GPIO_NUM        8
// #define Y3_GPIO_NUM        9
// #define Y2_GPIO_NUM       11

// #define VSYNC_GPIO_NUM     6
// #define HREF_GPIO_NUM      7
// #define PCLK_GPIO_NUM     13


// extern "C" void app_main(void)
// {
//     camera_config_t config = {};

//     config.ledc_channel = LEDC_CHANNEL_0;
//     config.ledc_timer   = LEDC_TIMER_0;

//     config.pin_d0 = Y2_GPIO_NUM;
//     config.pin_d1 = Y3_GPIO_NUM;
//     config.pin_d2 = Y4_GPIO_NUM;
//     config.pin_d3 = Y5_GPIO_NUM;
//     config.pin_d4 = Y6_GPIO_NUM;
//     config.pin_d5 = Y7_GPIO_NUM;
//     config.pin_d6 = Y8_GPIO_NUM;
//     config.pin_d7 = Y9_GPIO_NUM;

//     config.pin_xclk  = XCLK_GPIO_NUM;
//     config.pin_pclk  = PCLK_GPIO_NUM;
//     config.pin_vsync = VSYNC_GPIO_NUM;
//     config.pin_href  = HREF_GPIO_NUM;

//     config.pin_sccb_sda = SIOD_GPIO_NUM;
//     config.pin_sccb_scl = SIOC_GPIO_NUM;

//     config.pin_pwdn  = PWDN_GPIO_NUM;
//     config.pin_reset = RESET_GPIO_NUM;

//     config.xclk_freq_hz = 20000000;

//     config.pixel_format = PIXFORMAT_JPEG;
//     config.frame_size   = FRAMESIZE_VGA;

//     config.jpeg_quality = 12;

//     config.fb_count    = 2;
//     config.fb_location = CAMERA_FB_IN_PSRAM;
//     config.grab_mode   = CAMERA_GRAB_LATEST;

//     ESP_LOGI(TAG, "Initializing camera...");

//     esp_err_t err = esp_camera_init(&config);

//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Camera init FAILED! Error = 0x%x", err);
//         return;
//     }

//     ESP_LOGI(TAG, "Camera initialized successfully!");

//     while (true) {

//         camera_fb_t *fb = esp_camera_fb_get();

//         if (fb == NULL) {
//             ESP_LOGE(TAG, "Capture FAILED!");
//         }
//         else {
//             ESP_LOGI(
//                 TAG,
//                 "Capture OK: %ux%u, %u bytes, format=%d",
//                 fb->width,
//                 fb->height,
//                 fb->len,
//                 fb->format
//             );

//             esp_camera_fb_return(fb);
//         }

//         vTaskDelay(pdMS_TO_TICKS(3000));
//     }
// }








// #include <stdio.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_log.h"

// static const char *TAG = "TEST";

// extern "C" void app_main(void)
// {
//     ESP_LOGI(TAG, "=================================");
//     ESP_LOGI(TAG, "app_main() START");
//     ESP_LOGI(TAG, "ESP32-S3 is running!");
//     ESP_LOGI(TAG, "=================================");

//     while (true)
//     {
//         ESP_LOGI(TAG, "Program is still running...");
//         vTaskDelay(pdMS_TO_TICKS(2000));
//     }
// }