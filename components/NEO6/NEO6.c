#include <stdio.h>
#include <string.h>
#include "NEO6.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "NEO6.c";  //tag for source code

void uart_init(uart_port_t uart_num) {
    /* **brief this function initializes UART configuration parameters.
        Key configuration constants (e.g., data bits, parity) are provided by uart.h,
         while some values (e.g., baud rate) are user-defined. */
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = DATA_BITS,
        .parity = UART_PARITY,
        .stop_bits = UART_STOP_BITS,
        .flow_ctrl = UART_HW_FLOWCTRL,
        .rx_flow_ctrl_thresh = FLOW_CTRL_THRESH,
    };
    
    ESP_LOGI(TAG, "UART Configuration:");
    ESP_LOGI(TAG, "- Port: UART%d", uart_num);
    ESP_LOGI(TAG, "- Baud Rate: %d", BAUD_RATE);
    ESP_LOGI(TAG, "- Data Bits: %d", DATA_BITS);
    ESP_LOGI(TAG, "- Stop Bits: %d", UART_STOP_BITS);
    ESP_LOGI(TAG, "- Parity: %d", UART_PARITY);
    ESP_LOGI(TAG, "- Flow Control: %d", UART_HW_FLOWCTRL);
    ESP_LOGI(TAG, "- TX Pin: %d", TX_PIN);
    ESP_LOGI(TAG, "- RX Pin: %d", RX_PIN);
    
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_LOGI(TAG, "UART parameter configuration done.");

    // Set UART pins(TX: I15, RX: I14, RTS: IO18, CTS: IO19)
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART pin configuration done."); // Setup UART buffered IO with event queue

    ESP_ERROR_CHECK(uart_driver_install(uart_num, RX_BUFFER_SIZE, 0, 0, NULL, 0));
    ESP_LOGI(TAG, "UART driver installed.");
}



//will remove when process_nmea_sentence is implemented
void gps_event_handler(uint8_t *data, uint16_t len) {
    // Null-terminate the data for safe printing
    if (len >= RX_BUFFER_SIZE) {
        len = RX_BUFFER_SIZE - 1; // Prevent overflow
    }
    data[len] = '\0';

    // Log the raw NMEA sentence to console (UART1)
    //ESP_LOGI(TAG, "GPS Data: %s", (char *)data);
}



char uart_buffer[RX_BUFFER_SIZE];
int buffer_pos = 0;





void process_nmea_sentence(char *sentence) {
    // Check if the sentence starts with $GPRMC
    if (strncmp(sentence, "$GPRMC", 6) != 0) {
        return; // Not a GPRMC sentence, skip
    }

    // Tokenize the sentence using comma as delimiter
    char *fields[20]; // Array to hold pointers to fields
    int field_count = 0;

    fields[field_count++] = sentence; // First field starts at the beginning
    for (char *p = sentence; *p != '\0'; p++) {
        if (*p == ',') {
            *p = '\0'; // Replace comma with null terminator
            fields[field_count++] = p + 1; // Next field starts after the comma
        }
        if (*p == '*') break; // Stop at checksum
    }

    // Ensure we have the expected number of fields (at least 12 for GPRMC)
    if (field_count < 12) {
        printf("Incomplete GPRMC sentence\n");
        return;
    }

    // Extract fields
    char *time = fields[1];        // e.g., "123519.7"
    char *status = fields[2];      // e.g., "A"
    char *latitude = fields[3];    // e.g., "4807.038"
    char *lat_dir = fields[4];     // e.g., "N"
    char *longitude = fields[5];   // e.g., "01131.000"
    char *lon_dir = fields[6];     // e.g., "E"
    char *speed = fields[7];       // e.g., "622.4"
    char *course = fields[8];      // e.g., "984.4"
    char *date = fields[9];        // e.g., "230394"
    char *variation = fields[10];  // e.g., "003.1"
    char *var_dir = fields[11];    // e.g., "W"

    // Convert latitude and longitude to degrees and minutes
    float lat_deg = atoi(latitude) / 100;
    float lat_min = (atof(latitude) - (int)lat_deg * 100);
    lat_deg += lat_min / 60.0;

    float lon_deg = atoi(longitude) / 100;
    float lon_min = (atof(longitude) - (int)lon_deg * 100);
    lon_deg += lon_min / 60.0;

    // Format time and date
    char time_str[11];
    char date_str[12];
    snprintf(time_str, sizeof(time_str), "%c%c:%c%c:%c%c.%c",
             time[0], time[1], time[2], time[3], time[4], time[5], time[6]);
    snprintf(date_str, sizeof(date_str), "%c%c/%c%c/%c%c%c%c",
             date[0], date[1], date[2], date[3], '1', '9', date[4], date[5]);

    // Print human-readable output
    printf("Status: %s\nUTC Time: %s, Date: %s\nLatitude: %.3f° %s, Longitude: %.3f° %s\n"
           "Speed: %s knots, Course: %s°, Variation: %s° %s\n",
           status[0] == 'A' ? "Active" : "Void",time_str,date_str,  
           lat_deg, lat_dir, lon_deg, lon_dir,
           speed, course, variation, var_dir);
    printf("\n");
}









void gps_task(uart_port_t uart_num) {
    uint8_t buffer[RX_BUFFER_SIZE];

    while (1) {
        // Read data from UART1
        //ESP_LOGI(TAG, "uart_num:%d", uart_num);
        int len = uart_read_bytes(uart_num, buffer, RX_BUFFER_SIZE - 1, pdMS_TO_TICKS(100));
        //ESP_LOGI(TAG, "len:%d", len);
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                uart_buffer[buffer_pos++] = buffer[i];

                // Check for end of NMEA sentence (\r\n)
                if (buffer_pos >= 2 && uart_buffer[buffer_pos - 2] == '\r' && uart_buffer[buffer_pos - 1] == '\n') {
                    uart_buffer[buffer_pos] = '\0'; // Null-terminate the string
                    process_nmea_sentence(uart_buffer);
                    buffer_pos = 0; // Reset buffer position
                    }
            

                // Prevent buffer overflow
                if (buffer_pos >= RX_BUFFER_SIZE - 1) {
                    buffer_pos = 0; // Reset on overflow
                }
            }
            if (len < 0){
                 ESP_LOGE(TAG, "UART read error: %d",len);
            } else {
                //ESP_LOGI(TAG, "in else.");
                gps_event_handler(buffer, len);
            }
          
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Changed to 100ms.
    }
}
