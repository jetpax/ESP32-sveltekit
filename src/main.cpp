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

#include <ESP32SvelteKit.h>
#include <LightMqttSettingsService.h>
#include <LightStateService.h>
#include <PsychicHttpServer.h>

extern "C"
{
#include "berry.h"
}

#define SERIAL_BAUD_RATE 115200

PsychicHttpServer server;

ESP32SvelteKit esp32sveltekit(&server, 120);

LightMqttSettingsService lightMqttSettingsService = LightMqttSettingsService(&server,
                                                                             esp32sveltekit.getFS(),
                                                                             esp32sveltekit.getSecurityManager());

LightStateService lightStateService = LightStateService(&server,
                                                        esp32sveltekit.getSocket(),
                                                        esp32sveltekit.getSecurityManager(),
                                                        esp32sveltekit.getMqttClient(),
                                                        &lightMqttSettingsService);

void setup()
{
    // start serial and filesystem
    Serial.begin(SERIAL_BAUD_RATE);

    // start ESP32-SvelteKit
    esp32sveltekit.begin();

    // load the initial light settings
    lightStateService.begin();
    // start the light service
    lightMqttSettingsService.begin();
}

void loop()
{
    bvm *vm = be_vm_new(); // Construct a VM
    // printf("Berry VM loaded, RAM used=%u", be_gc_memcount(*vm));
    printf("-- Minimal test --\n");
    be_loadstring(vm, "print('Hello Berry')"); // Compile test code
    be_pcall(vm, 0);                           // Call function
    be_loadstring(vm, "a=10");
    be_pcall(vm, 0);
    be_loadstring(vm, "print(a)");
    be_pcall(vm, 0);
    printf("------------------\n\n");
    be_vm_delete(vm);

    // Delete Arduino loop task, as it is not needed in this example
    vTaskDelete(NULL);
}
