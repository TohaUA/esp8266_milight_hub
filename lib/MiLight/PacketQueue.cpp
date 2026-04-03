#include <PacketQueue.h>
#include <DebugSerial.h>

PacketQueue::PacketQueue()
  : droppedPackets(0) {}

void PacketQueue::push(const uint8_t* packet, const MiLightRemoteConfig* remoteConfig, const size_t repeatsOverride) {
  if (buffer.isFull()) {
    ++droppedPackets;
    DebugSerial.printf("WARN: Radio TX queue full, dropping oldest packet (total dropped: %d)\n", droppedPackets);
    buffer.shift(); // drop oldest
  }
  QueuedPacket qp;
  memcpy(qp.packet, packet, remoteConfig->packetFormatter->getPacketLength());
  qp.remoteConfig = remoteConfig;
  qp.repeatsOverride = repeatsOverride;
  buffer.push(qp);
}

bool PacketQueue::peek(QueuedPacket& out) const {
  if (buffer.isEmpty())
    return false;
  out = buffer.first();
  return true;
}

void PacketQueue::cyclePacket() {
  if (!buffer.isEmpty()) {
    buffer.shift();
  }
}

bool PacketQueue::isEmpty() const {
  return buffer.isEmpty();
}

size_t PacketQueue::getDroppedPacketCount() const {
  return droppedPackets;
}

size_t PacketQueue::size() const {
  return buffer.size();
}
