#include <driver/gpio.h>

#include "ovms_v3.hpp"
#include "ovms_events.h"
#include "metrics_standard.h"

static const char *TAG = "ovmsV3";
size_t OvmsV3Modifier = 0;
size_t OvmsV3Reader = 0;


OvmsV3::OvmsV3() : _metrics_filter("ovms-server-v3") {
  _sendall = false;
  _lasttx = 0;
  _lasttx_stream = 0;
  _lasttx_sendall = 0;
  _peers = 0;
  _streaming = 0;
  _updatetime_idle = 600;
  _updatetime_connected = 60;
  _updatetime_awake = _updatetime_idle;
  _updatetime_on = _updatetime_idle;
  _updatetime_charging = _updatetime_idle;
  _updatetime_sendall = 0;
  #undef bind 
  using std::placeholders::_1;
  using std::placeholders::_2;
  OvmsMetrics::instance(TAG).RegisterListener(TAG, "*", std::bind(&OvmsV3::MetricModified, this, _1));
  OvmsEvents::instance().RegisterEvent(TAG,"ticker.1", std::bind(&OvmsV3::Ticker1, this, _1, _2));
  OvmsEvents::instance().RegisterEvent(TAG,"ticker.60", std::bind(&OvmsV3::Ticker60, this, _1, _2));
  OvmsEvents::instance().RegisterEvent(TAG,"config.changed", std::bind(&OvmsV3::EventListener, this, _1, _2));
  OvmsEvents::instance().RegisterEvent(TAG,"config.mounted", std::bind(&OvmsV3::EventListener, this, _1, _2));
  // TBD should convert this to OVMS event
  esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &OvmsV3::handleEvent, NULL);

}

void OvmsV3::MetricModified(OvmsMetric* metric) {
  if (!MetricsStandard::instance().ms_s_v3_connected->AsBool()) return;
  if (_streaming) {
    metric->ClearModified(OvmsV3Modifier);
    TransmitMetric(metric);
  }
}

void OvmsV3::TransmitAllMetrics() {
  OvmsMetric* metric = OvmsMetrics::instance().m_first;
  while (metric != NULL) {
    metric->ClearModified(OvmsV3Modifier);
    if (!metric->AsString().empty())
      {
      TransmitMetric(metric);
      }
    metric = metric->m_next;
  }
}

void OvmsV3::TransmitMetric(OvmsMetric* metric) {
  auto const metric_name = metric->m_name;
  if (!_metrics_filter.CheckFilter(metric_name))
    return;
  std::string topic(_topic_prefix);
  topic.append("metric/");
  topic.append(metric_name);
  // Replace '.' inside the metric name by '/' for MQTT like namespacing.
  for(size_t i = _topic_prefix.length(); i < topic.length(); i++) {
      if(topic[i] == '.')
        topic[i] = '/';
  }
  std::string val = metric->AsString();
  // bool Mqtt::enqueue(const char *topic, const char *message, int qos, bool retain, bool store)
  // net::useMqtt()->enqueue(topic.c_str(), val.c_str(), 0, true, true);
  ESP_LOGI(TAG, "Tx metric %s=%s",topic.c_str(),val.c_str());
}

void OvmsV3::TransmitModifiedMetrics() {
  OvmsMetric* metric =OvmsMetrics::instance().m_first;
  while (metric != NULL)
    {
    if (metric->IsModifiedAndClear(OvmsV3Modifier))
      {
      TransmitMetric(metric);
      }
    metric = metric->m_next;
    }
  }

void OvmsV3::EventListener(std::string event, void* data) {
  if (event == "config.changed" || event == "config.mounted")
    {
    ConfigChanged((OvmsConfigParam*) data);
    }
}

void OvmsV3::ConfigChanged(OvmsConfigParam* param) {
  _streaming = OvmsConfig::instance().GetParamValueInt("vehicle", "stream", 0);
  _updatetime_connected = OvmsConfig::instance().GetParamValueInt("server.v3", "updatetime.connected", 60);
  _updatetime_idle = OvmsConfig::instance().GetParamValueInt("server.v3", "updatetime.idle", 600);
  _updatetime_awake = OvmsConfig::instance().GetParamValueInt("server.v3", "updatetime.awake", _updatetime_idle);
  _updatetime_on = OvmsConfig::instance().GetParamValueInt("server.v3", "updatetime.on", _updatetime_idle);
  _updatetime_charging = OvmsConfig::instance().GetParamValueInt("server.v3", "updatetime.charging", _updatetime_idle);
  _updatetime_sendall = OvmsConfig::instance().GetParamValueInt("server.v3", "updatetime.sendall", 0);
  _metrics_filter.LoadFilters(OvmsConfig::instance().GetParamValue("server.v3", "metrics.include"),
                              OvmsConfig::instance().GetParamValue("server.v3", "metrics.exclude"));
}

void OvmsV3::Ticker1(std::string event, void* data) {
  int now = MetricsStandard::instance().ms_m_monotonic->AsInt();
  if (_sendall) {
    ESP_LOGI(TAG, "Transmit all metrics");
    TransmitAllMetrics();
    _lasttx_sendall = now;
    _sendall = false;
    }
    // if (_notify_info_pending) TransmitPendingNotificationsInfo();
    // if (_notify_error_pending) TransmitPendingNotificationsError();
    // if (_notify_alert_pending) TransmitPendingNotificationsAlert();
    // if ((_notify_data_pending)&&(_notify_data_waitcomp==0))
    //   TransmitPendingNotificationsData();
    bool caron = MetricsStandard::instance().ms_v_env_on->AsBool();
    // Next send time depends on the state of the car
    int next = (_peers != 0) ? _updatetime_connected :
                (caron) ? _updatetime_on :
                (MetricsStandard::instance().ms_v_charge_inprogress->AsBool()) ? _updatetime_charging :
                (MetricsStandard::instance().ms_v_env_awake->AsBool()) ? _updatetime_awake :
                _updatetime_idle;

    if ((_lasttx_sendall == 0) ||
        (_updatetime_sendall > 0 && now > (_lasttx_sendall + _updatetime_sendall))) {
      ESP_LOGI(TAG, "Transmit all metrics");
      TransmitAllMetrics();
      _lasttx_sendall = now;
      } else if ((_lasttx==0)||(now>(_lasttx+next))) {
      TransmitModifiedMetrics();
      _lasttx = _lasttx_stream = now;
    } else if (_streaming && caron && _peers && now > _lasttx_stream + _streaming) {
      // TODO: transmit streaming metrics
      _lasttx_stream = now;
    }
  }

void OvmsV3::Ticker60(std::string event, void* data) {
  CountClients();
}

void OvmsV3::CountClients() {
  for (auto it = _clients.begin(); it != _clients.end();) {
    if (it->second < monotonictime)
      {
      // Expired, so delete it...
      ESP_LOGI(TAG, "MQTT client %s has timed out",it->first.c_str());
      it = _clients.erase(it);
      }
    else
      {
      it++;
      }
    }
  int nc = _clients.size();
  MetricsStandard::instance().ms_s_v3_peers->SetValue(nc);
  if (nc > _peers)
    ESP_LOGI(TAG,  "One or more peers have connected");
  if ((nc == 0)&&(_peers != 0))
    OvmsEvents::instance().SignalEvent("app.disconnected",NULL);
  else if ((nc > 0)&&(nc != _peers))
    OvmsEvents::instance().SignalEvent("app.connected",NULL);
  if (nc != _peers)
    _lasttx = 0; // Our peers have changed, so force a transmission of status messages
  _peers = nc;
}

// void OvmsV3::setState(State state) {
//   if (state == _state)
//     return;
//   ESP_LOGI(TAG, "state changed: %s -> %s", toString(_state), toString(state));
//   _state = state;
//   StaticJsonDocument<0> doc;
//   doc.set(serialized(toString(state)));
//   EventStateChanged::publish(this, doc);
// }

// DynamicJsonDocument *OvmsV3::getState(const JsonVariantConst args) {
//   DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
//   JsonObject info = doc->to<JsonObject>();
//   info["state"] = toString(_state);
//   return doc;
// }

// bool OvmsV3::handleRequest(Request &req) {
//   if (AppObject::handleRequest(req))
//     return true;
//   return false;
// }

void OvmsV3::handleEvent(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data) {
    switch (event_id) {
      case RMAKER_MQTT_EVENT_CONNECTED:
        OvmsV3::instance()._sendall = true;
        MetricsStandard::instance().ms_s_v3_connected->SetValue(true);
        //     setState(State::Connected);
        //     auto mqttCfg = useMqtt()->getCfg();
        //     _topic_prefix = std::string("retrovms/");
        //     _topic_prefix.append(mqttCfg.credentials.username);
        //     _topic_prefix.append("/");
        //     _topic_prefix.append(mqttCfg.credentials.client_id);
        //     _topic_prefix.append("/");
        break;
      case RMAKER_MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected.");
        //     MetricsStandard::instance().ms_s_v3_connected->SetValue(false);
        //     setState(State::Disconnected);
        break;
  }
}

const char *OvmsV3::toString(State s) {
  static const char *names[] = {"Offline", "Connected"};
  int si = (int)s;
  if (si < 0 || si > 1)
    si = 0;
  return names[si];
}

OvmsV3 &OvmsV3::instance() {
  static OvmsV3 i;
  return i;
}

OvmsV3 *useOvmsV3() {
  return &OvmsV3::instance();
}

