#ifndef RELAY_AUTOMATION_H
#define RELAY_AUTOMATION_H

#include <Arduino.h>

enum class RelayAutomationMode : uint8_t {
  Manual = 0,
  Automatic = 1,
};

enum class RelayAutomationDevice : uint8_t {
  OutdoorLight = 0,
  ExhaustFan = 1,
  MotionLight1 = 2,
  MotionLight2 = 3,
};

struct RelayAutomationSettings {
  uint32_t doorLockPulseMs = 1000;
  uint32_t garageLockPulseMs = 1000;
  float outdoorLightOnBelowLux = 30.0f;
  float outdoorLightOffAboveLux = 80.0f;
  float exhaustFanOnAboveTemperature = 30.0f;
  float exhaustFanOffBelowTemperature = 28.0f;
  uint32_t motionLight1DurationMs = 60000;
  uint32_t motionLight2DurationMs = 60000;
  RelayAutomationMode outdoorLightMode = RelayAutomationMode::Manual;
  RelayAutomationMode exhaustFanMode = RelayAutomationMode::Manual;
  RelayAutomationMode motionLight1Mode = RelayAutomationMode::Manual;
  RelayAutomationMode motionLight2Mode = RelayAutomationMode::Manual;
  bool outdoorLightManualState = false;
  bool exhaustFanManualState = false;
  bool motionLight1ManualState = false;
  bool motionLight2ManualState = false;
};

struct RelayAutomationSnapshot {
  RelayAutomationSettings settings;
  bool settingsLoadedFromStorage = false;
  bool storageAvailable = false;
  bool lastSaveOk = false;
  bool doorLockActive = false;
  bool garageLockActive = false;
  bool outdoorLightOn = false;
  bool exhaustFanOn = false;
  bool motionLight1On = false;
  bool motionLight2On = false;
  uint32_t doorLockRemainingMs = 0;
  uint32_t garageLockRemainingMs = 0;
  uint32_t motionLight1RemainingMs = 0;
  uint32_t motionLight2RemainingMs = 0;
};

void relayAutomationBegin();
void relayAutomationLoop();

RelayAutomationSettings relayAutomationGetSettings();
RelayAutomationSnapshot relayAutomationGetSnapshot();
bool relayAutomationUpdateSettings(const RelayAutomationSettings &settings);
bool relayAutomationSaveSettings();
bool relayAutomationLoadSettings();

bool relayAutomationSetMode(RelayAutomationDevice device, RelayAutomationMode mode);
bool relayAutomationSetManualState(RelayAutomationDevice device, bool enabled);
bool relayAutomationToggleManualState(RelayAutomationDevice device);
bool relayAutomationPulseDoorLock();
bool relayAutomationPulseGarageLock();

const char *relayAutomationModeName(RelayAutomationMode mode);
bool relayAutomationParseDevice(const String &value, RelayAutomationDevice &device);
bool relayAutomationParseMode(const String &value, RelayAutomationMode &mode);

#endif
