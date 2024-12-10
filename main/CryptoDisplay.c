#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "i2c-lcd1602.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include "rom/uart.h"
#include "sdkconfig.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "esp_tls.h"
#include "nvs_flash.h"
#include "smbus.h"
#include <stdlib.h>
#define TAG "app"

// LCD1602
#define LCD_NUM_ROWS 2
#define LCD_NUM_COLUMNS 32
#define LCD_NUM_VISIBLE_COLUMNS 16

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_TX_BUF_LEN 0 // disabled
#define I2C_MASTER_RX_BUF_LEN 0 // disabled
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_SDA_IO 14
#define I2C_MASTER_SCL_IO 13

// Wifi and Password
#define ESP_WIFI_SSID "example_ssid"
#define ESP_WIFI_PASS "example_password"
#define COIN_GECKO_API_KEY "exmaple_apikey"
#define ESP_MAXIMUM_RETRY 10

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 512

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
// API to pull data from
#define WEB_SERVER "api.coingecko.com"
#define WEB_PORT (443)
#define WEB_URL                                                                \
  "https://api.coingecko.com/api/v3/simple/"                                   \
  "price?ids=bitcoin%2Cmonero&vs_currencies=usd%2Ccad&precision=0"
// reading server_root_cert.pem, use command
/*pem file extracted via openssl s_client -showcerts -connect
 * api.coingecko.com:443 </dev/null */
extern const uint8_t
    server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t
    server_root_cert_pem_end[] asm("_binary_server_root_cert_pem_end");
static int s_retry_num = 0;

static void i2c_master_init(void) {
  int i2c_master_port = I2C_MASTER_NUM;
  i2c_config_t conf;
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = I2C_MASTER_SDA_IO;
  conf.sda_pullup_en = GPIO_PULLUP_DISABLE; // GY-2561 provides 10kΩ pullups
  conf.scl_io_num = I2C_MASTER_SCL_IO;
  conf.scl_pullup_en = GPIO_PULLUP_DISABLE; // GY-2561 provides 10kΩ pullups
  conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
  conf.clk_flags = 0;
  i2c_param_config(i2c_master_port, &conf);
  i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_LEN,
                     I2C_MASTER_TX_BUF_LEN, 0);
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < ESP_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_start(void) {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta = {.ssid = ESP_WIFI_SSID,
              .password = ESP_WIFI_PASS,
              /* Authmode threshold resets to WPA2 as default if password
               * matches WPA2 standards (password len => 8). If you want to
               * connect the device to deprecated WEP/WPA networks, Please set
               * the threshold value to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set
               * the password with length and format matching to
               * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
               */
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg =
                  {
                      .capable = true,
                      .required = false,
                  }},
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ESP_WIFI_SSID,
             ESP_WIFI_PASS);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ESP_WIFI_SSID,
             ESP_WIFI_PASS);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
}

void coin_gecko_get(char *output_buffer) {
  // Buffer to store response of http request
  int content_length = 0;

  esp_http_client_config_t config_get = {
      .url = WEB_URL,
      .method = HTTP_METHOD_GET,
      .cert_pem = (const char *)server_root_cert_pem_start,

  };

  esp_http_client_handle_t client = esp_http_client_init(&config_get);
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "x-cg-demo-api-key", COIN_GECKO_API_KEY);

  // GET Request
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
  } else {
    content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
      ESP_LOGE(TAG, "HTTP client fetch headers failed");
    } else {
      int data_read = esp_http_client_read_response(client, output_buffer,
                                                    MAX_HTTP_OUTPUT_BUFFER);
      if (data_read >= 0) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        ESP_LOG_BUFFER_HEX(TAG, output_buffer, data_read);
      } else {
        ESP_LOGE(TAG, "Failed to read response");
      }
    }
  }
  esp_http_client_close(client);

  esp_http_client_cleanup(client);
}
void lcd1602_task(void *pvParameters) {
  // Set up I2C
  i2c_master_init();
  i2c_port_t i2c_num = I2C_MASTER_NUM;
  uint8_t address = 0x3F;

  // Set up the SMBus
  smbus_info_t *smbus_info = smbus_malloc();
  ESP_ERROR_CHECK(smbus_init(smbus_info, i2c_num, address));
  ESP_ERROR_CHECK(smbus_set_timeout(smbus_info, 1000 / portTICK_PERIOD_MS));

  // Set up the LCD1602 device with backlight off
  i2c_lcd1602_info_t *lcd_info = i2c_lcd1602_malloc();
  ESP_ERROR_CHECK(i2c_lcd1602_init(lcd_info, smbus_info, true, LCD_NUM_ROWS,
                                   LCD_NUM_COLUMNS, LCD_NUM_VISIBLE_COLUMNS));
  ESP_ERROR_CHECK(i2c_lcd1602_reset(lcd_info));

  // turn off backlight
  ESP_LOGI(TAG, "backlight off");
  i2c_lcd1602_set_backlight(lcd_info, false);
  vTaskDelay(500 / portTICK_PERIOD_MS);
  // turn on backlight
  ESP_LOGI(TAG, "backlight on");
  i2c_lcd1602_set_backlight(lcd_info, true);
  vTaskDelay(500 / portTICK_PERIOD_MS);
  // Print BTC and XMR
  i2c_lcd1602_move_cursor(lcd_info, 0, 0);
  i2c_lcd1602_write_char(lcd_info, 'B');
  i2c_lcd1602_move_cursor(lcd_info, 1, 0);
  i2c_lcd1602_write_char(lcd_info, 'T');
  i2c_lcd1602_move_cursor(lcd_info, 2, 0);
  i2c_lcd1602_write_char(lcd_info, 'C');
  i2c_lcd1602_move_cursor(lcd_info, 4, 0);
  i2c_lcd1602_write_char(lcd_info, '$');

  i2c_lcd1602_move_cursor(lcd_info, 0, 1);
  i2c_lcd1602_write_char(lcd_info, 'X');
  i2c_lcd1602_move_cursor(lcd_info, 1, 1);
  i2c_lcd1602_write_char(lcd_info, 'M');
  i2c_lcd1602_move_cursor(lcd_info, 2, 1);
  i2c_lcd1602_write_char(lcd_info, 'R');
  i2c_lcd1602_move_cursor(lcd_info, 4, 1);
  i2c_lcd1602_write_char(lcd_info, '$');
  // Run the http GET request every 3 hours
  int sleeptimer = 1.08 * pow(10, 7); // 3 hours
  const TickType_t xDelay = sleeptimer / portTICK_PERIOD_MS;
  while (1) {
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    coin_gecko_get(output_buffer);
    printf("HTTP_EVENT_ON_DATA: %.*s\n", MAX_HTTP_OUTPUT_BUFFER, output_buffer);

    // Write bitcoin in USD
    int j = 5;
    for (int i = 18; output_buffer[i] != ','; i++) {
      i2c_lcd1602_move_cursor(lcd_info, j, 0);
      i2c_lcd1602_write_char(lcd_info, output_buffer[i]);
      j++;
    }
    j = 5;
    for (int i = 54; output_buffer[i] != ','; i++) {
      i2c_lcd1602_move_cursor(lcd_info, j, 1);
      i2c_lcd1602_write_char(lcd_info, output_buffer[i]);
      j++;
    }
    vTaskDelay(xDelay);
  }
  vTaskDelete(NULL);
}

void app_main() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  wifi_init_start();
  xTaskCreate(&lcd1602_task, "lcd1602_task", 8096, NULL, 5, NULL);
}
