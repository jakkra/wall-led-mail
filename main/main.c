#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include <esp_http_server.h>

#include "string.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <math.h>

#include "driver/rmt.h"
#include "led_strip.h"

static const char *TAG = "strip";

#define RMT_TX_CHANNEL RMT_CHANNEL_0

#define MAX_HTTP_RSP_LEN 128

// Max 5 leds at max power 255
#define POWER_BUDGET_MAIL_LED(_num_unread) (fmin((5 * 255) / _num_unread, 170))
#define MAX_BRIGHTNESS  100

static void init_led_strip(void);
static void init_wifi(void);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static void refresh_leds(void);
static httpd_handle_t start_webserver(void);
static esp_err_t wled_request_handler(httpd_req_t *req);
static void run_new_mail_animation(void);
static void run_mail_read_animation(void);
void set_pixel(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t brightness);

static void http_client_task(void *args);

static led_strip_t *strip;
static bool wifi_connected = false;
static uint8_t http_get_rsp_buf[MAX_HTTP_RSP_LEN];
static uint32_t num_unread_email = 0;
static uint32_t prev_num_unread_email = 0;
static bool leds_on = true;

uint32_t r = 255, g = 255, b = 255, a = 50;

static const httpd_uri_t wled = {
    .uri       = "/win*",
    .method    = HTTP_GET,
    .handler   = wled_request_handler,
};

void app_main(void)
{
    TaskHandle_t task_handle;
    BaseType_t status;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_led_strip();

    init_wifi();

    status = xTaskCreate(http_client_task, "http_client_task", 4096, NULL, tskIDLE_PRIORITY, &task_handle);
    assert(status == pdPASS);
    start_webserver();
}

static void http_client_task(void *args)
{
    esp_http_client_config_t config = {
        .url = "http://207.154.239.115/api/user/numUnreadMail?token=" CONFIG_BACKEND_ACCESS_TOKEN,
        .event_handler = http_event_handler,
    };
    
    while (true) {
        if (wifi_connected) {
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_err_t err = esp_http_client_perform(client);

            if (err == ESP_OK) {
                uint32_t content_len = esp_http_client_get_content_length(client);
                uint32_t status_code = esp_http_client_get_status_code(client);

                if (status_code == 200 && content_len < MAX_HTTP_RSP_LEN) {
                    prev_num_unread_email = num_unread_email;
                    num_unread_email = strtol((char*)http_get_rsp_buf, NULL, 10);
                    ESP_LOGI(TAG, "Num unread mail: %d", num_unread_email);
                    if (leds_on) {
                        if (prev_num_unread_email < num_unread_email) {
                            ESP_LOGD(TAG, "New mail => run animation");
                            run_new_mail_animation(); 
                        } else if (prev_num_unread_email > num_unread_email) {
                            run_mail_read_animation();
                        }
                        refresh_leds();
                    }
                } else {
                    ESP_LOGE(TAG, "Unexpected content length %d or status code %d", content_len, status_code);
                }
            }
            esp_http_client_cleanup(client);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void run_new_mail_animation()
{
    for (uint16_t i = 0; i < CONFIG_STRIP_LED_NUMBER; i++) {
        if (i >= CONFIG_STRIP_LED_NUMBER - num_unread_email) {
            // Don't touch mail leds
        } else {
            if (i > 1) {
                set_pixel(strip, i - 1, 255, 0, 0, 255);
            }
            if (i < CONFIG_STRIP_LED_NUMBER - num_unread_email - 1) {
                set_pixel(strip, i + 1, 255, 0, 0, 255);
            }
            set_pixel(strip, i, 255, 0, 0, 255);
            ESP_ERROR_CHECK(strip->refresh(strip, 100));
            vTaskDelay(pdMS_TO_TICKS(1));
            set_pixel(strip, i, r, g, b, a);
             if (i > 1) {
                set_pixel(strip, i - 1, r, g, b, a);
            }
            if (i < CONFIG_STRIP_LED_NUMBER - num_unread_email - 1) {
                set_pixel(strip, i + 1, r, g, b, a);
            }
        }
    }
}

static void run_mail_read_animation()
{
    for (int16_t i = CONFIG_STRIP_LED_NUMBER; i >= 0; i--) {
        if (i >= CONFIG_STRIP_LED_NUMBER - num_unread_email) {
            // Don't touch mail leds
        } else {
            if (i > 1) {
                set_pixel(strip, i - 1, 255, 0, 0, 255);
            }
            if (i < CONFIG_STRIP_LED_NUMBER - num_unread_email - 1) {
                set_pixel(strip, i + 1, 255, 0, 0, 255);
            }
            set_pixel(strip, i, 255, 0, 0, 255);
            ESP_ERROR_CHECK(strip->refresh(strip, 100));
            vTaskDelay(pdMS_TO_TICKS(1));
            set_pixel(strip, i, r, g, b, a);
             if (i > 1) {
                set_pixel(strip, i - 1, r, g, b, a);
            }
            if (i < CONFIG_STRIP_LED_NUMBER - num_unread_email - 1) {
                set_pixel(strip, i + 1, r, g, b, a);
            }
        }
    }
}

static void refresh_leds(void)
{
    if (!leds_on) return;
    for (uint16_t i = 0; i < CONFIG_STRIP_LED_NUMBER; i++) {
        if (i >= CONFIG_STRIP_LED_NUMBER - num_unread_email) {
            set_pixel(strip, i, 255, 0, 0, 255);
        } else {
            set_pixel(strip, i, r, g, b, a);
        }
    }
    ESP_ERROR_CHECK(strip->refresh(strip, 100));
}

static void strip_enable(bool on)
{
    if (!on) {
        leds_on = false;
        ESP_ERROR_CHECK(strip->clear(strip, 100));
    } else {
        leds_on = true;
        refresh_leds();
    }
}

static esp_err_t wled_request_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;
    uint32_t temp;
    
    bool input_ok = false;
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        assert(buf != NULL);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            if (httpd_query_key_value(buf, "T", param, sizeof(param)) == ESP_OK) {
                uint32_t t = strtol(param, NULL, 10);
                input_ok = true;
                if (t == 0) {
                    strip_enable(false);
                } else if (t == 1) {
                    strip_enable(!leds_on);
                } else if (t == 2) {
                    strip_enable(true);
                } else {
                    input_ok = false;
                }
            } else {
                if (httpd_query_key_value(buf, "A", param, sizeof(param)) == ESP_OK) {
                    temp = strtol(param, NULL, 10);
                    if (temp < 256) {
                        if (temp > MAX_BRIGHTNESS) {
                            temp = MAX_BRIGHTNESS;
                        }
                        a = temp;
                        input_ok = true;
                    }
                }
                if (httpd_query_key_value(buf, "R", param, sizeof(param)) == ESP_OK) {
                    temp = strtol(param, NULL, 10);
                    if (r < 256) {
                        r = temp;
                        input_ok = true;
                    }
                }
                if (httpd_query_key_value(buf, "G", param, sizeof(param)) == ESP_OK) {
                    temp = strtol(param, NULL, 10);
                    if (g < 256) {
                        g = temp;
                        input_ok = true;
                    }
                }
                if (httpd_query_key_value(buf, "B", param, sizeof(param)) == ESP_OK) {
                    temp = strtol(param, NULL, 10);
                    if (b < 256) {
                        b = temp;
                        input_ok = true;
                    }
                }

                if (input_ok) {
                    refresh_leds();
                }
            }
            
        }
        free(buf);
    }

    if (input_ok) {
        httpd_resp_send(req, "OK", strlen("OK"));
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid params");
    }

    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &wled);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void init_led_strip(void)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(CONFIG_RMT_TX_GPIO, RMT_TX_CHANNEL);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(CONFIG_STRIP_LED_NUMBER, (led_strip_dev_t)config.channel);
    strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!strip) {
        ESP_LOGE(TAG, "install WS2812 driver failed");
    }

    ESP_ERROR_CHECK(strip->clear(strip, 100));
    for (uint16_t i = 0; i < CONFIG_STRIP_LED_NUMBER; i++) {
        set_pixel(strip, i, r, g, b, a);
    }
    ESP_ERROR_CHECK(strip->refresh(strip, 100));
    ESP_LOGI(TAG, "LED Strip ready");
}

static void init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,  IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "WiFi Started");
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Failed to connect WiFi");
        wifi_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
        case HTTP_EVENT_ON_CONNECTED:
        case HTTP_EVENT_HEADER_SENT:
        case HTTP_EVENT_ON_HEADER:
        case HTTP_EVENT_ON_FINISH:
        case HTTP_EVENT_DISCONNECTED:
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (evt->data_len < MAX_HTTP_RSP_LEN) {
                memcpy(http_get_rsp_buf, evt->data, evt->data_len);
            }
            break;
    }

    return ESP_OK;
}

void set_pixel(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t brightness)
{
    assert(strip != NULL);
    assert(index < CONFIG_STRIP_LED_NUMBER);
    assert(red <= 255);
    assert(green <= 255);
    assert(blue <= 255);
    assert(brightness <= 255);

    float bri_multiplier = (float)brightness / 255;
    ESP_ERROR_CHECK(strip->set_pixel(strip, index, (float)red * bri_multiplier, (float)green * bri_multiplier, (float)blue * bri_multiplier));
}