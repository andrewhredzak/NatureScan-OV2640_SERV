#ifndef NEO6_H
#define NEO6_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "driver/uart.h"

#define UART_NUM UART_NUM_1             // Use UART1 for NEO-6M
#define BAUD_RATE 9600                  // Matches NEO-6M default
#define DATA_BITS UART_DATA_8_BITS      // 8 data bits for 8N1
#define UART_PARITY UART_PARITY_DISABLE // No parity for 8N1
#define UART_STOP_BITS UART_STOP_BITS_1 // 1 stop bit for 8N1
#define UART_HW_FLOWCTRL UART_HW_FLOWCTRL_DISABLE // No flow control
#define FLOW_CTRL_THRESH 0              // Irrelevant with flow control disabled
#define TX_PIN 12  // og  14    Adjust based on your ESP32-CAM schematic
#define RX_PIN 13  // og  15   Adjust based on your ESP32-CAM schematic
#define RX_BUFFER_SIZE 256
#define NMEA_SENTENCE_MAX_LENGTH 82  // Maximum length of NMEA sentence (including $ and \r\n)


// Context: This structure will be populated with settings specific to your NEO-6M, 
// which uses 9600 baud, 8 data bits, no parity, and 1 stop bit.


//function prototypes

void uart_init(uart_port_t uart_num); 
void gps_event_handler(uint8_t *data, uint16_t len);
void gps_task(uart_port_t uart_num);


#ifdef __cplusplus
}
#endif
#endif /* NEO6_H */