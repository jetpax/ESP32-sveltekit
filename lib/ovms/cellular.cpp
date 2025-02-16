#include "cellular.hpp"

#include <esp_event.h>
#include <esp_mac.h>
#include <sdkconfig.h>

#include "esp_modem_c_api_types.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "ovms_command.h"

statjic const char *TAG = "cellular";


#if defined(CONFIG_MODEM_FLOW_CONTROL_NONE)
#define MODEM_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_NONE
#elif defined(CONFIG_MODEM_FLOW_CONTROL_SW)
#define MODEM_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_SW
#elif defined(CONFIG_MODEM_FLOW_CONTROL_HW)
#define MODEM_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_HW
#endif

#define GPIO_OUTPUT_PWR_PIN (gpio_num_t) CONFIG_MODEM_PWR_PIN
#define GPIO_OUTPUT_MDM_DTR (gpio_num_t) CONFIG_MODEM_UART_DTR

#define MODEM_OUTPUTS_PIN_SEL       \
  ((1ULL << GPIO_OUTPUT_PWR_PIN) | \
   (1ULL << GPIO_OUTPUT_MDM_DTR))  

using namespace esp_modem;

namespace esp32m {
  namespace net {

  void cellular_status(int verbosity, OvmsWriter *writer, OvmsCommand *cmd,
                       int argc, const char *const *argv) {
    writer->puts("Hello World");
  }

    bool PppEvent::is(ip_event_t event) const {
      return _event == event;
    }

    bool PppEvent::is(Event &ev, PppEvent **r) {
      if (!ev.is(Type))
        return false;
      if (r)
        *r = (PppEvent *)&ev;
      return true;
    }

    bool PppEvent::is(Event &ev, ip_event_t event, PppEvent **r) {
      if (!ev.is(Type))
        return false;
      ip_event_t t = ((PppEvent &)ev)._event;
      if (t != event)
        return false;
      if (r)
        *r = (PppEvent *)&ev;
      return true;
    }

    void PppEvent::publish(ip_event_t event) {
      PppEvent ev(event);
      EventManager::instance().publish(ev);
    }

    static void ip_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data) {
      PppEvent::publish((ip_event_t)event_id);

      ESP_LOGD(TAG, "IP event! %" PRIu32, event_id);
      if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, (esp_netif_dns_type_t)0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR,IP2STR(&dns_info.ip.u_addr.ip4)); 
        esp_netif_get_dns_info(netif,(esp_netif_dns_type_t)1, &dns_info); 
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4)); 
        ESP_LOGI(TAG,"~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "GOT ip event!!!");
      } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
      } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR,
                 IPV62STR(event->ip6_info.ip));
      }
    }

    void ppp_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
      auto cellular = (Cellular *)arg;
      IpEvent::publish(cellular->netif(), (ip_event_t)event_id, event_data);
    }

    Cellular::Cellular(const char *name)
        : _name(name ? name : "cell") {}

    Cellular::~Cellular() {
      stop();
    }


    esp_err_t Cellular::start() {
      // setup GPIO outputs
      gpio_config_t io_conf = {}; 
      io_conf.intr_type = GPIO_INTR_DISABLE;  // disable interrupt
      io_conf.mode = GPIO_MODE_OUTPUT;        // set as output mode
      io_conf.pin_bit_mask = MODEM_OUTPUTS_PIN_SEL;  // bit mask of the pins to set
      io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // disable pull-down mode
      io_conf.pull_up_en = GPIO_PULLUP_DISABLE;      // disable pull-up mode
      gpio_config(&io_conf);  // configure GPIO with the given settings

      /* Wakeup modem */
      ESP_LOGI(TAG, "Powering up modem...");
      gpio_set_level(GPIO_OUTPUT_PWR_PIN, 0);
      vTaskDelay(pdMS_TO_TICKS(1000));
      gpio_set_level(GPIO_OUTPUT_PWR_PIN, 1);
      vTaskDelay(pdMS_TO_TICKS(15000));

      ESP_CHECK_RETURN(useNetif());
      ESP_CHECK_RETURN(useEventLoop());
      ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                 &ip_event_handler, NULL));
      ESP_ERROR_CHECK(esp_event_handler_register(
          NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &ppp_event_handler, NULL));

      // init the PPP netif, DTE and DCE respectively
      esp_modem_dce_config_t dce_config =
          ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_MODEM_PPP_APN);
      esp_netif_config_t ppp_netif_config = ESP_NETIF_DEFAULT_PPP();
      esp_netif_t *_ppp_if = esp_netif_new(&ppp_netif_config);
      assert(_ppp_if);

      /* Configure the DTE */
#if defined(CONFIG_MODEM_SERIAL_IS_UART)
        esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
        /* setup UART specific configuration based on kconfig options */
        dte_config.uart_config.tx_io_num = CONFIG_MODEM_UART_TX;
        dte_config.uart_config.rx_io_num = CONFIG_MODEM_UART_RX;
        dte_config.uart_config.rts_io_num = CONFIG_MODEM_UART_RTS;
        dte_config.uart_config.cts_io_num = CONFIG_MODEM_UART_CTS;
        dte_config.uart_config.flow_control = MODEM_FLOW_CONTROL;
        dte_config.uart_config.rx_buffer_size = CONFIG_MODEM_UART_RX_BUFFER_SIZE;
        dte_config.uart_config.tx_buffer_size = CONFIG_MODEM_UART_TX_BUFFER_SIZE;
        dte_config.uart_config.event_queue_size = CONFIG_MODEM_UART_EVENT_QUEUE_SIZE;
        dte_config.task_stack_size = CONFIG_MODEM_UART_EVENT_TASK_STACK_SIZE;
        dte_config.task_priority = CONFIG_MODEM_UART_EVENT_TASK_PRIORITY;
        dte_config.dte_buffer_size = CONFIG_MODEM_UART_RX_BUFFER_SIZE / 2;
        auto uart_dte = create_uart_dte(&dte_config);
        // level shifter is inverting, so invert again
        uart_set_line_inverse(dte_config.uart_config.port_num,
                              UART_SIGNAL_RXD_INV);

#if CONFIG_MODEM_DEVICE_BG96 == 1
        ESP_LOGI(TAG, "Initializing BG96 on UART...");
        auto _dce = create_BG96_dce(&dce_config, uart_dte, _ppp_if);
#elif CONFIG_MODEM_DEVICE_SIM800 == 1
        ESP_LOGI(TAG, "Initializing SIM800 on UART...");
        auto dce = create_SIM800_dce(&dce_config, uart_dte, _ppp_if);
#elif CONFIG_MODEM_DEVICE_SIM7000 == 1
        ESP_LOGI(TAG, "Initializing SIM7000 on UART...");
        auto dce = create_SIM7000_dce(&dce_config, uart_dte, _ppp_if);
#elif CONFIG_MODEM_DEVICE_SIM7070 == 1
        ESP_LOGI(TAG, "Initializing SIM7070 on UART...");
        auto dce = create_SIM7070_dce(&dce_config, uart_dte, _ppp_if);
#elif CONFIG_MODEM_DEVICE_SIM7600 == 1
        auto dce = create_SIM7600_dce(&dce_config, uart_dte, _ppp_if);
#elif CONFIG_MODEM_DEVICE_CUSTOM == 1
        ESP_LOGI(TAG, "Initializing custom modem on UART...");
        // FIXME need custom creator
        auto dce = create_generic_dce(&dce_config, uart_dte, _ppp_if);
#else
        ESP_LOGI(TAG, "Initializing generic modem on UART...");
        auto dce = create_generic_dce(&dce_config, uart_dte, _ppp_if);
#endif

#elif defined(CONFIG_MODEM_SERIAL_IS_USB)
#if CONFIG_MODEM_DEVICE_BG96 == 1
          ESP_LOGI(TAG, "Initializing BG96 module on USB...");
          struct esp_modem_usb_term_config usb_config =
              ESP_MODEM_BG96_USB_CONFIG();
#elif CONFIG_MODEM_DEVICE_SIM7600 == 1
        ESP_LOGI(TAG, "Initializing SIM7600 on USB...");
        struct esp_modem_usb_term_config usb_config =
            ESP_MODEM_SIM7600_USB_CONFIG();
#elif CONFIG_MODEM_DEVICE_A7670 == 1
        ESP_LOGI(TAG, "Initializing A7670 on USB...");
        struct esp_modem_usb_term_config usb_config =
            ESP_MODEM_A7670_USB_CONFIG();
#else
#error USB compatible modem not selected
#endif
          const esp_modem_dte_config_t dte_config =
              ESP_MODEM_DTE_DEFAULT_USB_CONFIG(usb_config);
          ESP_LOGI(TAG, "Waiting for USB device connection...");
          auto dte = create_usb_dte(&dte_config);

#if CONFIG_MODEM_DEVICE_BG96 == 1
          std::unique_ptr<DCE> dce =
              create_BG96_dce(&dce_config, dte, _ppp_if);
#elif CONFIG_MODEM_DEVICE_SIM7600 == 1 || CONFIG_MODEM_DEVICE_A7670 == 1
          std::unique_ptr<DCE> dce =
              create_SIM7600_dce(&dce_config, dte, _ppp_if);
#else
#error USB modem not selected
#endif

#else
#error No valid serial connection to modem selected
#endif

        assert(dce != nullptr);

        if (dte_config.uart_config.flow_control == ESP_MODEM_FLOW_CONTROL_HW) {
          if (command_result::OK != dce->set_flow_control(2, 2)) {
            ESP_LOGE(TAG, "Failed to set the set_flow_control mode");
            return 1;
          }
          ESP_LOGI(TAG, "set_flow_control OK");
        }

        // get module name
        std::string module_name;
        dce->get_module_name(module_name);
        ESP_LOGI(TAG, "%s initialized", module_name.c_str());
        OvmsCommand *cmd_cellular =
            OvmsCommandApp::instance(TAG).RegisterCommand(
                "cellular", "CELLULAR MODEM framework", cellular_status, "", 0,
                0, false);

        return ESP_OK;
        
    }

    esp_err_t Cellular::stop() {

      if (_ppp_if) {
        esp_netif_destroy(_ppp_if);
        _ppp_if = nullptr;
      }
      _ready = false;
      return ESP_OK;
    }

    // handle core events
    void Cellular::handleEvent(Event &ev) {
      Device::handleEvent(ev);
      PppEvent *ppp;
      if (PppEvent::is(ev, &ppp)) {
        ppp->claim(this);
        switch (ppp->event()) {
          case IP_EVENT_PPP_GOT_IP:
            logI("PPP link up");
            break;
          case IP_EVENT_PPP_LOST_IP:
            logI("PPP link down");
            break;
          default:
            break;
        }
      } else if (EventInit::is(ev, 0)) {
        start();
      } 
    }


    Cellular *useCellular(const char *name) {
      auto cell = new Cellular(name);
      return cell ;
    }    

  }  // namespace net
}  // namespace esp32m