#ifndef MQTT_UTILS_H
#define MQTT_UTILS_H

#include "Arduino.h"

typedef void (*handleFuncType)(byte*, unsigned int);

struct MqttTopic {
  String url;
  handleFuncType handle;
  uint8_t qos;

  MqttTopic& operator=(MqttTopic& mqttTopic)
  {
    url = mqttTopic.url;
    handle = mqttTopic.handle;
    qos = mqttTopic.qos;

    return *this;
  }
};

class MqttUtils
{
  public:
    String chipId;
    MqttTopic *topicList;
    bool deviceRegistered;
    uint count;

    MqttUtils();
    void insert(String, handleFuncType, uint8_t);
    void display();
    
  private:
    uint size;
    
    void setChipId();
};

#endif