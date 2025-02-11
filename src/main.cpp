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
#include <ESPFS.h>
#include <FS.h>
#include "esp_log.h"

extern "C"
{
#include "berry.h"
#include "be_vm.h"
#include "be_mapping.h"
#include "be_gc.h"
#include "berry_conf.h"
}

#define SERIAL_BAUD_RATE 115200

PsychicHttpServer server;

ESP32SvelteKit esp32sveltekit(&server, 120);

LightMqttSettingsService lightMqttSettingsService = LightMqttSettingsService(&server,
                                                                             &esp32sveltekit);

LightStateService lightStateService = LightStateService(&server,
                                                        &esp32sveltekit,
                                                        &lightMqttSettingsService);

void be_error_pop_all(bvm *vm)
{
    if (vm->obshook != NULL)
        (*vm->obshook)(vm, BE_OBS_PCALL_ERROR);
    be_pop(vm, be_top(vm)); // clear Berry stack
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- file written");
    }
    else
    {
        Serial.println("- write failed");
    }
    file.close();
}

void readFile(fs::FS &fs, const char *path)
{
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");
    while (file.available())
    {
        Serial.write(file.read());
    }
    file.close();
}

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

    writeFile(ESPFS, "/test.be", "print('Hello, World!')\r\na=20\r\nprint(a) \r\n");
}

void loop()
{
    readFile(ESPFS, "/test.be");

    printf("-- Minimal test from FS --\n");

    int32_t ret_code1 = 0, ret_code2 = 0;
    bvm *vm = be_vm_new();  // create a virtual machine instance
    comp_set_named_gbl(vm); // Enable named globals in Berry compiler
    comp_set_strict(vm);    // Enable strict mode in Berry compiler, equivalent of `import strict`
    be_set_ctype_func_hanlder(vm, be_call_ctype_func);
    // Set the GC threshold to 3584 bytes to avoid the first useless GC
    vm->gc.threshold = 3584;

    ret_code1 = be_loadstring(vm, "print('Hello, Berry!')\r\na=20\r\nprint(a)\r\n");
    if (ret_code1 != 0)
    {
        be_error_pop_all(vm); // clear Berry stack
    }
    ESP_LOGI("Main", "Berry code loaded, RAM used=%u", be_gc_memcount(vm));

    ret_code2 = be_pcall(vm, 0);
    if (ret_code2 != 0)
    {
        be_error_pop_all(vm); // clear Berry stack
    }
    ESP_LOGI("Main", "Berry code ran, RAM used=%u", be_gc_memcount(vm));

    printf("------------------\n\n");
    be_vm_delete(vm);

    // Delete Arduino loop task, as it is not needed in this example
    vTaskDelete(NULL);
}
