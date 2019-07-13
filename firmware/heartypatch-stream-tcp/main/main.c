#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_bt.h"	// bt.h deprecated
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/sdmmc_host.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "mdns.h"
#include "esp_log.h"
#include "kalam32.h"
#include "max30003.h"
#include "kalam32_tcp_server.h"

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

extern xSemaphoreHandle print_mux;
char uart_data[50];
const int uart_num = UART_NUM_1;
uint8_t* db;
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT_kalam = BIT0;

uint8_t* db;
//#undef CONFIG_MDNS_ENABLE
//#ifdef CONFIG_MDNS_ENABLE
//mdns_server_t * mdns = NULL;
//#endif

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT_kalam);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT_kalam);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void kalam_wifi_init(void)
{
      tcpip_adapter_init();
      wifi_event_group = xEventGroupCreate();
      ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
      wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
      ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
      wifi_config_t wifi_config = {
          .sta = {
              .ssid = CONFIG_WIFI_SSID,
              .password = CONFIG_WIFI_PASSWORD,
          },
      };
      ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
      ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
      ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
      ESP_ERROR_CHECK( esp_wifi_start() );

// Old code left for referenace, remove when verified.
//#ifdef CONFIG_MDNS_ENABLE
//      esp_err_t err = mdns_init(TCPIP_ADAPTER_IF_STA, &mdns);
//      ESP_ERROR_CHECK( mdns_set_hostname(mdns, KALAM32_MDNS_NAME) );
//      ESP_ERROR_CHECK( mdns_set_instance(mdns, KALAM32_MDNS_NAME) );
//#endif

#ifdef CONFIG_MDNS_ENABLE
      ESP_ERROR_CHECK( mdns_init() );
      ESP_ERROR_CHECK(mdns_hostname_set(KALAM32_MDNS_NAME));
      ESP_ERROR_CHECK(mdns_instance_name_set(KALAM32_MDNS_NAME));
#endif
}

void app_main(void)
{
    nvs_flash_init();

    max30003_initchip(PIN_SPI_MISO,PIN_SPI_MOSI,PIN_SPI_SCK,PIN_SPI_CS);
	vTaskDelay(2/ portTICK_PERIOD_MS);

    kalam_wifi_init();
	
    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT_kalam,false, true, portMAX_DELAY);
    
#ifdef CONFIG_TCP_ENABLE					//enable it from makemenuconfig
    kalam_tcp_start();
#endif
   
}
