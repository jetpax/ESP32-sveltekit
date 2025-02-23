#include "BerryReplService.h"
#include <ArduinoJson.h>
#include <ESP_LOG.h>

extern "C" void be_error_pop_all(bvm *vm);

static const char *TAG = "BerryReplService";

BerryReplService* BerryReplService::s_instance = nullptr;

BerryReplService::BerryReplService(ESP32SvelteKit *sveltekit)
    : _eventEndpoint(&BerryReplService::read, &BerryReplService::update, this, sveltekit->getSocket(), "repl"),
      _socket(sveltekit->getSocket()) 
{
    _vm = be_vm_new();
    s_instance = this;
    _lastResult = "";
    _logBuffer = "";

    ESP_LOGI(TAG, "Berry VM initialized");
}

void BerryReplService::begin() {
    _eventEndpoint.begin();
    addUpdateHandler([&](const String &originId) { onReplUpdated(); }, false);
    ESP_LOGI(TAG, "Berry REPL service started");
    registerPrintFunction();
}

void BerryReplService::registerPrintFunction() {
  be_pushntvfunction(_vm, [](bvm *vm) -> int {
      if (be_isstring(vm, 1)) {
          const char *output = be_tostring(vm, 1);
          ESP_LOGI(TAG, "Berry print captured: %s", output);
          s_instance->_logBuffer += String(output) + "\n";  // Append to log buffer
      }
      return 0;
  });
  be_setglobal(_vm, "print");
}

void BerryReplService::onReplUpdated() {
    ESP_LOGI(TAG, "onReplUpdated() called");

    JsonDocument doc;
    
    if (!_logBuffer.isEmpty()) {
        doc["stdout"] = _logBuffer;  // Separate print() logs
        _logBuffer.clear();
    }

    if (_lastResult.isEmpty()) {  // Ensure at least an empty string result
        doc["result"] = "";
    } else {
        doc["result"] = _lastResult;
    }

    if (_socket) {
        JsonObject obj = doc.as<JsonObject>();
        _socket->emitEvent("repl", obj);
        ESP_LOGI(TAG, "Sent WebSocket event: repl -> %s", _lastResult.c_str());
    } else {
        ESP_LOGE(TAG, "Socket is NULL, cannot send event!");
    }
}

void BerryReplService::processCommand(const String &command) {
  _logBuffer.clear();  // Reset logs before execution
  _lastResult.clear(); // Reset last result before execution
  _lastResult = executeCommand(command);
}


String BerryReplService::wrapCommand(const String &command) {
  // First attempt to wrap in `return (...)`
  String wrappedCmd = "return (" + command + ")";
  
  // Try to execute it
  int ret_code = be_loadstring(_vm, wrappedCmd.c_str());
  if (be_getexcept(_vm, ret_code) == BE_SYNTAX_ERROR) {
      be_pop(_vm, 2); // Remove error from stack
      return command; // Fallback to executing as-is
  }
  
  return wrappedCmd;
}


String BerryReplService::executeCommand(const String &command) {
  ESP_LOGI(TAG, "Executing command: %s", command.c_str());

  String modifiedCommand = wrapCommand(command);

  int ret;
  do {
      // First try wrapping in `return (...)`
      ret = be_loadbuffer(_vm, "input", modifiedCommand.c_str(), modifiedCommand.length());
      if (be_getexcept(_vm, ret) == BE_SYNTAX_ERROR) {
          be_pop(_vm, 2);  // Remove syntax error
          // Retry without wrapping
          ret = be_loadbuffer(_vm, "input", command.c_str(), command.length());
      }
      if (ret != 0) break;

      ESP_LOGI(TAG, "Berry script loaded successfully");

      // BrTimeoutStart();
      ret = be_pcall(_vm, 0);  // Execute the command
      // BrTimeoutReset();
  } while (0);

  if (ret != 0) {
      return handleExecutionError("Failed to execute command");
  }

  return extractExecutionResult();
}


String BerryReplService::handleExecutionError(const char* errorMessage) {
    ESP_LOGE(TAG, "%s", errorMessage);
    be_error_pop_all(_vm);
    return String("Error: ") + errorMessage;
}


String BerryReplService::extractExecutionResult() {
  if (!_logBuffer.isEmpty()) {
      _logBuffer.trim();  
      String result = _logBuffer; 
      _logBuffer.clear();  // Clear _logBuffer immediately to prevent duplicate sends
      return result;
  }

  const char* resultStr = be_tostring(_vm, -1);
  String result = resultStr ? String(resultStr) : "nil";
  be_pop(_vm, 1);

  ESP_LOGI(TAG, "Execution result: %s", result.c_str());
  return result;
}

void BerryReplService::read(String &state, JsonObject &root) {
    if (root.isNull() || root.size() == 0) {
        ESP_LOGD(TAG, "Empty JSON payload received, returning last result.");
        state = s_instance->_lastResult;
        return;
    }

    ESP_LOGI(TAG, "Received JSON command: %s", root["command"].as<const char*>());
    state = root["command"].as<const char*>();
}

StateUpdateResult BerryReplService::update(JsonObject &root, String &state) {
  if (!root["command"].is<const char*>()) {
    return StateUpdateResult::UNCHANGED;
  }

  String command = root["command"].as<String>();

  if (s_instance) {  
      s_instance->processCommand(command); 
  }

  return StateUpdateResult::CHANGED;
}
