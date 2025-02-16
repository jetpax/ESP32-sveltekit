#include "shell.hpp"
#include "Shellminator-IO.hpp"
#include "Shellminator.hpp"

static const char *TAG = "shell";

#include <WiFi.h>
#include "esp_wifi.h"

#define SERVER_PORT 23

// Create telnet server
WiFiServer telnetServer(SERVER_PORT);

// // Create a Shellminator object, and initialize it to use WiFiServer
Shellminator shell(&telnetServer);

const char logo[] =
"\033[38;05;208;1m\r\n"
"            ____       __           _    ____  ________     \r\n"
"       ___ / __ \\___  / /__________| |  / /  |/  / ___/ ___\r\n"
"     ____ / /_/ / _ \\/ __/ ___/ __ \\ | / / /|_/ /\\__ \\/_____\r\n"
"   _____ / _, _/  __/ /_/ /  / /_/ / |/ / /  / /___/ /_______ \r\n"
"        /_/ |_|\\___/\\__/_/   \\____/|___/_/  /_//____/       \r\n"
"\r\n"
"\r\n\033[0;37m"
"Visit:\033[1;32m https://retrovms.com\r\n\r\n"
;

void Shell::run() {
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    shell.update();
    delay(10);
  }
}

void executionFunction( char* cmd ) {
  char* cmdCopy = strdup(cmd);
  // Tokenize the input command
  char* argv[MAX_TOKENS];  // Array of pointers to tokens
  int argc = 0;  // Number of tokens
  char* token = strtok(cmdCopy, " ");  // Tokenize by space
  while (token != NULL && argc < MAX_TOKENS) {
      argv[argc] = token;  // Store pointer to token directly
      argc++;  // Increment token count
      token = strtok(NULL, " ");  // Get next token
  }
  OvmsCommandApp::instance(TAG).Execute(Shell::instance().verbosity, &Shell::instance(), argc, argv);
  free(cmdCopy);
}

// completion handling is so convoluted in OVMS it bears some explanation...
// completionFunction() 
//    => OvmsCommandApp.Complete()
//      => _root.Complete()
//        => OvmsCommand::Complete()
//          => _children.GetCompletion()
//            => OvmsCommandMap::GetCompletion()
//              => writer->SetCompletion()
//                => Shell::SetCompletion()

  void completionFunction( char* cmd ) {
  ESP_LOGI(TAG, "partial: %s", cmd);
  char* cmdCopy = strdup(cmd);
  // Tokenize the input command
  char* argv[MAX_TOKENS];  // Array of pointers to tokens
  int argc = 0;  // Number of tokens
  char* token = strtok(cmdCopy, " ");  // Tokenize by space
  while (token != NULL && argc < MAX_TOKENS) {
      argv[argc] = token;  // Store pointer to token directly
      argc++;  // Increment token count
      token = strtok(NULL, " ");  // Get next token
  }
  char** candidates = OvmsCommandApp::instance(TAG).Complete(&Shell::instance(), argc, argv);
  int count = 0;
  while (candidates[count] != nullptr) {
    count++;
  }
  if (count==0) ESP_LOGI(TAG, "no candidates");
  else if (count==1) {    // only one candidate, so complete for user
    std::string lastToken = argv[argc-1];
    ESP_LOGI(TAG, "last token: %s", lastToken.c_str());
    std::string candidate = candidates[0];
    size_t pos = candidate.find(lastToken);
    std::string suffix = candidate.substr(pos + lastToken.length());
    ESP_LOGI(TAG, "suffix: %s", suffix.c_str());
    for (char c : suffix) {
      shell.process(c);
    }
    shell.process(' ');
  } else {                // multiple candidates, print on new line for user
    shell.print("\n\r");
    for (int i=0; i<count ; i++) {
      ESP_LOGI(TAG, "candidate: %s", candidates[i]);
      shell.print(candidates[i]);
      shell.print(" ");
    }
    // reprint prompt and original command
    shell.print("\n\r");
    shell.printBanner();
    shell.print(cmd);
  }
  free(cmdCopy);
}

// OVMSwriter child method eventually called by completionFunction()
char** Shell::SetCompletion(int index, const char* token)
  {
  if (index < COMPLETION_MAX_TOKENS+1)
    {
    if (index == COMPLETION_MAX_TOKENS)
      token = "...";
    if (token)
      {
      strncpy(_space[index], token, TOKEN_MAX_LENGTH-1);
      _space[index][TOKEN_MAX_LENGTH-1] = '\0';
      _completions[index] = _space[index];
      ++index;
      }
    _completions[index] = NULL;
    }
  return _completions;
  }

  Shell::Shell() {
  // Attach the logo.
  shell.attachLogo(logo);
  shell.addExecFunc(&executionFunction);
  shell.addCmpltFunc(&completionFunction);
  shell.beginServer();
  shell.begin("RetroVMS");
  // start shell.update() task
  xTaskCreate([](void* self) { ((Shell*)self)->run(); }, "m/shell", 4096,
              this, tskIDLE_PRIORITY + 1, &_task);
  // assume secure for now
  this->SetSecure(true);
  }

void Shell::SetSecure(bool secure) {
  OvmsWriter::SetSecure(secure);
  // _rl.prompt_str = secure ? secure_prompt : _PROMPT_DEFAULT;
}

/// Print immutable c-string
///
int Shell::puts(const char* s) {
  shell.print(s) ;
  shell.print("\n\r");
  return 0;
}

/// Print buffer
///
ssize_t Shell::write(const void *buf, size_t len) {
    shell.write(reinterpret_cast<const uint8_t*>(buf), len);
    return len;
}

/// Print formatted string
///
int Shell::printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    // Determine the length of the formatted string
    int len = vsnprintf(nullptr, 0, fmt, args);
    if (len < 0) {
        va_end(args);
        return -1;  // Error in formatting
    }
    
    // Allocate memory for the formatted string
    char *buffer = (char *)malloc(len + 1);  // +1 for the null terminator
    if (buffer == nullptr) {
        va_end(args);
        return -1;  // Memory allocation failed
    }

    // Format the string into the buffer
    vsnprintf(buffer, len + 1, fmt, args);
    va_end(args);

    // Write the formatted string to the output
    shell.write(reinterpret_cast<const uint8_t*>(buffer), len);
    // Free the allocated memory
    free(buffer);
    shell.print("\r");
    
    return len;
}

Shell &Shell::instance() {
  static Shell i;
  return i;
}

void useShell() {
  Shell::instance();
}    

