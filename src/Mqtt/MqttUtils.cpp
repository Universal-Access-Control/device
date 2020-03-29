#include "MqttUtils.h"

MqttUtils::MqttUtils()
{
  this->size = 2;
  this->count = 0;
  this->topicList = new MqttTopic [this->size];
  this->deviceRegistered = false;
  this->setChipId();
}

void MqttUtils::display()
{
  for (int index = 0; index < this->count; index++)
    Serial.println(this->topicList[index].url);
}

void MqttUtils::insert(String url, handleFuncType handle, uint8_t qos = 0)
{
  MqttTopic newTopic;
  newTopic.url = this->chipId + url;
  newTopic.handle = handle;
  newTopic.qos = qos;

  if (this->count == this->size)
  {
    MqttTopic *newTopicList = new MqttTopic [this->size + 2];

    for (int index = 0; index < this->size; ++index)
      newTopicList[index] = this->topicList[index];

    delete[] this->topicList;
    this->topicList = newTopicList;
    this->size += 2;
  }

  this->topicList[this->count] = newTopic;
  ++this->count;
}

void MqttUtils::setChipId()
{
  uint64_t chipId = ESP.getEfuseMac();
  char arrChipId[12];
  snprintf(arrChipId, 12, "%04X%08X", (uint16_t)(chipId>>32), (uint32_t)chipId);
  this->chipId = (String)arrChipId;
}
