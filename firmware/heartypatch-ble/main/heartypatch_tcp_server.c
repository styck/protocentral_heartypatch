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
#include "esp_bt.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/sdmmc_host.h"

#include "heartypatch_tcp_server.h"
#include "mdns.h"
#include "esp_log.h"
#include "heartypatch_main.h"
#include "heartypatch_max30003.h"
#include "heartypatch_tcp_server.h"

#define TAG "heartypatch:"
#define HEARTYPATCH_TCP

int connectedflag = 0;
int total_data = 0;

/*AP info and tcp_server info*/
#define DEFAULT_SSID CONFIG_TCP_PERF_WIFI_SSID
#define DEFAULT_PWD CONFIG_TCP_PERF_WIFI_PASSWORD
#define DEFAULT_PORT 4567
#define DEFAULT_SERVER_IP CONFIG_TCP_PERF_SERVER_IP
#define DEFAULT_PKTSIZE 150 //CONFIG_TCP_PERF_PKT_SIZE
#define MAX_STA_CONN 1 //how many sta can be connected(AP mode)
#define PACK_BYTE_IS 100

/*socket*/
static int server_socket = 0;
static struct sockaddr_in server_addr;
static struct sockaddr_in client_addr;
static unsigned int socklen = sizeof(client_addr);
static int connect_socket = 0;

extern uint8_t rtor_detected;
extern SemaphoreHandle_t updateRRSemaphore ;

//send data
static void send_data(void *pvParameters)
{
    uint8_t* data_packet;
    uint8_t databuff[DEFAULT_PKTSIZE];
    memset(databuff, PACK_BYTE_IS, DEFAULT_PKTSIZE);
    data_packet=databuff;
    vTaskDelay(100/portTICK_RATE_MS);
    ESP_LOGI(TAG, "start sending...");
    while(1)
    {
       
	 {
		  xSemaphoreTake( updateRRSemaphore, portMAX_DELAY );
		  printf("tcp HR:%d" ,HR);
		  data_packet[0] =  0X0a ;			//packet header
		  data_packet[1] =  0xfa ;
		  data_packet[2] =  12 ;
		  data_packet[3] =  00 ;
		  data_packet[4] =  02 ;
		
		  data_packet[5] =  00;				//since rtor detection is enabled, ECG data is not available.
		  data_packet[6] =  00;
		  data_packet[7] =  00;
		  data_packet[8] =  00;
		 
		  data_packet[9] =  (uint8_t)RR;		//RR value
		  data_packet[10] =  RR>>8;
		  data_packet[11] =  RR>>16 ;
		  data_packet[12] =  RR>>24;
		 
		  data_packet[13] =  (uint8_t)HR ;	//HR value
		   
		  data_packet[14] =  00;
		  data_packet[15] =  00;
		  data_packet[16] =  00;
		  data_packet[17] =  00;
		  data_packet[18] =  0x0b;


	 	  send(connect_socket,data_packet,19 , 0);
		  rtor_detected = false;

	 }
	 vTaskDelay(2/portTICK_RATE_MS);
    }
}



char* tcpip_get_reason(int err)
/**
 * @brief Default MQTT port is pulled from the aws_iot_config.h
 */
{
    switch (err) {
	case 0:
	    return "reason: other reason";
	case ENOMEM:
	    return "reason: out of memory";
	case ENOBUFS:
	    return "reason: buffer error";
	case EWOULDBLOCK:
	    return "reason: timeout, try again";
	case EHOSTUNREACH:
	    return "reason: routing problem";
	case EINPROGRESS:
	    return "reason: operation in progress";
	case EINVAL:
	    return "reason: invalid value";
	case EADDRINUSE:
	    return "reason: address in use";
	case EALREADY:
	    return "reason: conn already connected";
	case EISCONN:
	    return "reason: conn already established";
	case ECONNABORTED:
	    return "reason: connection aborted";
	case ECONNRESET:
	    return "reason: connection is reset";
	case ENOTCONN:
	    return "reason: connection closed";
	case EIO:
	    return "reason: invalid argument";
	case -1:
	    return "reason: low level netif error";
	default:
	    return "reason not found";
    }
}

int show_socket_error_code(int socket)
{
    int result;
    u32_t optlen = sizeof(int);
    getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen);
    ESP_LOGI(TAG, "socket error %d reason: %s", result, tcpip_get_reason(result));
    return result;
}

int check_socket_error_code()
{
    int ret;
#if ESP_TCP_MODE_SERVER
    ESP_LOGI(TAG, "check server_socket");
    ret = show_socket_error_code(server_socket);
    if(ret == ECONNRESET)
	return ret;
#endif
    ESP_LOGI(TAG, "check connect_socket");
    ret = show_socket_error_code(connect_socket);
    if(ret == ECONNRESET)
	return ret;
    return 0;
}

void close_socket()
{
    close(connect_socket);
    close(server_socket);
}

//use this esp32 as a tcp server. return ESP_OK:success ESP_FAIL:error
esp_err_t create_tcp_server()
{
    ESP_LOGI(TAG, "server socket....port=%d\n", DEFAULT_PORT);
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
    	 show_socket_error_code(server_socket);
	     return ESP_FAIL;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
    	 show_socket_error_code(server_socket);
	     close(server_socket);
	      return ESP_FAIL;
    }
    if (listen(server_socket, 5) < 0)
    {
    	  show_socket_error_code(server_socket);
	      close(server_socket);
	      return ESP_FAIL;
    }

    connect_socket = accept(server_socket, (struct sockaddr*)&client_addr, &socklen);
    if (connect_socket<0)
    {
    	  show_socket_error_code(connect_socket);
	      close(server_socket);
	      return ESP_FAIL;
    }
    /*connection established，now can send/recv*/
    ESP_LOGI(TAG, "tcp connection established!");
    return ESP_OK;
}

//this task establish a TCP connection and receive data from TCP
void tcp_conn(void *pvParameters)
{
    ESP_LOGI(TAG, "task tcp_conn start.");

    /*create tcp socket*/
    int socket_ret;

    vTaskDelay(3000 / portTICK_RATE_MS);

    ESP_LOGI(TAG, "create_tcp_server.");
    socket_ret=create_tcp_server();
    if(ESP_FAIL == socket_ret)
    {
    	ESP_LOGI(TAG, "create tcp socket error,stop.");
    	vTaskDelete(NULL);
    }

    /*create a task to tx/rx data*/
    TaskHandle_t tx_rx_task;
    xTaskCreate(&send_data, "send_data", 4096, NULL, 4, &tx_rx_task);

    while (1)
    {
      	vTaskDelay(3000 / portTICK_RATE_MS);//every 3s
  	    int err_ret = check_socket_error_code();
  	    if (err_ret == ECONNRESET)
        {
      		ESP_LOGI(TAG, "disconnected... stop.");
          close_socket();
          socket_ret=create_tcp_server();
          //break;
  	    }
    }

    close_socket();
    vTaskDelete(tx_rx_task);
    vTaskDelete(NULL);
}

void heartypatch_tcp_start(void)
{
	xTaskCreate(&tcp_conn, "tcp_conn", 4096, NULL, 5, NULL);
 
}

/*********************** END TCP Server Code *****************/
