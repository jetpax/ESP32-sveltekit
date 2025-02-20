#ifndef WebSocketServer_h
#define WebSocketServer_h

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

#include <StatefulService.h>
#include <PsychicHttp.h>
#include <SecurityManager.h>

#define WEB_SOCKET_ORIGIN "wsserver"
#define WEB_SOCKET_ORIGIN_CLIENT_ID_PREFIX "wsserver:"

template <class T>
class WebSocketServer
{
public:
    WebSocketServer(JsonStateReader<T> stateReader,
                    JsonStateUpdater<T> stateUpdater,
                    StatefulService<T> *statefulService,
                    PsychicHttpServer *server,
                    const char *webSocketPath,
                    SecurityManager *securityManager,
                    PsychicWebSocketHandler &socket,  // Inject EventSocket’s WebSocket handler
                    AuthenticationPredicate authenticationPredicate = AuthenticationPredicates::IS_ADMIN) : _stateReader(stateReader),
                                                                                                            _stateUpdater(stateUpdater),
                                                                                                            _statefulService(statefulService),
                                                                                                            _server(server),
                                                                                                            _webSocket(socket), 
                                                                                                            _webSocketPath(webSocketPath),
                                                                                                            _authenticationPredicate(authenticationPredicate),
                                                                                                            _securityManager(securityManager)
    {
        _statefulService->addUpdateHandler(
            [&](const String &originId)
            { transmitData(nullptr, originId); },
            false);
    }

    void begin()
    {
        ESP_LOGI("WebSocketServer", "Using existing WebSocket handler from EventSocket.");
    }

    void onWSOpen(PsychicWebSocketClient *client)
    {
        // when a client connects, we transmit it's id and the current payload
        transmitId(client);
        transmitData(client, WEB_SOCKET_ORIGIN);
        ESP_LOGI("WebSocketServer", "ws[%s][%u] connect", client->remoteIP().toString().c_str(), client->socket());
    }

    void onWSClose(PsychicWebSocketClient *client)
    {
        ESP_LOGI("WebSocketServer", "ws[%s][%u] disconnect", client->remoteIP().toString().c_str(), client->socket());
    }

    esp_err_t onWSFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame)
    {
        ESP_LOGV("WebSocketServer", "ws[%s][%u] opcode[%d]", request->client()->remoteIP().toString().c_str(), request->client()->socket(), frame->type);

        if (frame->type == HTTPD_WS_TYPE_TEXT)
        {
            ESP_LOGV("WebSocketServer", "ws[%s][%u] request: %s", request->client()->remoteIP().toString().c_str(), request->client()->socket(), (char *)frame->payload);

            JsonDocument jsonDocument;
            DeserializationError error = deserializeJson(jsonDocument, (char *)frame->payload, frame->len);

            if (!error && jsonDocument.is<JsonObject>())
            {
                JsonObject jsonObject = jsonDocument.as<JsonObject>();
                _statefulService->update(jsonObject, _stateUpdater, clientId(request->client()));
                return ESP_OK;
            }
        }
        return ESP_OK;
    }

    String clientId(PsychicWebSocketClient *client)
    {
        return WEB_SOCKET_ORIGIN_CLIENT_ID_PREFIX + String(client->socket());
    }

private:
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;
    AuthenticationPredicate _authenticationPredicate;
    SecurityManager *_securityManager;
    PsychicHttpServer *_server;
    PsychicWebSocketHandler &_webSocket;
    String _webSocketPath;

    void transmitId(PsychicWebSocketClient *client)
    {
        JsonDocument jsonDocument;
        JsonObject root = jsonDocument.to<JsonObject>();
        root["type"] = "id";
        root["id"] = clientId(client);

        // serialize the json to a string
        String buffer;
        serializeJson(jsonDocument, buffer);
        client->sendMessage(buffer.c_str());
    }

    /**
     * Broadcasts the payload to the destination, if provided. Otherwise broadcasts to all clients except the origin, if
     * specified.
     *
     * Original implementation sent clients their own IDs so they could ignore updates they initiated. This approach
     * simplifies the client and the server implementation but may not be sufficient for all use-cases.
     */
    void transmitData(PsychicWebSocketClient *client, const String &originId)
    {
        JsonDocument jsonDocument;
        JsonObject root = jsonDocument.to<JsonObject>();
        std::string buffer; // WA serializeJson bug in Arduino-ESP32 3.1.2

        _statefulService->read(root, _stateReader);

        // serialize the json to a string
        serializeJson(jsonDocument, buffer);
        if (client)
        {
            client->sendMessage(buffer.c_str());
        }
        else
        {
            _webSocket.sendAll(buffer.c_str());
        }
    }
};

#endif
