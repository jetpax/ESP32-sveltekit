/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2023 - 2024 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <DownloadFirmwareService.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_crt_bundle.h"
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_src_certs_x509_crt_bundle_bin_start");

static EventSocket *_socket = nullptr;
static int previousProgress = 0;
JsonDocument doc;

void update_started()
{
    String output;
    doc["status"] = "preparing";
    JsonObject jsonObject = doc.as<JsonObject>();
    _socket->emitEvent(EVENT_DOWNLOAD_OTA, jsonObject);
}

void update_progress(int currentBytes, int totalBytes)
{
    doc["status"] = "progress";
    int progress = ((currentBytes * 100) / totalBytes);
    if (progress > previousProgress)
    {
        doc["progress"] = progress;
        JsonObject jsonObject = doc.as<JsonObject>();
        _socket->emitEvent(EVENT_DOWNLOAD_OTA, jsonObject);
        ESP_LOGV("Download OTA", "HTTP update process at %d of %d bytes... (%d %%)", currentBytes, totalBytes, progress);
    }
    previousProgress = progress;
}

void update_finished()
{
    doc["status"] = "finished";
    JsonObject jsonObject = doc.as<JsonObject>();
    _socket->emitEvent(EVENT_DOWNLOAD_OTA, jsonObject);

    // delay to allow the event to be sent out
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

void updateTask(void *param)
{
    WiFiClientSecure client;
    client.setTimeout(10);

    // Attach the certificate bundle for secure HTTPS connections
    esp_crt_bundle_attach(NULL); // Directly attach the certificate bundle without needing getESPClient()

    // Configure HTTP Update settings
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);

    String url = *((String *)param);
    String output;
    JsonObject jsonObject;

    // Perform the OTA update
    t_httpUpdate_return ret = httpUpdate.update(client, url.c_str());

    // Handle the result of the update
    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
        doc["status"] = "error";
        doc["error"] = httpUpdate.getLastErrorString().c_str();
        jsonObject = doc.as<JsonObject>();
        if (_socket)
        {
            _socket->emitEvent(EVENT_DOWNLOAD_OTA, jsonObject);
        }
        ESP_LOGE("Download OTA", "HTTP Update failed with error (%d): %s", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        break;

    case HTTP_UPDATE_NO_UPDATES:
        doc["status"] = "error";
        doc["error"] = "Update failed, has same firmware version";
        jsonObject = doc.as<JsonObject>();
        if (_socket)
        {
            _socket->emitEvent(EVENT_DOWNLOAD_OTA, jsonObject);
        }
        ESP_LOGE("Download OTA", "HTTP Update failed, has same firmware version");
        break;

    case HTTP_UPDATE_OK:
        doc["status"] = "success";
        doc["message"] = "Update successful - Restarting";
        jsonObject = doc.as<JsonObject>();
        if (_socket)
        {
            _socket->emitEvent(EVENT_DOWNLOAD_OTA, jsonObject);
        }
        ESP_LOGI("Download OTA", "HTTP Update successful - Restarting");
        break;
    }

    // Clean up the task to avoid memory leaks
    vTaskDelete(NULL);
}

DownloadFirmwareService::DownloadFirmwareService(PsychicHttpServer *server,
                                                 SecurityManager *securityManager,
                                                 EventSocket *socket) : _server(server),
                                                                        _securityManager(securityManager),
                                                                        _socket(socket)
{
}

void DownloadFirmwareService::begin()
{
    _socket->registerEvent(EVENT_DOWNLOAD_OTA);

    _server->on(GITHUB_FIRMWARE_PATH,
                HTTP_POST,
                _securityManager->wrapCallback(
                    std::bind(&DownloadFirmwareService::downloadUpdate, this, std::placeholders::_1, std::placeholders::_2),
                    AuthenticationPredicates::IS_ADMIN));

    ESP_LOGV("DownloadFirmwareService", "Registered POST endpoint: %s", GITHUB_FIRMWARE_PATH);
}

esp_err_t DownloadFirmwareService::downloadUpdate(PsychicRequest *request, JsonVariant &json)
{
    if (!json.is<JsonObject>())
    {
        return request->reply(400);
    }

    String downloadURL = json["download_url"];
    ESP_LOGI("Download OTA", "Starting OTA from: %s", downloadURL.c_str());
#ifdef SERIAL_INFO
    Serial.println("Starting OTA from: " + downloadURL);
#endif

    doc["status"] = "preparing";
    doc["progress"] = 0;
    doc["error"] = "";

    JsonObject jsonObject = doc.as<JsonObject>();
    _socket->emitEvent(EVENT_DOWNLOAD_OTA, jsonObject);

    if (xTaskCreatePinnedToCore(
            &updateTask,                // Function that should be called
            "Update",                   // Name of the task (for debugging)
            OTA_TASK_STACK_SIZE,        // Stack size (bytes)
            &downloadURL,               // Pass reference to this class instance
            (configMAX_PRIORITIES - 1), // Pretty high task priority
            NULL,                       // Task handle
            1                           // Have it on application core
            ) != pdPASS)
    {
        ESP_LOGE("Download OTA", "Couldn't create download OTA task");
        return request->reply(500);
    }
    return request->reply(200);
}
