#ifndef BERRY_REPL_SERVICE_H
#define BERRY_REPL_SERVICE_H

#include <ESP32SvelteKit.h>
#include <StatefulService.h>
#include <EventEndpoint.h>
#include <ArduinoJson.h>
#include "berry.h"
#include "be_vm.h"

class BerryReplService : public StatefulService<String> {
public:
    explicit BerryReplService(ESP32SvelteKit *sveltekit);
    void begin();
    void handleCommand(const String &command);

private:
    EventEndpoint<String> _eventEndpoint;
    bvm *_vm;  // Berry Virtual Machine instance
    String executeCommand(const String &command);
};

#endif // BERRY_REPL_SERVICE_H