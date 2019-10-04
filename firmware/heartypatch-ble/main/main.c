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

#include "esp_log.h"
#include "heartypatch_main.h"
#include "heartypatch_max30003.h"
#include "heartypatch_tcp_server.h"
#include "heartypatch_ble.h"
#include "heartypatch_adc.h"

#define TAG "heartypatch:"
#define delay_ms(ms) vTaskDelay((ms) / portTICK_RATE_MS)

#define BUF_SIZE  1000

extern xSemaphoreHandle print_mux;
char uart_data[50];
const int uart_num = UART_NUM_1;
uint8_t* db;

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT_kalam = BIT0;

uint8_t* db;
unsigned int global_heartRate ; 

extern QueueHandle_t xQueue_tcp;

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

void heartypatch_wifi_init(void)
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

}


void app_main(void)
{

    // Initialize NVS.
	esp_err_t ret;
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
	
	xQueue_tcp = xQueueCreate(20, sizeof( struct Tcp_Message *));
	if( xQueue_tcp==NULL )
	{
		ESP_LOGI(TAG, "Failed to create Queue..!");
	}
	
    max30003_initchip(PIN_SPI_MISO,PIN_SPI_MOSI,PIN_SPI_SCK,PIN_SPI_CS);

    heartypatch_start_max30003();
    heartypatch_adc_start();

	vTaskDelay(2000/ portTICK_PERIOD_MS);		//give sometime for max to settle

#ifdef CONFIG_BLE_MODE_ENABLE
	heartypatch_ble_Init();		
#endif
	
#ifdef CONFIG_WIFIMODE_ENABLE					//configure the ssid/password under makemenuconfig/heartypatch configuration.
	heartypatch_wifi_init();
    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT_kalam,false, true, portMAX_DELAY);
    
	/*only hr and rr is sending through tcp, to plot ECG, you need to configure the max30003 to read ecg data */
	vTaskDelay(500/ portTICK_PERIOD_MS);
    heartypatch_tcp_start();
#endif
     
	
}
