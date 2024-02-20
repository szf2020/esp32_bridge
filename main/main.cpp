/*
 * This file is part of the ESP32-XBee distribution (https://github.com/nebkat/esp32-xbee).
 * Copyright (c) 2019 Nebojsa Cvetkovic.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <web_server.h>
#include <log.h>

#include <interface/web_socket_client.h>
#include <esp_sntp.h>
#include <core_dump.h>
#include <esp_ota_ops.h>
#include <stream_stats.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/ledc.h"
// #include "button.h"

#include "device_data.h"
#include "config.h"
#include "wifi.h"
#include "interface/socket_server.h"
#include "uart.h"
#include "tasks.h"
#include "common_protocol/include/common_protocol.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void app_main(void);

#ifdef __cplusplus
}
#endif
#define ESP32_FIRMWARE_VERSION CONFIG_APP_PROJECT_VER
static const char *TAG = "MAIN";

static char *reset_reason_name(esp_reset_reason_t reason);

board_info_t board_data;
board_status_t board_status;

// static void reset_button_task() {
//     QueueHandle_t button_queue = button_init(PIN_BIT(GPIO_NUM_0));
//     gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
//     while (true) {
//         button_event_t button_ev;
//         if (xQueueReceive(button_queue, &button_ev, 1000 / portTICK_PERIOD_MS)) {
//             if (button_ev.event == BUTTON_DOWN && button_ev.duration > 5000) {
//                 config_reset();
//                 vTaskDelay(2000 / portTICK_PERIOD_MS);
//                 esp_restart();
//             }
//         }
//     }
// }

static void sntp_time_set_handler(struct timeval *tv)
{
    ESP_LOGI(TAG, "Synced time from SNTP");
}

static void show_config_board(void)
{
    ESP_LOGI(TAG, "-------Show configuration------");
    ESP_LOGI(TAG, "board_data.input = %d", board_data.input);
    ESP_LOGI(TAG, "board_data.input = %d", board_data.output);

    ESP_LOGI(TAG, "----------------------------------------------------------------");
}

static void user_get_data_input(void)
{
    // set the default values when first time power up
    // get data input
    int ret = -1;
    uint8_t ui8 = 0;
    uint32_t ui32 = 0;
    ESP_LOGI(TAG, "user_get_data_input called");
    ret = config_get_primitive(CONF_ITEM(KEY_CONFIG_INPUT_PROTO_TYPE), &board_data.input);
    if (board_data.input == 0)
    {
        ui8 = E_SERIAL_UART;
        config_set(CONF_ITEM(KEY_CONFIG_INPUT_PROTO_TYPE), &ui8);
    }

    ret = config_get_primitive(CONF_ITEM(KEY_CONFIG_OUTPUT_PROTO_TYPE), &board_data.output);
    if (board_data.output == 0)
    {
        ui8 = E_BLUETOOTH;
        config_set(CONF_ITEM(KEY_CONFIG_INPUT_PROTO_TYPE), &ui8);
    }
}

void app_main()
{
    printf("main running\n");

    core_dump_check();

    // xTaskCreate(reset_button_task, "reset_button", 4096, NULL, TASK_PRIORITY_RESET_BUTTON, NULL);

    stream_stats_init();

    config_init();
    user_get_data_input();
    show_config_board();

    uart_init();
    esp_reset_reason_t reset_reason = esp_reset_reason();

    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    char elf_buffer[17];
    esp_ota_get_app_elf_sha256(elf_buffer, sizeof(elf_buffer));

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║ ESP32 XBee %-33s "
                  "║",
             app_desc->version);
    ESP_LOGI(TAG, "╠══════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Compiled: %8s %-25s "
                  "║",
             app_desc->time, app_desc->date);
    ESP_LOGI(TAG, "║ ELF SHA256: %-32s "
                  "║",
             elf_buffer);
    ESP_LOGI(TAG, "║ ESP-IDF: %-35s "
                  "║",
             app_desc->idf_ver);
    ESP_LOGI(TAG, "║ Firm-Ver: %s    "
                  "║",
             ESP32_FIRMWARE_VERSION);
    ESP_LOGI(TAG, "╟──────────────────────────────────────────────╢");
    ESP_LOGI(TAG, "║ Reset reason: %-30s "
                  "║",
             reset_reason_name(reset_reason));
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    esp_event_loop_create_default();
    vTaskDelay(pdMS_TO_TICKS(2500));

    net_init();
    wifi_init();
    web_server_init();

    // update board_status information
    snprintf(board_status.esp32_firmware_version, sizeof(board_status.esp32_firmware_version), "%s", ESP32_FIRMWARE_VERSION);
    snprintf(board_status.esp32_time_built, sizeof(board_status.esp32_time_built), " %8s %-25s ", app_desc->time, app_desc->date);
    // ESP_LOGI(TAG, "esp32_firmware_version = %s", board_status.esp32_firmware_version);
    // ESP_LOGI(TAG, "esp32_time_built = %s", board_status.esp32_time_built);
    common_protocol_init();

    wait_for_ip();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_set_time_sync_notification_cb(sntp_time_set_handler);
    sntp_init();

#ifdef DEBUG_HEAP
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));

        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);

        uart_nmea("$PESP,HEAP,FREE,%d/%d,%d%%", info.total_free_bytes,
                  info.total_allocated_bytes + info.total_free_bytes,
                  100 * info.total_free_bytes / (info.total_allocated_bytes + info.total_free_bytes));
    }
#endif
}

static char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason)
    {
    default:
    case ESP_RST_UNKNOWN:
        return "UNKNOWN";
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXTERNAL";
    case ESP_RST_SW:
        return "SOFTWARE";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INTERRUPT_WATCHDOG";
    case ESP_RST_TASK_WDT:
        return "TASK_WATCHDOG";
    case ESP_RST_WDT:
        return "OTHER_WATCHDOG";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    }
}