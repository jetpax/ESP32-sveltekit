#include "BerryReplService.h"
#include <ArduinoJson.h>
#include <ESP_LOG.h>

extern "C" void be_error_pop_all(bvm *vm);

static const char *TAG = "BerryReplService";

// Initialize static instance pointer.
BerryReplService* BerryReplService::s_instance = nullptr;

BerryReplService::BerryReplService(ESP32SvelteKit *sveltekit)
    : _eventEndpoint(&BerryReplService::read, &BerryReplService::update, this, sveltekit->getSocket(), "repl"),
    _socket(sveltekit->getSocket()) 
{
    _vm = be_vm_new();
    s_instance = this;
    _lastResult = ""; // initially empty
    ESP_LOGI(TAG, "Berry VM created");
}

void BerryReplService::begin() {
    _eventEndpoint.begin();
    addUpdateHandler([&](const String &originId) { onReplUpdated(); }, false);
    ESP_LOGI(TAG, "Berry REPL service started");
}

void BerryReplService::onReplUpdated() {
  ESP_LOGI(TAG, "onReplUpdated() called");

  // Log the last known result before sending
  ESP_LOGI(TAG, "onReplUpdated(): Last known result = %s", _lastResult.c_str());

  // Construct the JSON payload
  JsonDocument doc;
  doc["result"] = _lastResult;
  JsonObject jsonObject = doc.as<JsonObject>();

  // Emit event directly using _socket
  if (_socket) {
      _socket->emitEvent("repl", jsonObject);
      ESP_LOGI(TAG, "Sent WebSocket event: repl -> %s", _lastResult.c_str());
  } else {
      ESP_LOGE(TAG, "Socket is NULL, cannot send event!");
  }
}

String BerryReplService::executeCommand(const String &command) {
    ESP_LOGI(TAG, "Executing command: %s", command.c_str());
  
    int ret = be_loadstring(_vm, command.c_str());
    if(ret != 0) {
        ESP_LOGE(TAG, "Failed to load command");
        be_error_pop_all(_vm);
        return "Error: Failed to load command";
    }
  
    ret = be_pcall(_vm, 0);
    if(ret != 0) {
        ESP_LOGE(TAG, "Failed to execute command");
        be_error_pop_all(_vm);
        return "Error: Command execution failed";
    }
  
    // Capture the result from the top of the VM's stack.
    const char* resultStr = be_tostring(_vm, -1);
    String result;
    if(resultStr) {
        result = String(resultStr);
    } else {
        result = "nil";  // No result was returned
    }
  
    // Clean up the result from the stack.
    be_pop(_vm, 1);
  
    ESP_LOGI(TAG, "Command executed successfully, result: %s", result.c_str());
    return result;
}

void BerryReplService::read(String &state, JsonObject &root) {
    ESP_LOGI(TAG, "Raw WebSocket payload received");

    // If the payload is empty, preserve the last valid result.
    if(root.isNull() || root.size() == 0) {
        ESP_LOGD(TAG, "Empty JSON payload received, ignoring.");
        // Set state to the last valid result.
        state = s_instance->_lastResult;
        return;
    }

    std::string jsonString;
    serializeJson(root, jsonString);
    ESP_LOGI(TAG, "Parsed JSON: %s", jsonString.c_str());

    if(root["command"].is<const char*>()) {
        state = root["command"].as<const char*>();
        ESP_LOGI(TAG, "WebSocket received command: %s", state.c_str());
    } else {
        ESP_LOGE(TAG, "Invalid WebSocket payload (missing or malformed 'command')");
    }
}

StateUpdateResult BerryReplService::update(JsonObject &root, String &state) {
    ESP_LOGI(TAG, "Processing WebSocket update...");

    if(!root["command"].is<const char*>()) {
        ESP_LOGD(TAG, "Empty or invalid command received via WebSocket; ignoring update.");
        return StateUpdateResult::UNCHANGED;
    }

    String command = root["command"].as<const char*>();
    ESP_LOGI(TAG, "Executing REPL command: %s", command.c_str());

    state = s_instance->executeCommand(command);
    ESP_LOGI(TAG, "Command execution result: %s", state.c_str());

    // Save the valid result so that if an empty payload comes in later, we don't lose it.
    s_instance->_lastResult = state;

    return StateUpdateResult::CHANGED;
}