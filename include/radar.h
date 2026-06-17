#ifndef RADAR_H
#define RADAR_H

#include <Arduino.h>
#include <vector>

#include "pinout.h"

namespace Radar {

constexpr uint8_t MAX_GATE = 8;
constexpr uint16_t ALL_GATES = 0xFFFF;
constexpr uint32_t DEFAULT_BAUD = 256000;

enum class TargetState : uint8_t {
  None = 0,
  Moving = 1,
  Stationary = 2,
  MovingAndStationary = 3,
};

enum class BaudRateIndex : uint16_t {
  Baud9600 = 1,
  Baud19200 = 2,
  Baud38400 = 3,
  Baud57600 = 4,
  Baud115200 = 5,
  Baud230400 = 6,
  Baud256000 = 7,
  Baud460800 = 8,
};

struct TargetData {
  TargetState state = TargetState::None;
  uint16_t movingDistanceCm = 0;
  uint8_t movingEnergy = 0;
  uint16_t stationaryDistanceCm = 0;
  uint8_t stationaryEnergy = 0;
  uint16_t detectionDistanceCm = 0;
  bool engineeringMode = false;
  uint8_t maxMovingGate = 0;
  uint8_t maxStationaryGate = 0;
  uint8_t movingGateEnergy[MAX_GATE + 1] = {};
  uint8_t stationaryGateEnergy[MAX_GATE + 1] = {};
  uint32_t updatedAtMs = 0;
};

struct AckData {
  uint16_t command = 0;
  uint16_t status = 0;
  std::vector<uint8_t> payload;
};

using LogCallback = void (*)(const String &message);
using TargetCallback = void (*)(const TargetData &target);
using AckCallback = void (*)(const AckData &ack);
using FirmwareCallback = void (*)(const String &version);
using MacCallback = void (*)(const String &mac);

void begin(uint32_t baud = DEFAULT_BAUD,
           int rxPin = PIN_RADAR_RX,
           int txPin = PIN_RADAR_TX,
           HardwareSerial &serial = Serial2);
void loop();

void setLogCallback(LogCallback callback);
void setTargetCallback(TargetCallback callback);
void setAckCallback(AckCallback callback);
void setFirmwareCallback(FirmwareCallback callback);
void setMacCallback(MacCallback callback);

bool isPresent();
bool hasMovingTarget();
bool hasStationaryTarget();
const TargetData &getTargetData();
uint32_t getLastUpdateAgeMs();
String targetStateName(TargetState state);

void sendCommand(uint16_t command);
void sendCommand(uint16_t command, const std::vector<uint8_t> &payload);
void enableConfiguration();
void endConfiguration();
void setMaxDistanceAndNoOneDuration(uint8_t movingGate, uint8_t stationaryGate, uint16_t noOneDurationSec);
void readParameters();
void enableEngineeringMode();
void disableEngineeringMode();
void setGateSensitivity(uint16_t gate, uint8_t movingSensitivity, uint8_t stationarySensitivity);
void setAllGateSensitivity(uint8_t movingSensitivity, uint8_t stationarySensitivity);
void getFirmwareVersion();
void setBaudRate(BaudRateIndex index);
void factoryReset();
void restart();
void setBluetooth(bool enabled);
void getMacAddress();

}  // namespace Radar

void radar_begin(uint32_t baud = Radar::DEFAULT_BAUD);
void radar_loop();
bool radar_is_present();

#endif
