// name - pass wifi
#define WIFI_SSID_tmp     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD_tmp "YOUR_WIFI_PASSWORD"

// main
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"

#include "esp_littlefs.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "esp_http_server.h"

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static esp_err_t send_file(
    httpd_req_t *req,
    const char *filepath,
    const char *content_type
);

static esp_err_t capture_handler(
    httpd_req_t *req
);

static bool is_photo_filename(
    const char *name
);

static bool is_photo_prefix(
    const char *prefix
);

static esp_err_t photos_handler(
    httpd_req_t *req
);

static esp_err_t delete_photos_handler(
    httpd_req_t *req
);

static esp_err_t photo_handler(
    httpd_req_t *req
);

static esp_err_t download_handler(
    httpd_req_t *req
);

extern const uint8_t index_html_start[]
    asm("_binary_index_html_start");

extern const uint8_t index_html_end[]
    asm("_binary_index_html_end");

extern const uint8_t style_css_start[]
    asm("_binary_style_css_start");

extern const uint8_t style_css_end[]
    asm("_binary_style_css_end");

extern const uint8_t app_js_start[]
    asm("_binary_app_js_start");

extern const uint8_t app_js_end[]
    asm("_binary_app_js_end");

static const char *TAG = "STORAGE";
#define WIFI_SSID     WIFI_SSID_tmp
#define WIFI_PASSWORD WIFI_PASSWORD_tmp

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0

static const char *WIFI_TAG = "WIFI";
static uint8_t s_zip_buffer[8192];

/* =========================================================
 * OV3660 CAMERA PIN CONFIG
 * ========================================================= */

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1

#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11

#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM      13

#define SD_MOUNT_POINT "/sdcard"
#define SD_CMD_GPIO  38
#define SD_CLK_GPIO  39
#define SD_D0_GPIO   40

static sdmmc_card_t *s_sd_card = NULL;

// Prototype
static esp_err_t download_all_handler(httpd_req_t *req);
static esp_err_t create_sd_photos_directory(void);
static esp_err_t sdcard_capture_test(void);
static esp_err_t capture_and_save(const char *requested_name);
static int find_next_sd_image_number(void);

typedef struct
{
    char filename[64];

    uint32_t crc32;

    uint32_t size;

    uint32_t offset;

    uint16_t name_length;

} zip_file_info_t;

#define ZIP_METADATA_FILE \
    SD_MOUNT_POINT "/zip_metadata.bin"

/* =========================================================
 * CAMERA INITIALIZATION
 * ========================================================= */
static esp_err_t camera_init(void)
{
    camera_config_t config = {0};

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;

    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;

    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = 20000000;

    config.pixel_format = PIXFORMAT_JPEG;

    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;

    /*
     * Chá»¥p -> lÆ°u -> chá»¥p tiáº¿p.
     * TrÃ¡nh FB-OVF Ä‘Ã£ xuáº¥t hiá»‡n á»Ÿ test trÆ°á»›c.
     */
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    ESP_LOGI(TAG, "Initializing OV3660...");

    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Camera initialization failed: 0x%x",
                 err);

        return err;
    }

    sensor_t *sensor = esp_camera_sensor_get();

    if (sensor != NULL)
    {
        ESP_LOGI(TAG,
                 "Camera PID: 0x%02X",
                 sensor->id.PID);
    }

    ESP_LOGI(TAG, "Camera initialized successfully");

    return ESP_OK;
}


/* =========================================================
 * LITTLEFS INITIALIZATION
 * ========================================================= */

static esp_err_t littlefs_init(void)
{
    ESP_LOGI(TAG, "Mounting LittleFS...");

    esp_vfs_littlefs_conf_t conf = {0};

    conf.base_path = "/littlefs";
    conf.partition_label = "storage";
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    esp_err_t err = esp_vfs_littlefs_register(&conf);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "LittleFS mount failed: 0x%x",
                 err);

        return err;
    }

    size_t total = 0;
    size_t used = 0;

    err = esp_littlefs_info(
        "storage",
        &total,
        &used
    );

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG,
                 "LittleFS total: %u bytes",
                 (unsigned)total);

        ESP_LOGI(TAG,
                 "LittleFS used : %u bytes",
                 (unsigned)used);
    }

    return ESP_OK;
}


static esp_err_t write_embedded_file(
    const char *path,
    const uint8_t *start,
    const uint8_t *end)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
    {
        ESP_LOGE("WEB",
                 "Failed to create file: %s",
                 path);

        return ESP_FAIL;
    }

    size_t size = (size_t)(end - start);

    size_t written =
        fwrite(start,
               1,
               size,
               file);

    fclose(file);

    if (written != size)
    {
        ESP_LOGE("WEB",
                 "Incomplete write: %s",
                 path);

        ESP_LOGE("WEB",
                 "Expected: %u bytes",
                 (unsigned)size);

        ESP_LOGE("WEB",
                 "Written : %u bytes",
                 (unsigned)written);

        return ESP_FAIL;
    }

    ESP_LOGI("WEB",
             "File written: %s (%u bytes)",
             path,
             (unsigned)written);

    return ESP_OK;
}

static esp_err_t install_web_files(void)
{
    ESP_LOGI("WEB",
             "Installing Web UI files...");

    ESP_ERROR_CHECK(
        write_embedded_file(
            "/littlefs/index.html",
            index_html_start,
            index_html_end
        )
    );

    ESP_ERROR_CHECK(
        write_embedded_file(
            "/littlefs/style.css",
            style_css_start,
            style_css_end
        )
    );

    ESP_ERROR_CHECK(
        write_embedded_file(
            "/littlefs/app.js",
            app_js_start,
            app_js_end
        )
    );

    ESP_LOGI("WEB",
             "Web UI files installed successfully");

    return ESP_OK;
}

/* =========================================================
 * CREATE /PHOTOS DIRECTORY
 * ========================================================= */

static esp_err_t create_photos_directory(void)
{
    const char *path = "/littlefs/photos";

    struct stat st;

    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
        {
            ESP_LOGI(TAG,
                     "Photos directory already exists");

            return ESP_OK;
        }

        ESP_LOGE(TAG,
                 "%s exists but is not a directory",
                 path);

        return ESP_FAIL;
    }

    if (mkdir(path, 0775) != 0)
    {
        ESP_LOGE(TAG,
                 "Failed to create %s",
                 path);

        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Created directory: %s",
             path);

    return ESP_OK;
}


/* =========================================================
 * FIND NEXT IMAGE NUMBER
 * ========================================================= */

 static int find_next_sd_image_number(void)
{
    DIR *dir = opendir(
        SD_MOUNT_POINT "/photos"
    );

    if (dir == NULL)
    {
        ESP_LOGE(
            "SD",
            "Failed to open SD photos directory"
        );

        return 1;
    }

    int max_number = 0;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;

        size_t len = strlen(name);

        if (len < 8)
        {
            continue;
        }

        if (!is_photo_filename(name))
        {
            continue;
        }

        int number = 0;

        if (sscanf(
                name,
                "IMG_%d.jpg",
                &number
            ) == 1)
        {
            if (number > max_number)
            {
                max_number = number;
            }
        }
    }

    closedir(dir);

    return max_number + 1;
}

/* =========================================================
 * CAPTURE AND SAVE
 * ========================================================= */

static esp_err_t capture_and_save(const char *requested_name)
{
    ESP_LOGI(
        "SD",
        "Starting capture to microSD..."
    );

    char filename[64];

    if (requested_name != NULL && requested_name[0] != '\0')
    {
        char base_name[50];
        size_t requested_length = strlen(requested_name);

        if (requested_length >= 4 &&
            strcasecmp(
                requested_name + requested_length - 4,
                ".jpg"
            ) == 0)
        {
            requested_length -= 4;
        }

        if (requested_length == 0 ||
            requested_length >= sizeof(base_name))
        {
            return ESP_ERR_INVALID_ARG;
        }

        memcpy(
            base_name,
            requested_name,
            requested_length
        );

        base_name[requested_length] = '\0';

        for (int suffix = 0; suffix < 10000; suffix++)
        {
            if (suffix == 0)
            {
                snprintf(
                    filename,
                    sizeof(filename),
                    "%s.jpg",
                    base_name
                );
            }
            else
            {
                snprintf(
                    filename,
                    sizeof(filename),
                    "%s_%04d.jpg",
                    base_name,
                    suffix
                );
            }

            if (!is_photo_filename(filename))
            {
                return ESP_ERR_INVALID_ARG;
            }

            char candidate_path[128];

            snprintf(
                candidate_path,
                sizeof(candidate_path),
                SD_MOUNT_POINT "/photos/%s",
                filename
            );

            struct stat candidate_stat;

            if (stat(candidate_path, &candidate_stat) != 0)
            {
                break;
            }

            if (suffix == 9999)
            {
                return ESP_ERR_NO_MEM;
            }
        }
    }
    else
    {
        int image_number =
            find_next_sd_image_number();

        snprintf(
            filename,
            sizeof(filename),
            "IMG_%04d.jpg",
            image_number
        );
    }

    char filepath[128];

    snprintf(
        filepath,
        sizeof(filepath),
        SD_MOUNT_POINT "/photos/%s",
        filename
    );

    ESP_LOGI(
        "SD",
        "Target file: %s",
        filepath
    );

    camera_fb_t *fb = NULL;

    /*
     * Try to capture up to 3 times.
     */
    for (int attempt = 1;
         attempt <= 3;
         attempt++)
    {
        ESP_LOGI(
            "SD",
            "Capture attempt %d/3...",
            attempt
        );

        fb = esp_camera_fb_get();

        if (fb != NULL)
        {
            break;
        }

        ESP_LOGW(
            "SD",
            "Camera framebuffer is NULL"
        );

        vTaskDelay(
            pdMS_TO_TICKS(300)
        );
    }

    if (fb == NULL)
    {
        ESP_LOGE(
            "SD",
            "Camera capture failed"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Captured JPEG: %u bytes",
        (unsigned)fb->len
    );

    /*
     * Check JPEG SOI.
     */
    if (fb->len < 4 ||
        fb->buf[0] != 0xFF ||
        fb->buf[1] != 0xD8)
    {
        ESP_LOGE(
            "SD",
            "Invalid JPEG SOI marker"
        );

        esp_camera_fb_return(fb);

        return ESP_FAIL;
    }

    /*
     * Check JPEG EOI.
     */
    if (fb->buf[fb->len - 2] != 0xFF ||
        fb->buf[fb->len - 1] != 0xD9)
    {
        ESP_LOGE(
            "SD",
            "Invalid JPEG EOI marker"
        );

        esp_camera_fb_return(fb);

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "JPEG markers: OK"
    );

    /*
     * Open SD file.
     */
    FILE *file = fopen(
        filepath,
        "wb"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            "SD",
            "Failed to create file: %s",
            filepath
        );

        esp_camera_fb_return(fb);

        return ESP_FAIL;
    }

    size_t jpeg_size = fb->len;

    size_t written =
        fwrite(
            fb->buf,
            1,
            jpeg_size,
            file
        );

    fclose(file);

    /*
     * Return framebuffer only after
     * finishing access to fb->buf.
     */
    esp_camera_fb_return(fb);

    if (written != jpeg_size)
    {
        ESP_LOGE(
            "SD",
            "Incomplete write: %u / %u bytes",
            (unsigned)written,
            (unsigned)jpeg_size
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Image saved successfully:"
    );

    ESP_LOGI(
        "SD",
        "%s",
        filepath
    );

    return ESP_OK;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    ESP_LOGI("HTTP", "Capture request received");

    char query[128];
    char requested_name[64] = {0};

    if (httpd_req_get_url_query_str(
            req,
            query,
            sizeof(query)
        ) == ESP_OK)
    {
        httpd_query_key_value(
            query,
            "name",
            requested_name,
            sizeof(requested_name)
        );
    }

    esp_err_t ret = capture_and_save(
        requested_name
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE("HTTP", "Capture failed");

        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");

        const char *response =
            "{\"success\":false,\"message\":\"Capture failed\"}";

        httpd_resp_send(
            req,
            response,
            HTTPD_RESP_USE_STRLEN
        );

        return ret;
    }

    ESP_LOGI("HTTP", "Capture successful");

    httpd_resp_set_type(req, "application/json");

    const char *response =
        "{\"success\":true,\"message\":\"Capture successful\"}";

    httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN
    );

    return ESP_OK;
}
static esp_err_t photos_handler(httpd_req_t *req)
{
    ESP_LOGI("HTTP", "Photos list request received");

    DIR *dir = opendir(
        SD_MOUNT_POINT "/photos"
    );

    if (dir == NULL)
    {
        ESP_LOGE("HTTP", "Failed to open photos directory");

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        httpd_resp_set_type(
            req,
            "application/json"
        );

        const char *response =
            "{\"success\":false,\"message\":\"Cannot open photos directory\"}";

        httpd_resp_send(
            req,
            response,
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_FAIL;
    }

    httpd_resp_set_type(
        req,
        "application/json"
    );

    httpd_resp_sendstr_chunk(
        req,
        "{\"success\":true,\"photos\":["
    );

    struct dirent *entry;

    bool first = true;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_REG)
        {
            continue;
        }

        const char *name = entry->d_name;

        if (!is_photo_filename(name))
        {
            continue;
        }

       if (first)
        {
            httpd_resp_sendstr_chunk(
                req,
                "\""
            );

            first = false;
        }
        else
        {
            httpd_resp_sendstr_chunk(
                req,
                ",\""
            );
        }

        httpd_resp_sendstr_chunk(
            req,
            name
        );

        httpd_resp_sendstr_chunk(
            req,
            "\""
        );
    }

    closedir(dir);

    httpd_resp_sendstr_chunk(
        req,
        "]}"
    );

    httpd_resp_sendstr_chunk(
        req,
        NULL
    );

    ESP_LOGI(
        "HTTP",
        "Photos list sent"
    );

    return ESP_OK;
}

static esp_err_t delete_photos_handler(httpd_req_t *req)
{
    char query[160];
    char prefix[50] = {0};
    char start_text[12] = {0};
    char end_text[12] = {0};

    if (httpd_req_get_url_query_str(
            req,
            query,
            sizeof(query)
        ) != ESP_OK ||
        httpd_query_key_value(
            query,
            "prefix",
            prefix,
            sizeof(prefix)
        ) != ESP_OK ||
        httpd_query_key_value(
            query,
            "start",
            start_text,
            sizeof(start_text)
        ) != ESP_OK ||
        httpd_query_key_value(
            query,
            "end",
            end_text,
            sizeof(end_text)
        ) != ESP_OK ||
        !is_photo_prefix(prefix))
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req,
            "{\"success\":false,\"message\":\"Invalid delete parameters\"}",
            HTTPD_RESP_USE_STRLEN
        );
        return ESP_ERR_INVALID_ARG;
    }

    char *start_end = NULL;
    char *end_end = NULL;
    unsigned long start = strtoul(start_text, &start_end, 10);
    unsigned long end = strtoul(end_text, &end_end, 10);

    if (*start_text == '\0' || *end_text == '\0' ||
        *start_end != '\0' || *end_end != '\0' ||
        start > 9999 || end > 9999 || start > end)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req,
            "{\"success\":false,\"message\":\"Invalid number range\"}",
            HTTPD_RESP_USE_STRLEN
        );
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(SD_MOUNT_POINT "/photos");

    if (dir == NULL)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(
            req,
            "{\"success\":false,\"message\":\"Cannot open photos directory\"}",
            HTTPD_RESP_USE_STRLEN
        );
        return ESP_FAIL;
    }

    uint32_t deleted = 0;
    struct dirent *entry;
    size_t prefix_length = strlen(prefix);

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;

        if (!is_photo_filename(name) ||
            strncmp(name, prefix, prefix_length) != 0)
        {
            continue;
        }

        const char *suffix = name + prefix_length;
        unsigned long number = 0;

        if (strncmp(suffix, ".jpg", 4) != 0)
        {
            if (*suffix != '_')
            {
                continue;
            }

            char *number_end = NULL;
            number = strtoul(suffix + 1, &number_end, 10);

            if (number_end == suffix + 1 ||
                strcmp(number_end, ".jpg") != 0)
            {
                continue;
            }
        }

        if (number < start || number > end)
        {
            continue;
        }

        char filepath[320];

        snprintf(
            filepath,
            sizeof(filepath),
            SD_MOUNT_POINT "/photos/%s",
            name
        );

        if (unlink(filepath) == 0)
        {
            deleted++;
        }
    }

    closedir(dir);

    char response[96];

    snprintf(
        response,
        sizeof(response),
        "{\"success\":true,\"deleted\":%u}",
        (unsigned)deleted
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t photo_handler(httpd_req_t *req)
{
    char query[128];
    char filename[64];

    if (httpd_req_get_url_query_str(
            req,
            query,
            sizeof(query)
        ) != ESP_OK)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    if (httpd_query_key_value(
            query,
            "name",
            filename,
            sizeof(filename)
        ) != ESP_OK)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    /*
     * Only allow files with the expected naming format.
     * This prevents paths such as ../something.
     */
    if (!is_photo_filename(filename))
    {
        ESP_LOGW(
            "HTTP",
            "Invalid photo filename: %s",
            filename
        );

        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char filepath[128];

    snprintf(
        filepath,
        sizeof(filepath),
        SD_MOUNT_POINT "/photos/%s",
        filename
    );

    ESP_LOGI(
        "HTTP",
        "Serving photo: %s",
        filepath
    );

    return send_file(
        req,
        filepath,
        "image/jpeg"
    );
}

static esp_err_t download_handler(httpd_req_t *req)
{
    char query[128];
    char filename[64];

    /*
     * Get query string
     */
    if (httpd_req_get_url_query_str(
            req,
            query,
            sizeof(query)
        ) != ESP_OK)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    /*
     * Get filename
     */
    if (httpd_query_key_value(
            query,
            "name",
            filename,
            sizeof(filename)
        ) != ESP_OK)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    /*
     * Validate filename
     */
    if (!is_photo_filename(filename))
    {
        ESP_LOGW(
            "HTTP",
            "Invalid download filename: %s",
            filename
        );

        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    /*
     * Build file path
     */
    char filepath[128];

    snprintf(
        filepath,
        sizeof(filepath),
        SD_MOUNT_POINT "/photos/%s",
        filename
    );

    ESP_LOGI(
        "HTTP",
        "Download request: %s",
        filepath
    );

    FILE *file = fopen(filepath, "rb");

    if (file == NULL)
    {
        ESP_LOGE(
            "HTTP",
            "Failed to open file: %s",
            filepath
        );

        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    /*
     * Tell browser this is a downloadable file
     */
    httpd_resp_set_type(
        req,
        "image/jpeg"
    );

    char disposition[96];

    snprintf(
        disposition,
        sizeof(disposition),
        "attachment; filename=\"%s\"",
        filename
    );

    httpd_resp_set_hdr(
        req,
        "Content-Disposition",
        disposition
    );

    /*
     * Send file
     */
    char buffer[1024];

    size_t bytes_read;

    while (
        (bytes_read =
            fread(
                buffer,
                1,
                sizeof(buffer),
                file
            )) > 0
    )
    {
        esp_err_t ret =
            httpd_resp_send_chunk(
                req,
                buffer,
                bytes_read
            );

        if (ret != ESP_OK)
        {
            fclose(file);

            ESP_LOGE(
                "HTTP",
                "Failed to send download: %s",
                filepath
            );

            return ret;
        }
    }

    fclose(file);

    /*
     * End chunked response
     */
    httpd_resp_send_chunk(
        req,
        NULL,
        0
    );

    ESP_LOGI(
        "HTTP",
        "Download completed: %s",
        filepath
    );

    return ESP_OK;
}

// program wifi 
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(WIFI_TAG, "Wi-Fi started");
         ESP_ERROR_CHECK(esp_wifi_connect());

    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {

        xEventGroupClearBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        ESP_LOGI(WIFI_TAG,
                 "Wi-Fi disconnected. Retrying...");

        esp_wifi_connect();

    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            WIFI_TAG,
            "Got IP address: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        ESP_LOGI(
            WIFI_TAG,
            "Gateway: " IPSTR,
            IP2STR(&event->ip_info.gw)
        );

        ESP_LOGI(
            WIFI_TAG,
            "Netmask: " IPSTR,
            IP2STR(&event->ip_info.netmask)
        );

        xEventGroupSetBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

static void wifi_init_sta(void)
{
    ESP_LOGI(WIFI_TAG, "Initializing Wi-Fi...");

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_LOGI(
        WIFI_TAG,
        "Starting Wi-Fi..."
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_ps(WIFI_PS_NONE)
    );

    ESP_LOGI(
        WIFI_TAG,
        "Waiting for IP address..."
    );

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {

        ESP_LOGI(
            WIFI_TAG,
            "Wi-Fi connected and IP address obtained"
        );
    }
}

static esp_err_t send_file(httpd_req_t *req,
                           const char *filepath,
                           const char *content_type)
{
    FILE *file = fopen(filepath, "rb");

    if (file == NULL)
    {
        ESP_LOGE("HTTP", "Failed to open file: %s", filepath);

        httpd_resp_send_404(req);

        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);

    char buffer[1024];

    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        esp_err_t ret =
            httpd_resp_send_chunk(req, buffer, bytes_read);

        if (ret != ESP_OK)
        {
            fclose(file);

            ESP_LOGE("HTTP",
                     "Failed to send file: %s",
                     filepath);

            return ret;
        }
    }

    fclose(file);

    httpd_resp_send_chunk(req, NULL, 0);

    ESP_LOGI("HTTP",
             "Served file: %s",
             filepath);

    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    return send_file(
        req,
        "/littlefs/index.html",
        "text/html"
    );
}

static esp_err_t style_css_handler(httpd_req_t *req)
{
    return send_file(
        req,
        "/littlefs/style.css",
        "text/css"
    );
}

static esp_err_t app_js_handler(httpd_req_t *req)
{
    return send_file(
        req,
        "/littlefs/app.js",
        "application/javascript"
    );
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    config.recv_wait_timeout = 300;
    config.send_wait_timeout = 300;
    config.max_uri_handlers = 12;

    if (httpd_start(&server, &config) != ESP_OK) {

        ESP_LOGE("HTTP",
                 "Failed to start HTTP server");

        return NULL;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t style_css = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = style_css_handler,
    .user_ctx = NULL
    };

    httpd_uri_t app_js = {
    .uri = "/app.js",
    .method = HTTP_GET,
    .handler = app_js_handler,
    .user_ctx = NULL
    };
    
    httpd_uri_t capture = {
    .uri = "/capture",
    .method = HTTP_POST,
    .handler = capture_handler,
    .user_ctx = NULL
    };

    httpd_uri_t photos = {
        .uri = "/photos",
        .method = HTTP_GET,
        .handler = photos_handler,
        .user_ctx = NULL
    };

    httpd_uri_t delete_photos = {
        .uri = "/delete-photos",
        .method = HTTP_DELETE,
        .handler = delete_photos_handler,
        .user_ctx = NULL
    };

    httpd_uri_t photo = {
        .uri = "/photo",
        .method = HTTP_GET,
        .handler = photo_handler,
        .user_ctx = NULL
    };

    httpd_uri_t download = {
        .uri = "/download",
        .method = HTTP_GET,
        .handler = download_handler,
        .user_ctx = NULL
    };

    httpd_uri_t download_all = {
        .uri = "/download-all",
        .method = HTTP_GET,
        .handler = download_all_handler,
        .user_ctx = NULL
    };

    esp_err_t err;

    err = httpd_register_uri_handler(server, &root);

    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &style_css);

    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /style.css");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &app_js);

    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /app.js");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &capture);

    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /capture");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &photos);
    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /photos");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &delete_photos);
    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /delete-photos");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &photo);
    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /photo");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &download);
    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /download");
        httpd_stop(server);
        return NULL;
    }

    err = httpd_register_uri_handler(server, &download_all);
    if (err != ESP_OK)
    {
        ESP_LOGE("HTTP", "Failed to register /download-all");
        httpd_stop(server);
        return NULL;
    }

    ESP_LOGI("HTTP",
             "HTTP server started");
  
    return server;
}

static esp_err_t sdcard_init(void)
{
    ESP_LOGI("SD", "=================================");
    ESP_LOGI("SD", "Initializing microSD card...");
    ESP_LOGI("SD", "=================================");

    /*
     * SDMMC host
     */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    host.slot = SDMMC_HOST_SLOT_1;
    host.flags |= SDMMC_HOST_FLAG_1BIT;

    /*
     * SDMMC GPIO configuration
     */
    sdmmc_slot_config_t slot_config =
        SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.width = 1;

    slot_config.clk = SD_CLK_GPIO;
    slot_config.cmd = SD_CMD_GPIO;
    slot_config.d0  = SD_D0_GPIO;

    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;

    slot_config.flags |=
        SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /*
     * FAT filesystem configuration
     *
     * IMPORTANT:
     * Do NOT format the card automatically
     * during this test.
     */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024
    };

    /*
     * Mount SD card
     */
    esp_err_t ret =
        esp_vfs_fat_sdmmc_mount(
            SD_MOUNT_POINT,
            &host,
            &slot_config,
            &mount_config,
            &s_sd_card
        );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            "SD",
            "Failed to mount microSD: 0x%x",
            ret
        );

        return ret;
    }

    ESP_LOGI(
        "SD",
        "microSD mounted at %s",
        SD_MOUNT_POINT
    );

    /*
     * Print card information
     */
    sdmmc_card_print_info(
        stdout,
        s_sd_card
    );

    return ESP_OK;
}

static esp_err_t sdcard_test(void)
{
    ESP_LOGI("SD", "Starting SD card read/write test...");

    const char *test_path =
        SD_MOUNT_POINT "/test.txt";

    /*
     * Write test
     */
    FILE *file = fopen(
        test_path,
        "w"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            "SD",
            "Failed to create %s",
            test_path
        );

        return ESP_FAIL;
    }

    const char *test_text =
        "ESP32-S3 microSD test OK\n";

    size_t written =
        fwrite(
            test_text,
            1,
            strlen(test_text),
            file
        );

    fclose(file);

    if (written != strlen(test_text))
    {
        ESP_LOGE(
            "SD",
            "Failed to write test file"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Write test: OK"
    );

    /*
     * Read test
     */
    file = fopen(
        test_path,
        "r"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            "SD",
            "Failed to open %s for reading",
            test_path
        );

        return ESP_FAIL;
    }

    char buffer[64];

    memset(
        buffer,
        0,
        sizeof(buffer)
    );

    size_t read =
        fread(
            buffer,
            1,
            sizeof(buffer) - 1,
            file
        );

    fclose(file);

    if (read == 0)
    {
        ESP_LOGE(
            "SD",
            "Failed to read test file"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Read test: OK"
    );

    ESP_LOGI(
        "SD",
        "File content: %s",
        buffer
    );

    ESP_LOGI(
        "SD",
        "================================="
    );

    ESP_LOGI(
        "SD",
        "microSD TEST SUCCESS"
    );

    ESP_LOGI(
        "SD",
        "================================="
    );

    return ESP_OK;
}

static esp_err_t create_sd_photos_directory(void)
{
    const char *path = SD_MOUNT_POINT "/photos";

    struct stat st;

    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
        {
            ESP_LOGI(
                "SD",
                "Photos directory already exists: %s",
                path
            );

            return ESP_OK;
        }

        ESP_LOGE(
            "SD",
            "%s exists but is not a directory",
            path
        );

        return ESP_FAIL;
    }

    if (mkdir(path, 0775) != 0)
    {
        ESP_LOGE(
            "SD",
            "Failed to create directory: %s",
            path
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Created directory: %s",
        path
    );

    return ESP_OK;
}

static esp_err_t sdcard_capture_test(void)
{
    ESP_LOGI(
        "SD",
        "================================="
    );

    ESP_LOGI(
        "SD",
        "Starting camera -> microSD test..."
    );

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == NULL)
    {
        ESP_LOGE(
            "SD",
            "Camera capture failed"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Captured JPEG: %u bytes",
        (unsigned)fb->len
    );

    if (fb->len < 4)
    {
        ESP_LOGE(
            "SD",
            "JPEG buffer is too small"
        );

        esp_camera_fb_return(fb);

        return ESP_FAIL;
    }

    /* Check JPEG SOI marker */
    if (fb->buf[0] != 0xFF ||
        fb->buf[1] != 0xD8)
    {
        ESP_LOGE(
            "SD",
            "Invalid JPEG SOI marker: %02X %02X",
            fb->buf[0],
            fb->buf[1]
        );

        esp_camera_fb_return(fb);

        return ESP_FAIL;
    }

    /* Check JPEG EOI marker */
    if (fb->buf[fb->len - 2] != 0xFF ||
        fb->buf[fb->len - 1] != 0xD9)
    {
        ESP_LOGE(
            "SD",
            "Invalid JPEG EOI marker: %02X %02X",
            fb->buf[fb->len - 2],
            fb->buf[fb->len - 1]
        );

        esp_camera_fb_return(fb);

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "JPEG markers: OK"
    );

    const char *path =
        SD_MOUNT_POINT "/photos/IMG_0001.jpg";

    FILE *file = fopen(
        path,
        "wb"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            "SD",
            "Failed to create: %s",
            path
        );

        esp_camera_fb_return(fb);

        return ESP_FAIL;
    }

    size_t jpeg_size = fb->len;

    size_t written =
        fwrite(
            fb->buf,
            1,
            jpeg_size,
            file
        );

    fclose(file);

    esp_camera_fb_return(fb);

    if (written != jpeg_size)
    {
        ESP_LOGE(
            "SD",
            "Incomplete write: %u / %u bytes",
            (unsigned)written,
            (unsigned)jpeg_size
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Image written successfully:"
    );

    ESP_LOGI(
        "SD",
        "%s",
        path
    );

    /* Read the file back */
    file = fopen(
        path,
        "rb"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            "SD",
            "Failed to reopen image"
        );

        return ESP_FAIL;
    }

    unsigned char header[2];

    size_t read =
        fread(
            header,
            1,
            sizeof(header),
            file
        );

    fclose(file);

    if (read != 2 ||
        header[0] != 0xFF ||
        header[1] != 0xD8)
    {
        ESP_LOGE(
            "SD",
            "Read-back JPEG verification failed"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        "SD",
        "Read-back JPEG verification: OK"
    );

    ESP_LOGI(
        "SD",
        "================================="
    );

    ESP_LOGI(
        "SD",
        "CAMERA -> microSD TEST SUCCESS"
    );

    ESP_LOGI(
        "SD",
        "================================="
    );

    return ESP_OK;
}

static esp_err_t zip_send_bytes(
    httpd_req_t *req,
    const uint8_t *data,
    size_t size
)
{
    return httpd_resp_send_chunk(
        req,
        (const char *)data,
        size
    );
}

static void zip_put_u16(
    uint8_t *buffer,
    uint16_t value
)
{
    buffer[0] =
        (uint8_t)(value & 0xFF);

    buffer[1] =
        (uint8_t)((value >> 8) & 0xFF);
}


static void zip_put_u32(
    uint8_t *buffer,
    uint32_t value
)
{
    buffer[0] =
        (uint8_t)(value & 0xFF);

    buffer[1] =
        (uint8_t)((value >> 8) & 0xFF);

    buffer[2] =
        (uint8_t)((value >> 16) & 0xFF);

    buffer[3] =
        (uint8_t)((value >> 24) & 0xFF);
}

static bool is_photo_filename(
    const char *name
)
{
    if (name == NULL)
    {
        return false;
    }

    size_t len = strlen(name);

    if (len < 5 || len >= 64)
    {
        return false;
    }

    if (strcasecmp(
            name + len - 4,
            ".jpg"
        ) != 0)
    {
        return false;
    }

    for (size_t index = 0; index < len - 4; index++)
    {
        char character = name[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' ||
              character == '-'))
        {
            return false;
        }
    }

    return true;
}

static bool is_zip_filename(
    const char *name
)
{
    if (name == NULL)
    {
        return false;
    }

    size_t len = strlen(name);

    if (len < 5 || len >= 64 ||
        strcasecmp(name + len - 4, ".zip") != 0)
    {
        return false;
    }

    for (size_t index = 0; index < len - 4; index++)
    {
        char character = name[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' ||
              character == '-'))
        {
            return false;
        }
    }

    return true;
}

static bool is_photo_prefix(
    const char *prefix
)
{
    if (prefix == NULL || strlen(prefix) >= 50)
    {
        return false;
    }

    for (size_t index = 0; prefix[index] != '\0'; index++)
    {
        char character = prefix[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' ||
              character == '-'))
        {
            return false;
        }
    }

    return true;
}

static uint32_t crc32_update(
    uint32_t crc,
    const uint8_t *buffer,
    size_t length
)
{
    while (length--)
    {
        crc ^= *buffer++;

        for (int i = 0; i < 8; i++)
        {
            if (crc & 1)
            {
                crc =
                    (crc >> 1)
                    ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static esp_err_t create_zip_metadata(
    const char *photo_prefix,
    uint32_t *out_file_count,
    uint32_t *out_central_size,
    uint32_t *out_archive_size
)
{
    ESP_LOGI(
        "ZIP",
        "create_zip_metadata(): ENTER"
    );

    ESP_LOGI(
        "ZIP",
        "Opening photos directory..."
    );

    DIR *dir = opendir(
        SD_MOUNT_POINT "/photos"
    );

    ESP_LOGI(
        "ZIP",
        "opendir() returned: %p",
        (void *)dir
    );

    if (dir == NULL)
    {
        ESP_LOGE(
            "ZIP",
            "Failed to open photos directory"
        );

        return ESP_FAIL;
    }

    FILE *meta_file = fopen(
        ZIP_METADATA_FILE,
        "wb"
    );

    if (meta_file == NULL)
    {
        closedir(dir);

        ESP_LOGE(
            "ZIP",
            "Failed to create metadata file"
        );

        return ESP_FAIL;
    }

    uint32_t file_count = 0;

    uint32_t central_size = 0;

    uint64_t archive_offset = 0;

    struct dirent *entry;

while ((entry = readdir(dir)) != NULL)
{
    const char *name = entry->d_name;

    size_t len = strlen(name);

    if (len < 5)
    {
        continue;
    }

    if (!is_photo_filename(name))
    {
        continue;
    }

    if (photo_prefix != NULL &&
        photo_prefix[0] != '\0' &&
        strncmp(
            name,
            photo_prefix,
            strlen(photo_prefix)
        ) != 0)
    {
        continue;
    }

    char filepath[320];

    snprintf(
        filepath,
        sizeof(filepath),
        SD_MOUNT_POINT "/photos/%s",
        name
    );

    struct stat file_stat;

    if (stat(filepath, &file_stat) != 0 ||
        file_stat.st_size < 0 ||
        (uint64_t)file_stat.st_size > UINT32_MAX)
    {
        ESP_LOGE(
            "ZIP",
            "Failed to get valid file size: %s",
            filepath
        );

        fclose(meta_file);
        closedir(dir);
        unlink(ZIP_METADATA_FILE);

        return ESP_FAIL;
    }

    uint32_t file_size =
        (uint32_t)file_stat.st_size;

    size_t name_length =
        strlen(name);

    if (name_length >= 64)
    {
        ESP_LOGW(
            "ZIP",
            "Filename too long: %s",
            name
        );

        continue;
    }

    /*
        * Local file header:
        *
        * 30 bytes + filename
        */
    uint64_t next_offset =
        archive_offset
        + 30ULL
        + name_length
        + file_size
        + 16ULL;

    if (next_offset >
        0xFFFFFFFFULL)
    {
        ESP_LOGE(
            "ZIP",
            "ZIP archive exceeds 4GB"
        );

        fclose(meta_file);
        closedir(dir);
        unlink(ZIP_METADATA_FILE);

        return ESP_FAIL;
    }

    /*
        * Central directory entry:
        *
        * 46 bytes + filename
        */
    uint64_t new_central_size =
        (uint64_t)central_size
        + 46ULL
        + name_length;

    if (new_central_size >
        0xFFFFFFFFULL)
    {
        ESP_LOGE(
            "ZIP",
            "Central directory exceeds 4GB"
        );

        fclose(meta_file);
        closedir(dir);
        unlink(ZIP_METADATA_FILE);

        return ESP_FAIL;
    }

    zip_file_info_t info;

    memset(
        &info,
        0,
        sizeof(info)
    );

    strcpy(
        info.filename,
        name
    );

    info.crc32 = 0;

    info.size =
        file_size;

    info.offset =
        (uint32_t)archive_offset;

    info.name_length =
        (uint16_t)name_length;

    size_t written =
        fwrite(
            &info,
            1,
            sizeof(info),
            meta_file
        );

    if (written != sizeof(info))
    {
        ESP_LOGE(
            "ZIP",
            "Failed to write ZIP metadata"
        );

        fclose(meta_file);
        closedir(dir);
        unlink(ZIP_METADATA_FILE);

        return ESP_FAIL;
    }

    archive_offset =
        next_offset;

    central_size =
        (uint32_t)new_central_size;

    file_count++;

}

    fclose(meta_file);
    closedir(dir);

    *out_file_count = file_count;
    *out_central_size = central_size;
    *out_archive_size = (uint32_t)archive_offset;

    ESP_LOGI(
        "ZIP",
        "create_zip_metadata(): EXIT"
    );

    return ESP_OK;
}
static esp_err_t send_zip_local_header(
    httpd_req_t *req,
    const zip_file_info_t *info
)
{
    uint8_t header[30 + 64];

    memset(
        header,
        0,
        sizeof(header)
    );

    /*
     * Local file header signature
     */
    zip_put_u32(
        &header[0],
        0x04034B50
    );

    /*
     * Version needed
     */
    zip_put_u16(
        &header[4],
        20
    );

    zip_put_u16(
        &header[6],
        0x0008
    );

    /*
     * Compression method = STORE
     */
    zip_put_u16(
        &header[8],
        0
    );

    /*
     * Modification time/date
     */
    zip_put_u16(
        &header[10],
        0
    );

    zip_put_u16(
        &header[12],
        0
    );

    /*
     * CRC32
     */
    zip_put_u32(
        &header[14],
        0
    );

    /*
     * Compressed size
     */
    zip_put_u32(
        &header[18],
        0
    );

    /*
     * Uncompressed size
     */
    zip_put_u32(
        &header[22],
        0
    );

    /*
     * Filename length
     */
    zip_put_u16(
        &header[26],
        info->name_length
    );

    /*
     * Extra field length
     */
    zip_put_u16(
        &header[28],
        0
    );

    memcpy(
        &header[30],
        info->filename,
        info->name_length
    );

    return zip_send_bytes(
        req,
        header,
        30 + info->name_length
    );
}

static esp_err_t send_zip_file_data(
    httpd_req_t *req,
    zip_file_info_t *info
)
{
    char filepath[320];

    snprintf(
        filepath,
        sizeof(filepath),
        SD_MOUNT_POINT
        "/photos/%s",
        info->filename
    );

    FILE *file = fopen(
        filepath,
        "rb"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            "ZIP",
            "Failed to open: %s",
            filepath
        );

        return ESP_FAIL;
    }

    size_t bytes_read;

    uint32_t total_sent = 0;
    uint32_t crc = 0xFFFFFFFFUL;

    while (
        (bytes_read =
            fread(
                s_zip_buffer,
                1,
                sizeof(s_zip_buffer),
                file
            )) > 0
    )
    {
        esp_err_t ret =
            zip_send_bytes(
                req,
                s_zip_buffer,
                bytes_read
            );

        if (ret != ESP_OK)
        {
            fclose(file);

            return ret;
        }

        crc = crc32_update(crc, s_zip_buffer, bytes_read);

        total_sent +=
            (uint32_t)bytes_read;
    }

    fclose(file);

    if (total_sent != info->size)
    {
        ESP_LOGE(
            "ZIP",
            "File changed while downloading: %s",
            info->filename
        );

        return ESP_FAIL;
    }

    info->crc32 = crc ^ 0xFFFFFFFFUL;

    uint8_t descriptor[16];

    zip_put_u32(&descriptor[0], 0x08074B50);
    zip_put_u32(&descriptor[4], info->crc32);
    zip_put_u32(&descriptor[8], info->size);
    zip_put_u32(&descriptor[12], info->size);

    if (zip_send_bytes(req, descriptor, sizeof(descriptor)) != ESP_OK)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t send_zip_central_header(
    httpd_req_t *req,
    const zip_file_info_t *info
)
{
    uint8_t header[46 + 64];

    memset(
        header,
        0,
        sizeof(header)
    );

    /*
     * Central directory signature
     */
    zip_put_u32(
        &header[0],
        0x02014B50
    );

    /*
     * Version made by
     */
    zip_put_u16(
        &header[4],
        20
    );

    /*
     * Version needed
     */
    zip_put_u16(
        &header[6],
        20
    );

    /*
     * General purpose flag
     */
    zip_put_u16(
        &header[8],
        0x0008
    );

    /*
     * Compression method = STORE
     */
    zip_put_u16(
        &header[10],
        0
    );

    /*
     * Modification time/date
     */
    zip_put_u16(
        &header[12],
        0
    );

    zip_put_u16(
        &header[14],
        0
    );

    /*
     * CRC32
     */
    zip_put_u32(
        &header[16],
        info->crc32
    );

    /*
     * Compressed size
     */
    zip_put_u32(
        &header[20],
        info->size
    );

    /*
     * Uncompressed size
     */
    zip_put_u32(
        &header[24],
        info->size
    );

    /*
     * Filename length
     */
    zip_put_u16(
        &header[28],
        info->name_length
    );

    /*
     * Extra field length
     */
    zip_put_u16(
        &header[30],
        0
    );

    /*
     * Comment length
     */
    zip_put_u16(
        &header[32],
        0
    );

    /*
     * Disk number
     */
    zip_put_u16(
        &header[34],
        0
    );

    /*
     * Internal attributes
     */
    zip_put_u16(
        &header[36],
        0
    );

    /*
     * External attributes
     */
    zip_put_u32(
        &header[38],
        0
    );

    /*
     * Relative offset
     */
    zip_put_u32(
        &header[42],
        info->offset
    );

    memcpy(
        &header[46],
        info->filename,
        info->name_length
    );

    return zip_send_bytes(
        req,
        header,
        46 + info->name_length
    );
}

static esp_err_t send_zip_end(
    httpd_req_t *req,
    uint32_t file_count,
    uint32_t central_size,
    uint32_t central_offset
)
{
    uint8_t end[22];

    memset(
        end,
        0,
        sizeof(end)
    );

    /*
     * End of central directory signature
     */
    zip_put_u32(
        &end[0],
        0x06054B50
    );

    /*
     * Disk number
     */
    zip_put_u16(
        &end[4],
        0
    );

    /*
     * Central directory disk
     */
    zip_put_u16(
        &end[6],
        0
    );

    /*
     * Entries on this disk
     */
    zip_put_u16(
        &end[8],
        (uint16_t)file_count
    );

    /*
     * Total entries
     */
    zip_put_u16(
        &end[10],
        (uint16_t)file_count
    );

    /*
     * Central directory size
     */
    zip_put_u32(
        &end[12],
        central_size
    );

    /*
     * Central directory offset
     */
    zip_put_u32(
        &end[16],
        central_offset
    );

    /*
     * Comment length
     */
    zip_put_u16(
        &end[20],
        0
    );

    return zip_send_bytes(
        req,
        end,
        sizeof(end)
    );
}

static esp_err_t download_all_handler(httpd_req_t *req)
{
    char query[128];
    char photo_prefix[50] = {0};

    if (httpd_req_get_url_query_str(
            req,
            query,
            sizeof(query)
        ) == ESP_OK &&
        httpd_query_key_value(
            query,
            "prefix",
            photo_prefix,
            sizeof(photo_prefix)
        ) == ESP_OK &&
        !is_photo_prefix(photo_prefix))
    {
        httpd_resp_set_status(
            req,
            "400 Bad Request"
        );

        httpd_resp_set_type(
            req,
            "text/plain"
        );

        httpd_resp_send(
            req,
            "Invalid photo prefix",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(
        "HTTP",
        "Download ALL request received"
    );

    ESP_LOGI(
        "ZIP",
        "STEP 1: Creating ZIP metadata..."
    );

    ESP_LOGI(
        "ZIP",
        "Before create_zip_metadata()"
    );

    uint32_t file_count = 0;

    uint32_t central_size = 0;

    uint32_t archive_size = 0;

    ESP_LOGI(
        "ZIP",
        "Calling create_zip_metadata()..."
    );

    esp_err_t ret =
        create_zip_metadata(
            photo_prefix,
            &file_count,
            &central_size,
            &archive_size
        );

    ESP_LOGI(
        "ZIP",
        "Returned from create_zip_metadata(): 0x%x",
        ret
    );

    ESP_LOGI(
        "ZIP",
        "HTTP task stack free after metadata: %u",
        (unsigned)uxTaskGetStackHighWaterMark(NULL)
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            "ZIP",
            "STEP 1 FAILED: create_zip_metadata()"
        );

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        httpd_resp_set_type(
            req,
            "text/plain"
        );

        httpd_resp_send(
            req,
            "ZIP metadata creation failed",
            HTTPD_RESP_USE_STRLEN
        );

        return ret;
    }

    ESP_LOGI(
        "ZIP",
        "STEP 1 OK: ZIP metadata created"
    );

    ESP_LOGI(
        "ZIP",
        "Files: %u",
        (unsigned)file_count
    );

    ESP_LOGI(
        "ZIP",
        "Central size: %u bytes",
        (unsigned)central_size
    );

    ESP_LOGI(
        "ZIP",
        "Archive size: %u bytes",
        (unsigned)archive_size
    );

    if (archive_size > UINT32_MAX - central_size - 22U)
    {
        ESP_LOGE(
            "ZIP",
            "ZIP archive size overflows 32-bit format"
        );

        unlink(ZIP_METADATA_FILE);

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        httpd_resp_set_type(
            req,
            "text/plain"
        );

        httpd_resp_send(
            req,
            "ZIP archive is too large",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_FAIL;
    }

    if (file_count > UINT16_MAX)
    {
        ESP_LOGE(
            "ZIP",
            "Too many files for a ZIP archive: %u",
            (unsigned)file_count
        );

        unlink(ZIP_METADATA_FILE);

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        httpd_resp_set_type(
            req,
            "text/plain"
        );

        httpd_resp_send(
            req,
            "Too many files for ZIP archive",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_FAIL;
    }

    char archive_name[64] = "photos.zip";
    char archive_query[128];
    char requested_name[64];

    if (httpd_req_get_url_query_str(
            req,
            archive_query,
            sizeof(archive_query)
        ) == ESP_OK &&
        httpd_query_key_value(
            archive_query,
            "name",
            requested_name,
            sizeof(requested_name)
        ) == ESP_OK &&
        is_zip_filename(requested_name))
    {
        snprintf(
            archive_name,
            sizeof(archive_name),
            "%s",
            requested_name
        );
    }

    httpd_resp_set_type(
        req,
        "application/zip"
    );

    char disposition[96];

    snprintf(
        disposition,
        sizeof(disposition),
        "attachment; filename=\"%s\"",
        archive_name
    );

    httpd_resp_set_hdr(
        req,
        "Content-Disposition",
        disposition
    );

    FILE *meta_file = fopen(
        ZIP_METADATA_FILE,
        "rb+"
    );

    if (meta_file == NULL)
    {
        ESP_LOGE(
            "ZIP",
            "Failed to reopen metadata file"
        );

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        httpd_resp_send(
            req,
            "ZIP metadata read failed",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_FAIL;
    }

    zip_file_info_t info;

    for (uint32_t index = 0; index < file_count; index++)
    {
        size_t bytes_read = fread(
            &info,
            1,
            sizeof(info),
            meta_file
        );

        if (bytes_read != sizeof(info))
        {
            ESP_LOGE(
                "ZIP",
                "Invalid metadata at index %u",
                (unsigned)index
            );

            fclose(meta_file);
            unlink(ZIP_METADATA_FILE);
            return ESP_FAIL;
        }

        ret = send_zip_local_header(req, &info);

        if (ret == ESP_OK)
        {
            ret = send_zip_file_data(req, &info);
        }

        if (ret != ESP_OK)
        {
            ESP_LOGE(
                "ZIP",
                "Failed to send file: %s",
                info.filename
            );

            fclose(meta_file);
            unlink(ZIP_METADATA_FILE);
            return ret;
        }

        if (fseek(
                meta_file,
                (long)(index * sizeof(info)),
                SEEK_SET
            ) != 0 ||
            fwrite(&info, 1, sizeof(info), meta_file) != sizeof(info) ||
            fseek(
                meta_file,
                (long)((index + 1) * sizeof(info)),
                SEEK_SET
            ) != 0)
        {
            ESP_LOGE(
                "ZIP",
                "Failed to update ZIP metadata: %s",
                info.filename
            );

            fclose(meta_file);
            unlink(ZIP_METADATA_FILE);
            return ESP_FAIL;
        }
    }

    rewind(meta_file);

    for (uint32_t index = 0; index < file_count; index++)
    {
        size_t bytes_read = fread(
            &info,
            1,
            sizeof(info),
            meta_file
        );

        if (bytes_read != sizeof(info))
        {
            ESP_LOGE(
                "ZIP",
                "Invalid metadata while creating central directory"
            );

            fclose(meta_file);
            unlink(ZIP_METADATA_FILE);
            return ESP_FAIL;
        }

        ret = send_zip_central_header(req, &info);

        if (ret != ESP_OK)
        {
            ESP_LOGE(
                "ZIP",
                "Failed to send central directory entry: %s",
                info.filename
            );

            fclose(meta_file);
            unlink(ZIP_METADATA_FILE);
            return ret;
        }
    }

    fclose(meta_file);
    unlink(ZIP_METADATA_FILE);

    ret = send_zip_end(
        req,
        file_count,
        central_size,
        archive_size
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            "ZIP",
            "Failed to send ZIP end record"
        );

        return ret;
    }

    ret = httpd_resp_send_chunk(
        req,
        NULL,
        0
    );

    ESP_LOGI(
        "ZIP",
        "ZIP download completed: %u files",
        (unsigned)file_count
    );

    return ret;
}

/* =========================================================
 * APP MAIN
 * ========================================================= */

// ----------------- GD 3 -----------------

 void app_main(void)
{
    ESP_LOGI(
        TAG,
        "================================="
    );

    ESP_LOGI(
        TAG,
        "ESP32 Camera Server"
    );

    ESP_LOGI(
        TAG,
        "================================="
    );


    /* 1. Initialize NVS */

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);


    /* 2. Initialize LittleFS */

    ESP_ERROR_CHECK(
        littlefs_init()
    );


    /* 3. Install Web UI */

    ESP_ERROR_CHECK(
        install_web_files()
    );


    /* 4. Create LittleFS photos directory */

    ESP_ERROR_CHECK(
        create_photos_directory()
    );


    /* 5. Initialize microSD */

    ESP_ERROR_CHECK(
        sdcard_init()
    );

    ESP_ERROR_CHECK(
        sdcard_test()
    );

    ESP_ERROR_CHECK(
        create_sd_photos_directory()
    );

    ESP_ERROR_CHECK(
        camera_init()
    );

    ESP_LOGI(
        "STORAGE",
        "Waiting for camera to stabilize..."
    );

    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );

    esp_err_t capture_test_ret = sdcard_capture_test();
    if (capture_test_ret != ESP_OK)
    {
        ESP_LOGE(
            "SD",
            "Camera -> microSD test FAILED"
        );
    }
    else
    {
        ESP_LOGI(
            "SD",
            "Camera -> microSD test SUCCESS"
        );
    }

    /* 8. Initialize Wi-Fi */

    ESP_LOGI(
        TAG,
        "Starting Wi-Fi initialization..."
    );

    wifi_init_sta();

    ESP_LOGI(
        TAG,
        "Wi-Fi initialization finished"
    );


    /* 9. Start HTTP server */

    ESP_LOGI(
        TAG,
        "Starting HTTP server..."
    );

    httpd_handle_t server =
        start_webserver();

    if (server == NULL)
    {
        ESP_LOGE(
            TAG,
            "HTTP server failed to start"
        );
    }
    else
    {
        ESP_LOGI(
            TAG,
            "HTTP server is running"
        );
    }


    /* 10. System ready */

    ESP_LOGI(
        TAG,
        "System ready"
    );


    while (1)
    {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}

// ----------------- GD 2 -----------------

// void app_main(void)
// {
//     ESP_LOGI(TAG, "========================================");
//     ESP_LOGI(TAG, "ESP32-S3 OV3660 + LittleFS");
//     ESP_LOGI(TAG, "MULTI IMAGE TEST");
//     ESP_LOGI(TAG, "========================================");

//     // LittleFS
//     littlefs_init();

//     // Photos directory
//     create_photos_directory();

//     // Camera
//     camera_init();

//     // Capture
//     int image_number = find_next_image_number();

//     ESP_LOGI(
//         TAG,
//         "Next image: /littlefs/photos/IMG_%04d.jpg",
//         image_number
//     );

//     capture_and_save();

//     // List
//     list_photos();

//     // Read test
//     read_and_validate_image(
//         "/littlefs/photos/IMG_0001.jpg"
//     );

//     // ========================================
//     // Wi-Fi
//     // ========================================

//     ESP_LOGI(
//         TAG,
//         "Starting Wi-Fi..."
//     );

//     esp_err_t ret = nvs_flash_init();

//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
//         ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

//         ESP_ERROR_CHECK(
//             nvs_flash_erase()
//         );

//         ret = nvs_flash_init();
//     }

//     ESP_ERROR_CHECK(ret);

//     wifi_init_sta();

//     ESP_LOGI(
//         TAG,
//         "Wi-Fi initialization finished"
//     );

//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }

// void app_main(void)
// {
//     ESP_LOGI(TAG,
//              "========================================");

//     ESP_LOGI(TAG,
//              "ESP32-S3 OV3660 + LittleFS");

//     ESP_LOGI(TAG,
//              "MULTI IMAGE TEST");

//     ESP_LOGI(TAG,
//              "========================================");


//     /* 1. LittleFS */

//     if (littlefs_init() != ESP_OK)
//     {
//         ESP_LOGE(TAG,
//                  "LittleFS initialization failed");

//         while (1)
//         {
//             vTaskDelay(pdMS_TO_TICKS(1000));
//         }
//     }


//     /* 2. Photos directory */

//     if (create_photos_directory() != ESP_OK)
//     {
//         ESP_LOGE(TAG,
//                  "Photos directory initialization failed");

//         while (1)
//         {
//             vTaskDelay(pdMS_TO_TICKS(1000));
//         }
//     }


//     /* 3. Camera */

//     if (camera_init() != ESP_OK)
//     {
//         ESP_LOGE(TAG,
//                  "Camera initialization failed");

//         while (1)
//         {
//             vTaskDelay(pdMS_TO_TICKS(1000));
//         }
//     }


//     /* 4. Capture one image */

//     if (capture_and_save() != ESP_OK)
//     {
//         ESP_LOGE(TAG,
//                  "Capture/save failed");
//     }


//     /* 5. Show files */

//     list_photos();


//     ESP_LOGI(TAG,
//              "========================================");

//     ESP_LOGI(TAG,
//              "MULTI IMAGE TEST FINISHED");

//     ESP_LOGI(TAG,
//              "========================================");

//     read_and_validate_image("/littlefs/photos/IMG_0001.jpg");
//     while (1)
//     {
//         vTaskDelay(pdMS_TO_TICKS(5000));
//     }
// }