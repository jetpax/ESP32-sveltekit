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
static const char *TAG = "housekeeping";

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include "ovms_housekeeping.h"
#include "ovms_peripherals.h"
#include "ovms_events.h"
#include "global.h"
#ifdef CONFIG_OVMS_SC_JAVASCRIPT_DUKTAPE
#include "ovms_script.h"
#endif
#include "ovms_config.h"
#include "ovms_metrics.h"
#include "metrics_standard.h"
#include "ovms_config.h"
#include "ovms_module.h"
#include "ovms_boot.h"
#include "vehicle.h"
#include "dbc_app.h"
#ifdef CONFIG_OVMS_COMP_SERVER_V2
#include "ovms_server_v2.h"
#endif
#ifdef CONFIG_OVMS_COMP_SERVER_V3
#include "ovms_server_v3.h"
#endif
#include "rom/rtc.h"
#ifdef CONFIG_OVMS_COMP_OBD2ECU
#include "obd2ecu.h"
#endif
#ifdef CONFIG_OVMS_COMP_CANOPEN)
#include "canopen.h"
#endif
#ifdef CONFIG_OVMS_COMP_TPMS
#include "ovms_tpms.h"
#endif
#ifdef CONFIG_OVMS_COMP_LOCATION
#include "ovms_location.h"
#endif
#include "ovms_notify.h"



#define AUTO_INIT_STABLE_TIME           120     // seconds after which an auto init boot is considered stable;
                                                // 120 seconds to take modem model auto detection into account
                                                // (late driver installation esp. after unscheduled reboot)
                                                // (Note: resolution = 10 seconds)
#define AUTO_INIT_INHIBIT_CRASHCOUNT    5

static int tick = 0;

void HousekeepingUpdate12V()
  {
#ifdef CONFIG_OVMS_COMP_ADC
  OvmsMetricFloat* m1 = MetricsStandard::instance().ms_v_bat_12v_voltage;
  if (m1 == NULL)
    return;
  if (MyPeripherals == NULL)
    return;

  // Allow the user to adjust the ADC conversion factor
  float f = OvmsConfig::instance(TAG).GetParamValueFloat("system.adc","factor12v");
  if (f == 0) f = 195.7;
  float v = (float)MyPeripherals->m_esp32adc->read() / f;
  // smooth out ADC errors & noise:
  if (m1->AsFloat() != 0)
    v = (m1->AsFloat() * 4 + v) / 5;
  v = trunc(v*100) / 100;
  if (v < 1.0) v=0;
  m1->SetValue(v);
  if (MetricsStandard::instance().ms_v_bat_12v_voltage_ref->AsFloat() == 0)
    MetricsStandard::instance().ms_v_bat_12v_voltage_ref->SetValue(OvmsConfig::instance(TAG).GetParamValueFloat("vehicle","12v.ref", 12.6));
#endif // #ifdef CONFIG_OVMS_COMP_ADC
  }

// Housekeeping 1s timer tick callback
void HousekeepingTicker1( TimerHandle_t timer )
  {
  // Workaround for FreeRTOS duplicate timer callback bug
  // (see https://github.com/espressif/esp-idf/issues/8234)
  static TickType_t last_tick = 0;
  TickType_t curr_tick = xTaskGetTickCount();
  // if (curr_tick < last_tick + xTimerGetPeriod(timer) - 3) return;
  last_tick = curr_tick;
  monotonictime++;

  MetricsStandard::instance(TAG).ms_m_monotonic->SetValue((int)monotonictime);
  MetricsStandard::instance().ms_m_timeutc->SetValue((int)time(NULL));

  HousekeepingUpdate12V();
  OvmsEvents::instance(TAG).SignalEvent("ticker.1", NULL);

  tick++;
  if ((tick % 10)==0) OvmsEvents::instance(TAG).SignalEvent("ticker.10", NULL);
  if ((tick % 60)==0) OvmsEvents::instance(TAG).SignalEvent("ticker.60", NULL);
  if ((tick % 300)==0) OvmsEvents::instance(TAG).SignalEvent("ticker.300", NULL);
  if ((tick % 600)==0) OvmsEvents::instance(TAG).SignalEvent("ticker.600", NULL);
  if ((tick % 3600)==0)
    {
    tick = 0;
    OvmsEvents::instance(TAG).SignalEvent("ticker.3600", NULL);
    }

  time_t rawtime;
  time ( &rawtime );
  struct tm* tmu = localtime(&rawtime);
  if (tmu->tm_sec == 0)
    {
    // Start of the minute, so signal a timer event
    char tev[16];
    sprintf(tev,"clock.%02d%02d",tmu->tm_hour,tmu->tm_min);
    OvmsEvents::instance(TAG).SignalEvent(tev, NULL);
    if ((tmu->tm_hour==0)&&(tmu->tm_min==0))
      {
      sprintf(tev,"clock.day%1d",tmu->tm_wday);
      OvmsEvents::instance(TAG).SignalEvent(tev, NULL);
      }
    }
  }

// Construct On First Use instantiation 
Housekeeping &Housekeeping::instance(const char* caller) {
    static bool initialized = false;
    if (!initialized) {
      initialized = true;
        ESP_LOGI(TAG, "COFU by %s", caller);
    }
    static Housekeeping i;
    return i;
}

Housekeeping::Housekeeping() {
  // OvmsConfig::instance(TAG).RegisterParam("system.adc", "ADC configuration", true, true);
  // OvmsConfig::instance(TAG).RegisterParam("auto", "Auto init configuration", true, true);

  // Register our events
  #undef bind  // Kludgy, but works
  using std::placeholders::_1;
  using std::placeholders::_2;
  OvmsEvents::instance(TAG).RegisterEvent(TAG,"housekeeping.init", std::bind(&Housekeeping::Init, this, _1, _2));
  OvmsEvents::instance(TAG).RegisterEvent(TAG,"ticker.10", std::bind(&Housekeeping::Metrics, this, _1, _2));
  OvmsEvents::instance(TAG).RegisterEvent(TAG,"ticker.300", std::bind(&Housekeeping::TimeLogger, this, _1, _2));

  // Fire off the event that causes us to be called back in Events tasks context
  OvmsEvents::instance(TAG).SignalEvent("housekeeping.init", NULL);
  }

Housekeeping::~Housekeeping() {
  }

void Housekeeping::Init(std::string event, void* data) {
  ESP_LOGI(TAG, "housekeeping.init on CPU core %d",xPortGetCoreID());
  ESP_LOGI(TAG, "reset_reason: cpu0=%d, cpu1=%d", rtc_get_reset_reason(0), rtc_get_reset_reason(1));

  tick = 0;
  m_timer1 = xTimerCreate("Housekeep ticker",1000 / portTICK_PERIOD_MS,pdTRUE,this,HousekeepingTicker1);
  xTimerStart(m_timer1, 0);

  ESP_LOGI(TAG, "Starting PERIPHERALS...");
  Peripherals* MyPeripherals = new Peripherals();


// #ifdef CONFIG_OVMS_COMP_EXT12V
//   MyPeripherals->m_ext12v->SetPowerMode(Off);
// #endif // #ifdef CONFIG_OVMS_COMP_EXT12V

//   // component auto init:
//   if (!OvmsConfig::instance(TAG).GetParamValueBool("auto", "init", true))
//     {
//     ESP_LOGW(TAG, "Auto init disabled (enable: config set auto init yes)");
//     }
//   else if (Boot::instance(TAG).GetEarlyCrashCount() >= AUTO_INIT_INHIBIT_CRASHCOUNT)
//     {
//     ESP_LOGE(TAG, "Auto init inhibited: too many early crashes (%d)", Boot::instance(TAG).GetEarlyCrashCount());
//     }
//   else
//     {
// #ifdef CONFIG_OVMS_COMP_MAX7317
//     ESP_LOGI(TAG, "Auto init max7317 (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
//     MyPeripherals->m_max7317->AutoInit();
// #endif // #ifdef CONFIG_OVMS_COMP_MAX7317

// #ifdef CONFIG_OVMS_COMP_EXT12V
//     ESP_LOGI(TAG, "Auto init ext12v (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
//     MyPeripherals->m_ext12v->AutoInit();
// #endif // CONFIG_OVMS_COMP_EXT12V

//   ESP_LOGI(TAG, "Auto init dbc (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
//   dbc::instance(TAG).AutoInit();


// #ifdef CONFIG_OVMS_COMP_CELLULAR
//     ESP_LOGI(TAG, "Auto init modem (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
//     MyPeripherals->m_cellular_modem->AutoInit();
// #endif // #ifdef CONFIG_OVMS_COMP_CELLULAR

ESP_LOGI(TAG, "Auto init vehicle (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
OvmsVehicleFactory::instance(TAG).AutoInit();

#ifdef CONFIG_OVMS_COMP_ESP32CAN
  ESP_LOGI(TAG, "Auto init CAN (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
  MyPeripherals->m_esp32can->SetPowerMode(Off);
#endif // #ifdef CONFIG_OVMS_COMP_ESP32CAN

#ifdef CONFIG_OVMS_COMP_CANOPEN
  ESP_LOGI(TAG, "Auto init CANopen (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
  CANopen::instance(TAG);
#endif

#ifdef CONFIG_OVMS_COMP_TPMS
  ESP_LOGI(TAG, "Auto init TPMS (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
  OvmsTPMS::instance(TAG);
#endif

#ifdef CONFIG_OVMS_COMP_LOCATION
  ESP_LOGI(TAG, "Auto init LOCATION (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
  OvmsLocations::instance(TAG);
#endif

ESP_LOGI(TAG, "Auto init NOTIFY (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
OvmsNotify::instance(TAG);



// #ifdef CONFIG_OVMS_COMP_OBD2ECU
//     ESP_LOGI(TAG, "Auto init obd2ecu (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
//     obd2ecuInit.AutoInit();
// #endif // CONFIG_OVMS_COMP_OBD2ECU

// #ifdef CONFIG_OVMS_SC_JAVASCRIPT_DUKTAPE
//     ESP_LOGI(TAG, "Auto init javascript (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
//     MyDuktape.AutoInitDuktape();
// #endif // #ifdef CONFIG_OVMS_SC_JAVASCRIPT_DUKTAPE

//     ESP_LOGI(TAG, "Auto init done (free: %zu bytes)", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
//     }

//   // OvmsEvents::instance(TAG).SignalEvent("system.start",NULL);

  Metrics(event,data); // Causes the metrics to be produced
  }

void Housekeeping::Metrics(std::string event, void* data) {
  OvmsMetricInt* m2 = MetricsStandard::instance().ms_m_tasks;
  if (m2 == NULL) return;

  m2->SetValue(uxTaskGetNumberOfTasks());

  OvmsMetricInt* m3 = MetricsStandard::instance().ms_m_freeram;
  if (m3 == NULL) return;

  uint32_t caps = MALLOC_CAP_8BIT;
  size_t free = heap_caps_get_free_size(caps);
  m3->SetValue(free);
  // set boot stable flag after some seconds uptime:
  // if (!Boot::instance(TAG).GetStable() && monotonictime >= AUTO_INIT_STABLE_TIME) {
  //   size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL);
  //   size_t free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT|MALLOC_CAP_INTERNAL);
  //   size_t lgst_8bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL);
  //   size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  //   size_t lgst_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  //   ESP_LOGI(TAG, "System considered stable (RAM: 8b=%zu-%zu 32b=%zu SPI=%zu-%zu)",
  //     lgst_8bit, free_8bit, free_32bit-free_8bit, lgst_spiram, free_spiram);
  //   Boot::instance(TAG).SetStable();
  // …and send debug crash data as necessary:
  // Boot::instance(TAG).NotifyDebugCrash();
  // }
}

void Housekeeping::TimeLogger(std::string event, void* data) {
  time_t rawtime;
  time ( &rawtime );
  struct tm* tmu = localtime(&rawtime);
  char tb[64];
  if (strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S %Z", tmu) > 0) {
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL);
    size_t free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT|MALLOC_CAP_INTERNAL);
    size_t lgst_8bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t lgst_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "%.24s (RAM: 8b=%zu-%zu 32b=%zu SPI=%zu-%zu)",
      tb, lgst_8bit, free_8bit, free_32bit-free_8bit, lgst_spiram, free_spiram);
    }
  }
