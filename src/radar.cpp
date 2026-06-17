#include "radar.h"

#include <algorithm>

namespace Radar {

namespace {

constexpr uint8_t REPORT_HEADER[] = {0xF4, 0xF3, 0xF2, 0xF1};
constexpr uint8_t REPORT_FOOTER[] = {0xF8, 0xF7, 0xF6, 0xF5};
constexpr uint8_t COMMAND_HEADER[] = {0xFD, 0xFC, 0xFB, 0xFA};
constexpr uint8_t COMMAND_FOOTER[] = {0x04, 0x03, 0x02, 0x01};

HardwareSerial *radarSerial = &Serial2;
std::vector<uint8_t> rxBuf;
TargetData lastTarget;

LogCallback logCallback = nullptr;
TargetCallback targetCallback = nullptr;
AckCallback ackCallback = nullptr;
FirmwareCallback firmwareCallback = nullptr;
MacCallback macCallback = nullptr;

void addLog(const String &message) {
  if (logCallback) {
    logCallback(message);
    return;
  }

  Serial.println(message);
}

bool matchesAt(const std::vector<uint8_t> &data, size_t pos, const uint8_t *pattern, size_t size) {
  if (pos + size > data.size()) return false;
  for (size_t i = 0; i < size; ++i) {
    if (data[pos + i] != pattern[i]) return false;
  }
  return true;
}

uint16_t readU16(const std::vector<uint8_t> &data, size_t pos) {
  if (pos + 1 >= data.size()) return 0;
  return uint16_t(data[pos]) | (uint16_t(data[pos + 1]) << 8);
}

uint32_t readU32(const std::vector<uint8_t> &data, size_t pos) {
  if (pos + 3 >= data.size()) return 0;
  return uint32_t(data[pos]) | (uint32_t(data[pos + 1]) << 8) | (uint32_t(data[pos + 2]) << 16) |
         (uint32_t(data[pos + 3]) << 24);
}

void appendU16(std::vector<uint8_t> &data, uint16_t value) {
  data.push_back(value & 0xFF);
  data.push_back((value >> 8) & 0xFF);
}

void appendU32(std::vector<uint8_t> &data, uint32_t value) {
  data.push_back(value & 0xFF);
  data.push_back((value >> 8) & 0xFF);
  data.push_back((value >> 16) & 0xFF);
  data.push_back((value >> 24) & 0xFF);
}

void parseEngineeringData(TargetData &target, const std::vector<uint8_t> &payload) {
  if (payload.size() < 15) return;

  target.engineeringMode = true;
  target.maxMovingGate = std::min<uint8_t>(payload[11], MAX_GATE);
  target.maxStationaryGate = std::min<uint8_t>(payload[12], MAX_GATE);

  size_t idx = 13;
  for (uint8_t gate = 0; gate <= target.maxMovingGate && idx < payload.size(); ++gate) {
    target.movingGateEnergy[gate] = payload[idx++];
  }

  for (uint8_t gate = 0; gate <= target.maxStationaryGate && idx < payload.size(); ++gate) {
    target.stationaryGateEnergy[gate] = payload[idx++];
  }
}

void handleReportFrame(const std::vector<uint8_t> &frame) {
  uint16_t payloadLen = readU16(frame, 4);
  if (payloadLen < 13 || frame.size() < size_t(10 + payloadLen)) return;

  std::vector<uint8_t> payload(frame.begin() + 6, frame.begin() + 6 + payloadLen);
  uint8_t dataType = payload[0];
  if (payload[1] != 0xAA || payload[payload.size() - 2] != 0x55 || payload[payload.size() - 1] != 0x00) {
    return;
  }

  TargetData target;
  target.state = static_cast<TargetState>(payload[2]);
  target.movingDistanceCm = readU16(payload, 3);
  target.movingEnergy = payload[5];
  target.stationaryDistanceCm = readU16(payload, 6);
  target.stationaryEnergy = payload[8];
  target.detectionDistanceCm = readU16(payload, 9);
  target.engineeringMode = dataType == 0x01;
  target.updatedAtMs = millis();

  if (dataType == 0x01) {
    parseEngineeringData(target, payload);
  }

  lastTarget = target;
  if (targetCallback) targetCallback(lastTarget);
}

String formatFirmware(const std::vector<uint8_t> &payload) {
  if (payload.size() < 8) return "";

  uint16_t major = readU16(payload, 4);
  uint32_t minor = readU32(payload, 6);
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "V%u.%02u.%08lu", major >> 8, major & 0xFF, (unsigned long)minor);
  return String(buffer);
}

String formatMac(const std::vector<uint8_t> &payload) {
  if (payload.size() < 8) return "";

  char buffer[18];
  snprintf(buffer,
           sizeof(buffer),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           payload[2],
           payload[3],
           payload[4],
           payload[5],
           payload[6],
           payload[7]);
  return String(buffer);
}

void handleAckFrame(const std::vector<uint8_t> &frame) {
  uint16_t payloadLen = readU16(frame, 4);
  if (payloadLen < 4 || frame.size() < size_t(10 + payloadLen)) return;

  std::vector<uint8_t> payload(frame.begin() + 6, frame.begin() + 6 + payloadLen);
  uint16_t ackCommand = readU16(payload, 0);
  AckData ack;
  ack.command = ackCommand >= 0x0100 ? ackCommand - 0x0100 : ackCommand;
  ack.status = readU16(payload, 2);
  if (payload.size() > 4) {
    ack.payload.assign(payload.begin() + 4, payload.end());
  }

  if (ackCallback) ackCallback(ack);

  if (ack.status != 0) {
    addLog("LD2410B command failed: 0x" + String(ack.command, HEX) + ", status " + String(ack.status));
    return;
  }

  if (ack.command == 0x00A0) {
    String version = formatFirmware(payload);
    addLog("LD2410B firmware: " + version);
    if (firmwareCallback) firmwareCallback(version);
  } else if (ack.command == 0x00A5) {
    String mac = formatMac(payload);
    addLog("LD2410B MAC: " + mac);
    if (macCallback) macCallback(mac);
  } else {
    addLog("LD2410B ACK: 0x" + String(ack.command, HEX));
  }
}

void handleFrame(const std::vector<uint8_t> &frame, bool reportFrame) {
  if (reportFrame) {
    handleReportFrame(frame);
  } else {
    handleAckFrame(frame);
  }
}

void parseRxBuffer() {
  while (rxBuf.size() >= 10) {
    size_t reportPos = rxBuf.size();
    size_t commandPos = rxBuf.size();

    for (size_t i = 0; i + 4 <= rxBuf.size(); ++i) {
      if (reportPos == rxBuf.size() && matchesAt(rxBuf, i, REPORT_HEADER, sizeof(REPORT_HEADER))) {
        reportPos = i;
      }
      if (commandPos == rxBuf.size() && matchesAt(rxBuf, i, COMMAND_HEADER, sizeof(COMMAND_HEADER))) {
        commandPos = i;
      }
      if (reportPos != rxBuf.size() && commandPos != rxBuf.size()) break;
    }

    size_t framePos = std::min(reportPos, commandPos);
    if (framePos == rxBuf.size()) {
      rxBuf.clear();
      return;
    }

    if (framePos > 0) {
      rxBuf.erase(rxBuf.begin(), rxBuf.begin() + framePos);
    }

    bool reportFrame = matchesAt(rxBuf, 0, REPORT_HEADER, sizeof(REPORT_HEADER));
    const uint8_t *footer = reportFrame ? REPORT_FOOTER : COMMAND_FOOTER;
    uint16_t payloadLen = readU16(rxBuf, 4);
    if (payloadLen > 128) {
      rxBuf.erase(rxBuf.begin());
      continue;
    }

    size_t frameLen = 4 + 2 + payloadLen + 4;
    if (rxBuf.size() < frameLen) return;

    if (!matchesAt(rxBuf, frameLen - 4, footer, 4)) {
      rxBuf.erase(rxBuf.begin());
      continue;
    }

    std::vector<uint8_t> frame(rxBuf.begin(), rxBuf.begin() + frameLen);
    rxBuf.erase(rxBuf.begin(), rxBuf.begin() + frameLen);
    handleFrame(frame, reportFrame);
  }
}

}  // namespace

void begin(uint32_t baud, int rxPin, int txPin, HardwareSerial &serial) {
  radarSerial = &serial;
  radarSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
  rxBuf.clear();
  lastTarget = TargetData();
  addLog("LD2410B UART started on RX GPIO" + String(rxPin) + ", TX GPIO" + String(txPin));
}

void loop() {
  while (radarSerial->available()) {
    rxBuf.push_back((uint8_t)radarSerial->read());
    if (rxBuf.size() > 512) {
      rxBuf.erase(rxBuf.begin(), rxBuf.begin() + (rxBuf.size() - 256));
    }
    yield();
  }

  parseRxBuffer();
}

void setLogCallback(LogCallback callback) {
  logCallback = callback;
}

void setTargetCallback(TargetCallback callback) {
  targetCallback = callback;
}

void setAckCallback(AckCallback callback) {
  ackCallback = callback;
}

void setFirmwareCallback(FirmwareCallback callback) {
  firmwareCallback = callback;
}

void setMacCallback(MacCallback callback) {
  macCallback = callback;
}

bool isPresent() {
  return lastTarget.state != TargetState::None;
}

bool hasMovingTarget() {
  return lastTarget.state == TargetState::Moving || lastTarget.state == TargetState::MovingAndStationary;
}

bool hasStationaryTarget() {
  return lastTarget.state == TargetState::Stationary || lastTarget.state == TargetState::MovingAndStationary;
}

const TargetData &getTargetData() {
  return lastTarget;
}

uint32_t getLastUpdateAgeMs() {
  return lastTarget.updatedAtMs == 0 ? UINT32_MAX : millis() - lastTarget.updatedAtMs;
}

String targetStateName(TargetState state) {
  switch (state) {
    case TargetState::None:
      return "none";
    case TargetState::Moving:
      return "moving";
    case TargetState::Stationary:
      return "stationary";
    case TargetState::MovingAndStationary:
      return "moving_and_stationary";
    default:
      return "unknown";
  }
}

void sendCommand(uint16_t command) {
  sendCommand(command, std::vector<uint8_t>());
}

void sendCommand(uint16_t command, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> frame;
  frame.reserve(10 + payload.size() + 2);
  frame.insert(frame.end(), COMMAND_HEADER, COMMAND_HEADER + sizeof(COMMAND_HEADER));
  appendU16(frame, payload.size() + 2);
  appendU16(frame, command);
  frame.insert(frame.end(), payload.begin(), payload.end());
  frame.insert(frame.end(), COMMAND_FOOTER, COMMAND_FOOTER + sizeof(COMMAND_FOOTER));
  radarSerial->write(frame.data(), frame.size());
}

void enableConfiguration() {
  std::vector<uint8_t> payload;
  appendU16(payload, 0x0001);
  sendCommand(0x00FF, payload);
  addLog("LD2410B configuration mode requested");
}

void endConfiguration() {
  sendCommand(0x00FE);
  addLog("LD2410B end configuration requested");
}

void setMaxDistanceAndNoOneDuration(uint8_t movingGate, uint8_t stationaryGate, uint16_t noOneDurationSec) {
  std::vector<uint8_t> payload;
  appendU16(payload, 0x0000);
  appendU32(payload, std::min<uint8_t>(movingGate, MAX_GATE));
  appendU16(payload, 0x0001);
  appendU32(payload, std::min<uint8_t>(stationaryGate, MAX_GATE));
  appendU16(payload, 0x0002);
  appendU32(payload, noOneDurationSec);
  sendCommand(0x0060, payload);
  addLog("LD2410B distance/delay config sent");
}

void readParameters() {
  sendCommand(0x0061);
  addLog("LD2410B read parameters requested");
}

void enableEngineeringMode() {
  sendCommand(0x0062);
  addLog("LD2410B engineering mode requested");
}

void disableEngineeringMode() {
  sendCommand(0x0063);
  addLog("LD2410B engineering mode disable requested");
}

void setGateSensitivity(uint16_t gate, uint8_t movingSensitivity, uint8_t stationarySensitivity) {
  std::vector<uint8_t> payload;
  appendU16(payload, 0x0000);
  appendU32(payload, gate == ALL_GATES ? ALL_GATES : std::min<uint16_t>(gate, MAX_GATE));
  appendU16(payload, 0x0001);
  appendU32(payload, std::min<uint8_t>(movingSensitivity, 100));
  appendU16(payload, 0x0002);
  appendU32(payload, std::min<uint8_t>(stationarySensitivity, 100));
  sendCommand(0x0064, payload);
  addLog("LD2410B gate sensitivity config sent");
}

void setAllGateSensitivity(uint8_t movingSensitivity, uint8_t stationarySensitivity) {
  setGateSensitivity(ALL_GATES, movingSensitivity, stationarySensitivity);
}

void getFirmwareVersion() {
  sendCommand(0x00A0);
  addLog("LD2410B firmware requested");
}

void setBaudRate(BaudRateIndex index) {
  std::vector<uint8_t> payload;
  appendU16(payload, static_cast<uint16_t>(index));
  sendCommand(0x00A1, payload);
  addLog("LD2410B baud rate config sent");
}

void factoryReset() {
  sendCommand(0x00A2);
  addLog("LD2410B factory reset requested");
}

void restart() {
  sendCommand(0x00A3);
  addLog("LD2410B restart requested");
}

void setBluetooth(bool enabled) {
  std::vector<uint8_t> payload;
  appendU16(payload, enabled ? 0x0001 : 0x0000);
  sendCommand(0x00A4, payload);
  addLog(String("LD2410B Bluetooth ") + (enabled ? "enable" : "disable") + " requested");
}

void getMacAddress() {
  std::vector<uint8_t> payload;
  appendU16(payload, 0x0001);
  sendCommand(0x00A5, payload);
  addLog("LD2410B MAC requested");
}

}  // namespace Radar

void radar_begin(uint32_t baud) {
  Radar::begin(baud);
}

void radar_loop() {
  Radar::loop();
}

bool radar_is_present() {
  return Radar::isPresent();
}
