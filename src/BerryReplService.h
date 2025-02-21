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

    // Callbacks must be static to match the EventEndpoint signature.
    static void read(String &state, JsonObject &root);
    static StateUpdateResult update(JsonObject &root, String &state);

    // Use a static pointer so that static methods can access the instance.
    static BerryReplService* s_instance;

private:
    void onReplUpdated();
    String executeCommand(const String &command);

    bvm *_vm;
    EventEndpoint<String> _eventEndpoint;
    // Store the last valid result so that empty updates don't override it.
    String _lastResult;
    EventSocket* _socket; 
};

#endif  // BERRY_REPL_SERVICE_H