#ifndef MqttPubSub_h
#define MqttPubSub_h

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <StatefulService.h>
#include <PsychicMqttClient.h>

#define MQTT_ORIGIN_ID "mqtt"

template <class T>
class MqttConnector
{
protected:
    StatefulService<T> *_statefulService;
    PsychicMqttClient *_mqttClient;
    size_t _bufferSize;

    MqttConnector(StatefulService<T> *statefulService, PsychicMqttClient *mqttClient, size_t bufferSize) : _statefulService(statefulService),
                                                                                                           _mqttClient(mqttClient),
                                                                                                           _bufferSize(bufferSize)
    {
        _mqttClient->onConnect(std::bind(&MqttConnector::onConnect, this));
    }

    virtual void onConnect() = 0;

public:
    inline PsychicMqttClient *getMqttClient() const
    {
        return _mqttClient;
    }
};

template <class T>
class MqttPub : virtual public MqttConnector<T>
{
public:
    MqttPub(JsonStateReader<T> stateReader,
            StatefulService<T> *statefulService,
            PsychicMqttClient *mqttClient,
            const String &pubTopic = "",
            bool retain = false,
            size_t bufferSize = DEFAULT_BUFFER_SIZE) : MqttConnector<T>(statefulService, mqttClient, bufferSize),
                                                       _stateReader(stateReader),
                                                       _pubTopic(pubTopic),
                                                       _retain(retain)
    {
        MqttConnector<T>::_statefulService->addUpdateHandler([&](const String &originId)
                                                             { publish(); },
                                                             false);
    }

    void setRetain(const bool retain)
    {
        _retain = retain;
        publish();
    }

    void setPubTopic(const String &pubTopic)
    {
        _pubTopic = pubTopic;
        publish();
    }

protected:
    virtual void onConnect()
    {
        publish();
    }

private:
    JsonStateReader<T> _stateReader;
    String _pubTopic;
    bool _retain;

    void publish()
    {
        if (_pubTopic.length() > 0 && MqttConnector<T>::_mqttClient->connected())
        {
            // serialize to json doc
            DynamicJsonDocument json(MqttConnector<T>::_bufferSize);
            JsonObject jsonObject = json.to<JsonObject>();
            MqttConnector<T>::_statefulService->read(jsonObject, _stateReader);

            // serialize to string
            String payload;
            serializeJson(json, payload);

            // publish the payload
            MqttConnector<T>::_mqttClient->publish(_pubTopic.c_str(), 0, _retain, payload.c_str());
        }
    }
};

template <class T>
class MqttSub : virtual public MqttConnector<T>
{
public:
    MqttSub(JsonStateUpdater<T> stateUpdater,
            StatefulService<T> *statefulService,
            PsychicMqttClient *mqttClient,
            const String &subTopic = "",
            size_t bufferSize = DEFAULT_BUFFER_SIZE) : MqttConnector<T>(statefulService, mqttClient, bufferSize),
                                                       _stateUpdater(stateUpdater),
                                                       _subTopic(subTopic)
    {
        MqttConnector<T>::_mqttClient->onMessage(std::bind(&MqttSub::onMqttMessage,
                                                           this,
                                                           std::placeholders::_1,
                                                           std::placeholders::_2,
                                                           std::placeholders::_3,
                                                           std::placeholders::_4,
                                                           std::placeholders::_5));
    }

    void setSubTopic(const String &subTopic)
    {
        if (!_subTopic.equals(subTopic))
        {
            // unsubscribe from the existing topic if one was set
            if (_subTopic.length() > 0)
            {
                MqttConnector<T>::_mqttClient->unsubscribe(_subTopic.c_str());
            }
            // set the new topic and re-configure the subscription
            _subTopic = subTopic;
            subscribe();
        }
    }

protected:
    virtual void onConnect()
    {
        subscribe();
    }

private:
    JsonStateUpdater<T> _stateUpdater;
    String _subTopic;

    void subscribe()
    {
        if (_subTopic.length() > 0)
        {
            MqttConnector<T>::_mqttClient->subscribe(_subTopic.c_str(), 2);
        }
    }

    void onMqttMessage(char *topic,
                       char *payload,
                       int retain,
                       int qos,
                       bool dup)
    {
        // we only care about the topic we are watching in this class
        if (strcmp(_subTopic.c_str(), topic))
        {
            return;
        }

        // deserialize from string
        DynamicJsonDocument json(MqttConnector<T>::_bufferSize);
        DeserializationError error = deserializeJson(json, payload);
        if (!error && json.is<JsonObject>())
        {
            JsonObject jsonObject = json.as<JsonObject>();
            MqttConnector<T>::_statefulService->update(jsonObject, _stateUpdater, MQTT_ORIGIN_ID);
        }
    }
};

template <class T>
class MqttPubSub : public MqttPub<T>, public MqttSub<T>
{
public:
    MqttPubSub(JsonStateReader<T> stateReader,
               JsonStateUpdater<T> stateUpdater,
               StatefulService<T> *statefulService,
               PsychicMqttClient *mqttClient,
               const String &pubTopic = "",
               const String &subTopic = "",
               bool retain = false,
               size_t bufferSize = DEFAULT_BUFFER_SIZE) : MqttConnector<T>(statefulService, mqttClient, bufferSize),
                                                          MqttPub<T>(stateReader, statefulService, mqttClient, pubTopic, retain, bufferSize),
                                                          MqttSub<T>(stateUpdater, statefulService, mqttClient, subTopic, bufferSize)
    {
    }

public:
    void configureTopics(const String &pubTopic, const String &subTopic)
    {
        MqttSub<T>::setSubTopic(subTopic);
        MqttPub<T>::setPubTopic(pubTopic);
    }

protected:
    void onConnect()
    {
        MqttSub<T>::onConnect();
        MqttPub<T>::onConnect();
    }
};

#endif // end MqttPubSub
