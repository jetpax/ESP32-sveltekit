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
  doc["result"] = _lastResult;
  
  if (!_logBuffer.isEmpty()) {
      doc["stdout"] = _logBuffer;
      _logBuffer.clear();  // Reset buffer after sending
  }

  if (_socket) {
      JsonObject obj = doc.as<JsonObject>();  // Create an lvalue JsonObject
      _socket->emitEvent("repl", obj);
      ESP_LOGI(TAG, "Sent WebSocket event: repl -> %s", _lastResult.c_str());
  } else {
      ESP_LOGE(TAG, "Socket is NULL, cannot send event!");
  }
}

void BerryReplService::processCommand(const String &command) {
    ESP_LOGI(TAG, "Processing command: %s", command.c_str());

    _logBuffer = "";  // Reset logs before execution
    _lastResult = executeCommand(command);

    onReplUpdated();  // Send results & logs via WebSocket
}

String BerryReplService::executeCommand(const String &command) {
    ESP_LOGI(TAG, "Executing command: %s", command.c_str());

    String modifiedCommand = wrapCommand(command);
    ESP_LOGI(TAG, "Modified command: %s", modifiedCommand.c_str());

    int ret = be_loadstring(_vm, modifiedCommand.c_str());
    if (ret != 0) {
        return handleExecutionError("Failed to load command");
    }

    ret = be_pcall(_vm, 0);
    if (ret != 0) {
        return handleExecutionError("Failed to execute command");
    }

    return extractExecutionResult();
}

String BerryReplService::wrapCommand(const String &command) {
  if (command.startsWith("print(")) {
      return command;  // Execute `print()` directly without wrapping in `return`
  }

  if (!(command.startsWith("return") || command.startsWith("def") ||
        command.startsWith("import") || command.startsWith("for") ||
        command.startsWith("while") || command.startsWith("if") ||
        command.startsWith("class"))) 
  {
      return "return (" + command + ")";
  }
  return command;
}

String BerryReplService::handleExecutionError(const char* errorMessage) {
    ESP_LOGE(TAG, "%s", errorMessage);
    be_error_pop_all(_vm);
    return String("Error: ") + errorMessage;
}

String BerryReplService::extractExecutionResult() {
  if (!_logBuffer.isEmpty()) {
      String result = _logBuffer;  // Prioritize printed output
      _logBuffer.clear();  // Clear buffer after sending
      ESP_LOGI(TAG, "Returning captured print output: %s", result.c_str());
      return result;
  }

  const char* resultStr = be_tostring(_vm, -1);
  String result = resultStr ? String(resultStr) : "nil";  // Default to "nil"
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
  if (!root.containsKey("command")) {
      return StateUpdateResult::UNCHANGED;
  }

  String command = root["command"].as<String>();

  if (s_instance) {  
      s_instance->processCommand(command);  // ✅ Call via singleton instance
  }

  return StateUpdateResult::CHANGED;
}