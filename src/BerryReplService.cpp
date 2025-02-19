#include "BerryReplService.h"
#include <ArduinoJson.h>
#include <ESP_LOG.h>

// Declare the Berry error handler (from your main.cpp)
extern "C" void be_error_pop_all(bvm *vm);

BerryReplService::BerryReplService(ESP32SvelteKit *sveltekit)
    : _eventEndpoint(
          // State updater: Extract the command from incoming JSON.
          [](String &state, JsonObject &root) -> StateUpdateResult {
              if (root["command"].is<const char*>()) {
                  state = root["command"].as<const char*>();
                  return StateUpdateResult::CHANGED;
              }
              return StateUpdateResult::UNCHANGED;
          },
          // State reader: Write the current state (the command result) into the outgoing JSON.
          [](JsonObject &root, String &state) -> StateUpdateResult {
              root["result"] = state;
              return StateUpdateResult::CHANGED;
          },
          this, sveltekit->getSocket(), "repl")
// BerryReplService::BerryReplService(ESP32SvelteKit *sveltekit)
//     : _eventEndpoint(
//           [](String &state, JsonObject &root) -> StateUpdateResult {
//               if (root.containsKey("command")) {
//                   state = root["command"].as<const char*>();
//                   Serial.printf("[BerryReplService] WebSocket received: %s\n", state.c_str());
//                   return StateUpdateResult::CHANGED;
//               }
//               Serial.println("[BerryReplService] Invalid WebSocket payload");
//               return StateUpdateResult::UNCHANGED;
//           },
//           [](JsonObject &root, String &state) -> StateUpdateResult {
//               root["result"] = state;
//               return StateUpdateResult::CHANGED;
//           },
//           this, sveltekit->getSocket(), "repl")
// BerryReplService::BerryReplService(ESP32SvelteKit *sveltekit)
//     : _eventEndpoint(
//           [](String &state, JsonObject &root) -> StateUpdateResult {
//               Serial.println("[BerryReplService] Raw WebSocket payload received:");

//               serializeJson(root, Serial);  // Print the actual JSON received
//               Serial.println();

//               if (root["command"].is<const char*>()) {
//                   state = root["command"].as<const char*>();
//                   Serial.printf("[BerryReplService] WebSocket received: %s\n", state.c_str());
//                   return StateUpdateResult::CHANGED;
//               }

//               Serial.println("[BerryReplService] Invalid WebSocket payload");
//               return StateUpdateResult::UNCHANGED;
//           },
//           [](JsonObject &root, String &state) -> StateUpdateResult {
//               root["result"] = state;
//               return StateUpdateResult::CHANGED;
//           },
//           this, sveltekit->getSocket(), "repl")

{
    _vm = be_vm_new();  // Create a new Berry VM instance for testing
    ESP_LOGI("BerryReplService", "Berry VM created");
}


void BerryReplService::begin() {
    _eventEndpoint.begin();
    ESP_LOGI("BerryReplService", "Berry REPL service started");
}

String BerryReplService::executeCommand(const String &command) {
    int ret = be_loadstring(_vm, command.c_str());
    if (ret != 0) {
        ESP_LOGE("BerryReplService", "Failed to load command");
        be_error_pop_all(_vm);
        return "Error: Failed to load command";
    }
    ret = be_pcall(_vm, 0);
    if (ret != 0) {
        ESP_LOGE("BerryReplService", "Failed to execute command");
        be_error_pop_all(_vm);
        return "Error: Command execution failed";
    }
    ESP_LOGI("BerryReplService", "Command executed successfully");
    return "Command executed successfully";
}

// void BerryReplService::handleCommand(const String &command) {
//     String result = executeCommand(command);
//     // Update the state so the EventEndpoint sends the new result.
//     update([&](String &state) {
//         state = result;
//         return StateUpdateResult::CHANGED;
//     }, "repl");
// }


// void BerryReplService::handleCommand(const String &command) {
//   Serial.printf("[BerryReplService] Received command: %s\n", command.c_str());
//   String result = executeCommand(command);
//   Serial.printf("[BerryReplService] Command result: %s\n", result.c_str());

//   // Use update() to modify the state and notify clients
//   update([&](String &state) {
//       state = result;
//       return StateUpdateResult::CHANGED;
//   }, "repl");
// }

void BerryReplService::handleCommand(const String &command) {
  Serial.printf("[BerryReplService] handleCommand() called with: %s\n", command.c_str());
  String result = executeCommand(command);
  Serial.printf("[BerryReplService] Command execution result: %s\n", result.c_str());

  // Update WebSocket state
  update([&](String &state) {
      state = result;
      Serial.printf("[BerryReplService] WebSocket state updated: %s\n", state.c_str());
      return StateUpdateResult::CHANGED;
  }, "repl");
}