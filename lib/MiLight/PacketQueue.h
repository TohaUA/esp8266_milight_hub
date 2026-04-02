#pragma once

#include <CircularBuffer.h>
#include <MiLightRadioConfig.h>
#include <MiLightRemoteConfig.h>

#ifndef MILIGHT_MAX_QUEUED_PACKETS
#define MILIGHT_MAX_QUEUED_PACKETS 20
#endif

struct QueuedPacket {
  uint8_t packet[MILIGHT_MAX_PACKET_LENGTH];
  const MiLightRemoteConfig* remoteConfig;
  size_t repeatsOverride;
};

class PacketQueue {
public:
  PacketQueue();

  void push(const uint8_t* packet, const MiLightRemoteConfig* remoteConfig, const size_t repeatsOverride);
  const QueuedPacket* currentPacket() const;
  void cyclePacket();
  bool isEmpty() const;
  size_t size() const;
  size_t getDroppedPacketCount() const;

private:
  size_t droppedPackets;
  CircularBuffer<QueuedPacket, MILIGHT_MAX_QUEUED_PACKETS> buffer;
};
