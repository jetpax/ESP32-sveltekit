/*
;    Project:       Open Vehicle Monitor System
;    Date:          14th March 2017
;
;    Changes:
;    1.0  Initial release
;
;    (C) 2011       Michael Stegen / Stegen Electronics
;    (C) 2011-2017  Mark Webb-Johnson
;    (C) 2011        Sonny Chen @ EPRO/DX
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
*/

#include "ovms_log.h"
static const char *TAG = "peripherals";

#include <stdio.h>
#include <string.h>
#include <cstdio>
#include "ovms_config.h"
#include "ovms_command.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"
#include "ovms_peripherals.h"
#include "esp_idf_version.h"
#include "can.h"

#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_mac.h>
#endif
/*
Initializes various components of the system, including:

SPI bus: Initializes the SPI bus if components like MAX7317 or MCP2515 are used.
MAX7317 I/O Expander: If enabled, creates an instance of the max7317 class for interfacing with the MAX7317 chip.
ESP32 CAN: If enabled, creates an instance of the esp32can class for using the ESP32's built-in CAN controller.
ESP32 WIFI: If enabled, creates an instance of the esp32wifi class for managing WiFi connectivity.
ESP32 BLUETOOTH: If enabled, creates an instance of the esp32bluetooth class for Bluetooth communication.
ESP32 ADC: If enabled, creates an instance of the esp32adc class for reading analog values through the ADC.
MCP2515 CAN 1 and 2: If enabled, creates two instances of the mcp2515 class for interfacing with two MCP2515 CAN transceivers.
External SWCAN (MCP2515+TH8056 driver): If enabled, creates an instance of the swcan class for using an external SWCAN module with an MCP2515 and TH8056 driver.
SD card: If enabled, creates an instance of the sdcard class for accessing an SD card.
Cellular modem: If enabled, creates an instance of the modem class for interfacing with a cellular modem.
External 12V supply: If enabled, creates an instance of the ext12v class for monitoring and managing an external 12V supply.
The constructor comments also mention other components like OBD2 ECU that are not explicitly handled in this code snippet.

The code also initializes different components based on configuration settings:
  Network: Sets the MAC address if defined in the configuration and initializes the network interface (Wi-Fi or cellular).
  GPIO: Sets the direction and initial state of various GPIO pins used for communication with other components.
  SPI bus: Initializes the SPI bus for communication with SPI devices like MAX7317 and MCP2515.
  MAX7317: Initializes the MAX7317 I/O expander (if enabled).
  MCP2515: Initializes the MCP2515 CAN bus controllers (if enabled).
  ESP32CAN: Initializes the ESP32 internal CAN bus controller (if enabled).
  ESP32 Wi-Fi: Initializes the ESP32 Wi-Fi interface (if enabled).
  ESP32 Bluetooth: Initializes the ESP32 Bluetooth interface (if enabled).
  ESP32 ADC: Initializes the ESP32 ADC for analog readings (if enabled).
  SD card: Initializes the SD card module (if enabled).
  Cellular modem: Initializes the cellular modem (if enabled).
  Ext12V: Initializes the external 12V power supply monitoring (if enabled).

Overall, this code serves as the initialization point for various hardware and communication interfaces used in the OVMS, laying the foundation for further operations and data acquisition.*/



Peripherals::Peripherals()
  {
  ESP_LOGI(TAG, "Initialising OVMS Peripherals...");

#if defined(CONFIG_OVMS_COMP_WIFI)||defined(CONFIG_OVMS_COMP_CELLULAR)
  if (OvmsConfig::instance(TAG).IsDefined("network","mac"))
    {
    std::string mac = OvmsConfig::instance(TAG).GetParamValue("network", "mac");
    int mac_addr_k[6];
    uint8_t mac_addr[8];
    memset(mac_addr,0,sizeof(mac_addr));
    if (std::sscanf(mac.c_str(),
                    "%02x:%02x:%02x:%02x:%02x:%02x",
                    &mac_addr_k[0], &mac_addr_k[1], &mac_addr_k[2],
                    &mac_addr_k[3], &mac_addr_k[4], &mac_addr_k[5]) == 6)
      {
      for (int k=0;k<6;k++) mac_addr[k] = mac_addr_k[k];
      esp_base_mac_addr_set(mac_addr);
      ESP_LOGI(TAG, "  Base network MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);
      }
    }
#if ESP_IDF_VERSION_MAJOR >= 4
  ESP_LOGI(TAG, "  ESP-NETIF");
  ESP_ERROR_CHECK(esp_netif_init());
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
#else
  ESP_LOGI(TAG, "  TCP/IP Adaptor");
  tcpip_adapter_init();
#endif
#endif // #if defined(CONFIG_OVMS_COMP_WIFI)||defined(CONFIG_OVMS_COMP_CELLULAR)

  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

  gpio_set_direction((gpio_num_t)CONFIG_SPI_MISO, GPIO_MODE_INPUT);
  gpio_set_direction((gpio_num_t)CONFIG_SPI_MOSI, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)CONFIG_SPI_CLK, GPIO_MODE_OUTPUT);

#ifdef CONFIG_OVMS_COMP_MAX7317
  gpio_set_direction((gpio_num_t)SPI_MAX7317_CS, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)SPI_MAX7317_CS, 1); // to prevent SPI crosstalk during initialization
#endif // #ifdef CONFIG_OVMS_COMP_MAX7317

#ifdef CONFIG_OVMS_COMP_MCP2515
  gpio_set_direction((gpio_num_t)CONFIG_CAN_1_CS, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)CONFIG_CAN_2_CS, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)CONFIG_CAN_1_INT, GPIO_MODE_INPUT);
  gpio_set_direction((gpio_num_t)CONFIG_CAN_2_INT, GPIO_MODE_INPUT);
  gpio_set_level((gpio_num_t)CONFIG_CAN_1_CS, 1); // to prevent SPI crosstalk during initialization
  gpio_set_level((gpio_num_t)CONFIG_CAN_2_CS, 1); // to prevent SPI crosstalk during initialization
#endif // #ifdef CONFIG_OVMS_COMP_MCP2515

#ifdef CONFIG_OVMS_COMP_EXTERNAL_SWCAN
  gpio_set_direction((gpio_num_t)MCP2515_SWCAN_CS, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)MCP2515_SWCAN_INT, GPIO_MODE_INPUT);
  gpio_set_level((gpio_num_t)MCP2515_SWCAN_CS, 1); // to prevent SPI crosstalk during initialization
#endif // #ifdef CONFIG_OVMS_COMP_EXTERNAL_SWCAN

#ifdef CONFIG_OVMS_COMP_SDCARD
  gpio_set_direction((gpio_num_t)CONFIG_SDMMC_CLK, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)CONFIG_SDMMC_CMD, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)CONFIG_SDCARD_CD, GPIO_MODE_INPUT);

  // GPIOs CMD, D0 should have external 10k pull-ups.
  // Internal pull-ups are not sufficient. However, enabling internal pull-ups
  // does make a difference some boards, so we do that here.
  gpio_set_pull_mode((gpio_num_t)CONFIG_SDMMC_CMD, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
  gpio_set_pull_mode((gpio_num_t)CONFIG_SDMMC_D0, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
#endif // #ifdef CONFIG_OVMS_COMP_SDCARD

  // ESP_LOGI(TAG, "  ESP32 system");
  // m_esp32 = new esp32system("esp32");


#if defined(CONFIG_OVMS_COMP_MAX7317)||defined(CONFIG_OVMS_COMP_MCP2515)||defined(CONFIG_OVMS_COMP_EXTERNAL_SWCAN)
  m_spibus = new spi("spi2", SPI2_HOST, CONFIG_SPI_MISO, CONFIG_SPI_MOSI, CONFIG_SPI_CLK);
#endif

#ifdef CONFIG_OVMS_COMP_MAX7317
  ESP_LOGI(TAG, "  MAX7317 I/O Expander");
  m_max7317 = new max7317("egpio", m_spibus, SPI2_HOST, 20000000, SPI_MAX7317_CS);
#endif // #ifdef CONFIG_OVMS_COMP_MAX7317

#ifdef CONFIG_OVMS_COMP_ESP32CAN
  ESP_LOGI(TAG, "  ESP32 CAN");
  m_esp32can = new esp32can("can1", CONFIG_CAN_0_TX, CONFIG_CAN_0_RX);
  // dummy call to instantiate can
  can::instance(TAG).HasLogger();
#endif // #ifdef CONFIG_OVMS_COMP_ESP32CAN

#ifdef CONFIG_OVMS_COMP_WIFI
  ESP_LOGI(TAG, "  ESP32 WIFI");
  m_esp32wifi = new esp32wifi("wifi");
#endif // #ifdef CONFIG_OVMS_COMP_WIFI

#ifdef CONFIG_OVMS_COMP_BLUETOOTH
  ESP_LOGI(TAG, "  ESP32 BLUETOOTH");
  m_esp32bluetooth = new esp32bluetooth("bluetooth");
#endif // #ifdef CONFIG_OVMS_COMP_BLUETOOTH

#ifdef CONFIG_OVMS_COMP_ADC
  ESP_LOGI(TAG, "  ESP32 ADC");
  m_esp32adc = new esp32adc("adc", ADC1_CHANNEL_0, ADC_WIDTH_BIT_12, ADC_ATTEN_DB_11);

#endif // #ifdef CONFIG_OVMS_COMP_ADC

#ifdef CONFIG_OVMS_COMP_MCP2515
  ESP_LOGI(TAG, "  MCP2515 CAN 1/2");
  m_mcp2515_1 = new mcp2515("can2", m_spibus, 2 * 1000 * 1000, CONFIG_CAN_1_CS, CONFIG_CAN_1_INT);
  ESP_LOGI(TAG, "  MCP2515 CAN 2/2");
  m_mcp2515_2 = new mcp2515("can3", m_spibus, 2 * 1000 * 1000, CONFIG_CAN_2_CS, CONFIG_CAN_2_INT);
#endif // #ifdef CONFIG_OVMS_COMP_MCP2515

#ifdef CONFIG_OVMS_COMP_EXTERNAL_SWCAN
  ESP_LOGI(TAG, "  can3/swcan (MCP2515 + TH8056 DRIVER)");

  // External SWCAN module with MCP2515. Here we use software CS (maximum 3 HW CS pins already used)
  m_mcp2515_swcan = new swcan("can4", m_spibus, 10000000, MCP2515_SWCAN_CS, MCP2515_SWCAN_INT, false);
#endif // #ifdef CONFIG_OVMS_COMP_EXTERNAL_SWCAN

#ifdef CONFIG_OVMS_COMP_SDCARD
  ESP_LOGI(TAG, "  SD CARD");
  m_sdcard = new sdcard("sdcard", true, true, CONFIG_SDCARD_CD);
#endif // #ifdef CONFIG_OVMS_COMP_SDCARD

#ifdef CONFIG_OVMS_COMP_CELLULAR
  ESP_LOGI(TAG, "  CELLULAR MODEM");
  gpio_config_t gpio_conf =
    {
    .pin_bit_mask = BIT(CONFIG_MODEM_UART_RX),
    .mode = GPIO_MODE_OUTPUT ,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
    };
  gpio_config( &gpio_conf );
  gpio_conf =
    {
    .pin_bit_mask = BIT(CONFIG_MODEM_UART_TX),
    .mode = GPIO_MODE_INPUT ,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
    };
  gpio_config( &gpio_conf );
  m_cellular_modem = new modem("cellular", UART_NUM_1, 115200, CONFIG_MODEM_UART_RX, CONFIG_MODEM_UART_TX, CONFIG_MODEM_PWR, CONFIG_MODEM_DTR);
#endif // #ifdef CONFIG_OVMS_COMP_CELLULAR

#ifdef CONFIG_OVMS_COMP_OBD2ECU
  m_obd2ecu = NULL;
#endif // #ifdef CONFIG_OVMS_COMP_OBD2ECU

#ifdef CONFIG_OVMS_COMP_EXT12V
  m_ext12v = new ext12v("ext12v");
#endif // #ifdef CONFIG_OVMS_COMP_EXT12V
  }

Peripherals::~Peripherals()
  {
#if defined(CONFIG_OVMS_COMP_WIFI)||defined(CONFIG_OVMS_COMP_CELLULAR)

#if ESP_IDF_VERSION_MAJOR >= 4
  esp_netif_deinit();
#endif

#endif
  }
