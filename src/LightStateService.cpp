/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 - 2024 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <LightStateService.h>
static const char *TAG = "LightStateService";

LightStateService::LightStateService(PsychicHttpServer *server,
                                     ESP32SvelteKit *sveltekit,
                                     LightMqttSettingsService *lightMqttSettingsService) : _httpEndpoint(LightState::read,
                                                                                                         LightState::update,
                                                                                                         this,
                                                                                                         server,
                                                                                                         LIGHT_SETTINGS_ENDPOINT_PATH,
                                                                                                         sveltekit->getSecurityManager(),
                                                                                                         AuthenticationPredicates::IS_AUTHENTICATED),
                                                                                           _eventEndpoint(LightState::read,
                                                                                                          LightState::update,
                                                                                                          this,
                                                                                                          sveltekit->getSocket(),
                                                                                                          LIGHT_SETTINGS_EVENT),
                                                                                           _mqttEndpoint(LightState::homeAssistRead,
                                                                                                         LightState::homeAssistUpdate,
                                                                                                         this,
                                                                                                         sveltekit->getMqttClient()),
                                                                                           _webSocketServer(LightState::read,
                                                                                                            LightState::update,
                                                                                                            this,
                                                                                                            server,
                                                                                                            LIGHT_SETTINGS_SOCKET_PATH,
                                                                                                            sveltekit->getSecurityManager(),
                                                                                                            sveltekit->getSocket()->getWebSocketHandler(),
                                                                                                            AuthenticationPredicates::IS_AUTHENTICATED),
                                                                                           _mqttClient(sveltekit->getMqttClient()),
                                                                                           _lightMqttSettingsService(lightMqttSettingsService),
                                                                                           _featuresService(sveltekit->getFeatureService())

{

    // configure MQTT callback
    _mqttClient->onConnect(std::bind(&LightStateService::registerConfig, this));

    // configure update handler for when the light settings change
    _lightMqttSettingsService->addUpdateHandler([&](const String &originId)
                                                { registerConfig(); },
                                                false);

    // configure settings service update handler to update LED state
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void LightStateService::begin()
{
    _httpEndpoint.begin();
    _eventEndpoint.begin();
    _webSocketServer.begin();  // Ensure WebSocketServer is initialized
    _state.ledOn = DEFAULT_LED_STATE;

    #ifdef RGB_BUILTIN
      ESP_LOGI(TAG, "Add RGBLED on GPIO%d", PIN_RGB_LED);
      // configure led to be output
      pinMode(PIN_RGB_LED, OUTPUT);
      _featuresService->addFeature("rgb", true);
#else
    ESP_LOGI(TAG, "NO RGBLED");
    _featuresService->addFeature("rgb", false);
#endif

    onConfigUpdated();
}

void LightStateService::onConfigUpdated()
{
#ifdef RGB_BUILTIN
    Serial.printf("[onConfigUpdated] Updating RGBLED state to: %s\n", _state.ledOn ? "BLUE" : "BLACK");
    if (_state.ledOn) rgbLedWrite(RGB_BUILTIN,_state.red*255.,_state.green*255.,_state.blue*255.);
    else rgbLedWrite(RGB_BUILTIN,0,0,0);
#else
    digitalWrite(LED_BUILTIN, _state.ledOn ? 1 : 0);
#endif
}

void LightStateService::registerConfig()
{
    if (!_mqttClient->connected())
    {
        return;
    }
    String configTopic;
    String subTopic;
    String pubTopic;

    JsonDocument doc;
    _lightMqttSettingsService->read([&](LightMqttSettings &settings)
                                    {
    configTopic = settings.mqttPath + "/config";
    subTopic = settings.mqttPath + "/set";
    pubTopic = settings.mqttPath + "/state";
    doc["~"] = settings.mqttPath;
    doc["name"] = settings.name;
    doc["unique_id"] = settings.uniqueId; });
    doc["cmd_t"] = "~/set";
    doc["stat_t"] = "~/state";
    doc["schema"] = "json";
    doc["brightness"] = false;

    String payload;
    serializeJson(doc, payload);
    _mqttClient->publish(configTopic.c_str(), 0, false, payload.c_str());

    _mqttEndpoint.configureTopics(pubTopic, subTopic);
}
