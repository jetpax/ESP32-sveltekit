#ifndef BERRY_REPL_SERVICE_H
#define BERRY_REPL_SERVICE_H

#include <ESP32SvelteKit.h>
#include <EventEndpoint.h>
#include <StatefulService.h>
#include <be_vm.h>

class BerryReplService : public StatefulService<String> {
public:
    BerryReplService(ESP32SvelteKit *sveltekit);
    void begin();

    // WebSocket handlers (must be static for EventEndpoint)
    static void read(String &state, JsonObject &root);
    static StateUpdateResult update(JsonObject &root, String &state);

    // Use a static pointer so that static methods can access the instance.
    static BerryReplService* s_instance;

private:
    void onReplUpdated();
    String executeCommand(const String &command);
    String wrapCommand(const String &command);     // Auto-wrap single expressions
    String extractExecutionResult();               // Handles explicit return values
    String handleExecutionError(const char* err);  // Error handling

    void processCommand(const String &command);  // <-- Add this line
    void registerPrintFunction();  // Capture print() calls

    bvm *_vm;
    EventEndpoint<String> _eventEndpoint;
    EventSocket* _socket;

    // Store the last valid result + print output logs
    String _lastResult;
    String _logBuffer;  
};

#endif  // BERRY_REPL_SERVICE_H