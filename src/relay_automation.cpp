#include <Arduino.h>
#include <math.h>

#include "modules.h"
#include "pinout.h"
#include "relay_automation.h"

namespace {
constexpr uint32_t SETTINGS_MAGIC = 0x524C5941;
constexpr uint16_t SETTINGS_VERSION = 1;
constexpr uint32_t SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 4096UL;

constexpr uint8_t RELAY_DOOR_LOCK = 0;
constexpr uint8_t RELAY_GARAGE_LOCK = 1;
constexpr uint8_t RELAY_OUTDOOR_LIGHT = 2;
constexpr uint8_t RELAY_EXHAUST_FAN = 5;
constexpr uint8_t RELAY_MOTION_LIGHT_1 = 6;
constexpr uint8_t RELAY_MOTION_LIGHT_2 = 7;

constexpr uint32_t MIN_PULSE_MS = 100;
constexpr uint32_t MAX_PULSE_MS = 30000;
constexpr uint32_t MIN_MOTION_HOLD_MS = 1000;
constexpr uint32_t MAX_MOTION_HOLD_MS = 3600000;

struct StoredRelayAutomationSettings {
  uint32_t magic = SETTINGS_MAGIC;
  uint16_t version = SETTINGS_VERSION;
  uint16_t size = 0;
  RelayAutomationSettings settings;
  uint32_t crc = 0;
};

RelayAutomationSettings settings;
bool settingsLoadedFromStorage = false;
bool storageAvailable = false;
bool lastSaveOk = false;
bool doorLockPulseActive = false;
bool garageLockPulseActive = false;
uint32_t doorLockPulseEndsAt = 0;
uint32_t garageLockPulseEndsAt = 0;
uint32_t motionLight1HoldUntil = 0;
uint32_t motionLight2HoldUntil = 0;

uint32_t fnv1a(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t settingsCrc(const StoredRelayAutomationSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored),
               sizeof(StoredRelayAutomationSettings) - sizeof(stored.crc));
}

RelayAutomationMode sanitizeMode(RelayAutomationMode mode) {
  return mode == RelayAutomationMode::Automatic ? RelayAutomationMode::Automatic : RelayAutomationMode::Manual;
}

uint32_t boundedDuration(uint32_t value, uint32_t minimum, uint32_t maximum, uint32_t fallback) {
  if (value < minimum || value > maximum) {
    return fallback;
  }
  return value;
}

void sanitizeSettings(RelayAutomationSettings &value) {
  const RelayAutomationSettings defaults;

  value.doorLockPulseMs = boundedDuration(value.doorLockPulseMs, MIN_PULSE_MS, MAX_PULSE_MS, defaults.doorLockPulseMs);
  value.garageLockPulseMs = boundedDuration(value.garageLockPulseMs, MIN_PULSE_MS, MAX_PULSE_MS, defaults.garageLockPulseMs);
  value.motionLight1DurationMs =
    boundedDuration(value.motionLight1DurationMs, MIN_MOTION_HOLD_MS, MAX_MOTION_HOLD_MS, defaults.motionLight1DurationMs);
  value.motionLight2DurationMs =
    boundedDuration(value.motionLight2DurationMs, MIN_MOTION_HOLD_MS, MAX_MOTION_HOLD_MS, defaults.motionLight2DurationMs);

  if (!isfinite(value.outdoorLightOnBelowLux) || value.outdoorLightOnBelowLux < 0.0f) {
    value.outdoorLightOnBelowLux = defaults.outdoorLightOnBelowLux;
  }
  if (!isfinite(value.outdoorLightOffAboveLux) || value.outdoorLightOffAboveLux <= value.outdoorLightOnBelowLux) {
    value.outdoorLightOffAboveLux = max(value.outdoorLightOnBelowLux + 5.0f, defaults.outdoorLightOffAboveLux);
  }
  if (!isfinite(value.exhaustFanOnAboveTemperature)) {
    value.exhaustFanOnAboveTemperature = defaults.exhaustFanOnAboveTemperature;
  }
  if (!isfinite(value.exhaustFanOffBelowTemperature) ||
      value.exhaustFanOffBelowTemperature >= value.exhaustFanOnAboveTemperature) {
    value.exhaustFanOffBelowTemperature = min(value.exhaustFanOnAboveTemperature - 1.0f, defaults.exhaustFanOffBelowTemperature);
  }

  value.outdoorLightMode = sanitizeMode(value.outdoorLightMode);
  value.exhaustFanMode = sanitizeMode(value.exhaustFanMode);
  value.motionLight1Mode = sanitizeMode(value.motionLight1Mode);
  value.motionLight2Mode = sanitizeMode(value.motionLight2Mode);
}

RelayAutomationMode getMode(RelayAutomationDevice device) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      return settings.outdoorLightMode;
    case RelayAutomationDevice::ExhaustFan:
      return settings.exhaustFanMode;
    case RelayAutomationDevice::MotionLight1:
      return settings.motionLight1Mode;
    case RelayAutomationDevice::MotionLight2:
      return settings.motionLight2Mode;
  }
  return RelayAutomationMode::Manual;
}

bool getManualState(RelayAutomationDevice device) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      return settings.outdoorLightManualState;
    case RelayAutomationDevice::ExhaustFan:
      return settings.exhaustFanManualState;
    case RelayAutomationDevice::MotionLight1:
      return settings.motionLight1ManualState;
    case RelayAutomationDevice::MotionLight2:
      return settings.motionLight2ManualState;
  }
  return false;
}

uint8_t relayForDevice(RelayAutomationDevice device) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      return RELAY_OUTDOOR_LIGHT;
    case RelayAutomationDevice::ExhaustFan:
      return RELAY_EXHAUST_FAN;
    case RelayAutomationDevice::MotionLight1:
      return RELAY_MOTION_LIGHT_1;
    case RelayAutomationDevice::MotionLight2:
      return RELAY_MOTION_LIGHT_2;
  }
  return RELAY_OUTDOOR_LIGHT;
}

void applyManualState(RelayAutomationDevice device) {
  if (getMode(device) == RelayAutomationMode::Manual) {
    relaySet(relayForDevice(device), getManualState(device));
  }
}

void applyManualStates() {
  applyManualState(RelayAutomationDevice::OutdoorLight);
  applyManualState(RelayAutomationDevice::ExhaustFan);
  applyManualState(RelayAutomationDevice::MotionLight1);
  applyManualState(RelayAutomationDevice::MotionLight2);
}

void setAllRelaysSafeOff() {
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    relaySet(channel, false);
  }
  doorLockPulseActive = false;
  garageLockPulseActive = false;
}

void serviceDoorPulses(uint32_t now) {
  if (doorLockPulseActive && static_cast<int32_t>(now - doorLockPulseEndsAt) >= 0) {
    doorLockPulseActive = false;
    relaySet(RELAY_DOOR_LOCK, false);
  }

  if (garageLockPulseActive && static_cast<int32_t>(now - garageLockPulseEndsAt) >= 0) {
    garageLockPulseActive = false;
    relaySet(RELAY_GARAGE_LOCK, false);
  }
}

void serviceOutdoorLight(const ModuleSnapshot &modules) {
  if (settings.outdoorLightMode != RelayAutomationMode::Automatic || !modules.lightValid || isnan(modules.lux)) {
    return;
  }

  const bool current = relayGet(RELAY_OUTDOOR_LIGHT);
  if (!current && modules.lux < settings.outdoorLightOnBelowLux) {
    relaySet(RELAY_OUTDOOR_LIGHT, true);
  } else if (current && modules.lux > settings.outdoorLightOffAboveLux) {
    relaySet(RELAY_OUTDOOR_LIGHT, false);
  }
}

void serviceExhaustFan(const ModuleSnapshot &modules) {
  if (settings.exhaustFanMode != RelayAutomationMode::Automatic || !modules.climateValid || isnan(modules.temperatureC)) {
    return;
  }

  const bool current = relayGet(RELAY_EXHAUST_FAN);
  if (!current && modules.temperatureC > settings.exhaustFanOnAboveTemperature) {
    relaySet(RELAY_EXHAUST_FAN, true);
  } else if (current && modules.temperatureC < settings.exhaustFanOffBelowTemperature) {
    relaySet(RELAY_EXHAUST_FAN, false);
  }
}

void serviceMotionLight(const ModuleSnapshot &modules,
                        RelayAutomationMode mode,
                        bool motionActive,
                        uint8_t relay,
                        uint32_t durationMs,
                        uint32_t &holdUntil) {
  if (mode != RelayAutomationMode::Automatic) {
    return;
  }

  const uint32_t now = millis();
  if (motionActive) {
    holdUntil = now + durationMs;
  }

  relaySet(relay, static_cast<int32_t>(now - holdUntil) < 0);
}

uint32_t remainingMs(bool active, uint32_t endsAt) {
  if (!active) {
    return 0;
  }

  const int32_t remaining = static_cast<int32_t>(endsAt - millis());
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}
}

void relayAutomationBegin() {
  settings = RelayAutomationSettings();
  setAllRelaysSafeOff();
  relayAutomationLoadSettings();
  applyManualStates();
}

void relayAutomationLoop() {
  const uint32_t now = millis();
  serviceDoorPulses(now);

  const ModuleSnapshot modules = modulesGetSnapshot();
  serviceOutdoorLight(modules);
  serviceExhaustFan(modules);
  serviceMotionLight(modules,
                     settings.motionLight1Mode,
                     modules.motion1Active,
                     RELAY_MOTION_LIGHT_1,
                     settings.motionLight1DurationMs,
                     motionLight1HoldUntil);
  serviceMotionLight(modules,
                     settings.motionLight2Mode,
                     modules.motion2Active,
                     RELAY_MOTION_LIGHT_2,
                     settings.motionLight2DurationMs,
                     motionLight2HoldUntil);
}

RelayAutomationSettings relayAutomationGetSettings() {
  return settings;
}

RelayAutomationSnapshot relayAutomationGetSnapshot() {
  RelayAutomationSnapshot snapshot;
  snapshot.settings = settings;
  snapshot.settingsLoadedFromStorage = settingsLoadedFromStorage;
  snapshot.storageAvailable = storageAvailable;
  snapshot.lastSaveOk = lastSaveOk;
  snapshot.doorLockActive = relayGet(RELAY_DOOR_LOCK);
  snapshot.garageLockActive = relayGet(RELAY_GARAGE_LOCK);
  snapshot.outdoorLightOn = relayGet(RELAY_OUTDOOR_LIGHT);
  snapshot.exhaustFanOn = relayGet(RELAY_EXHAUST_FAN);
  snapshot.motionLight1On = relayGet(RELAY_MOTION_LIGHT_1);
  snapshot.motionLight2On = relayGet(RELAY_MOTION_LIGHT_2);
  snapshot.doorLockRemainingMs = remainingMs(doorLockPulseActive, doorLockPulseEndsAt);
  snapshot.garageLockRemainingMs = remainingMs(garageLockPulseActive, garageLockPulseEndsAt);
  snapshot.motionLight1RemainingMs = remainingMs(snapshot.motionLight1On, motionLight1HoldUntil);
  snapshot.motionLight2RemainingMs = remainingMs(snapshot.motionLight2On, motionLight2HoldUntil);
  return snapshot;
}

bool relayAutomationUpdateSettings(const RelayAutomationSettings &updatedSettings) {
  settings = updatedSettings;
  sanitizeSettings(settings);
  setAllRelaysSafeOff();
  applyManualStates();
  return relayAutomationSaveSettings();
}

bool relayAutomationSaveSettings() {
  StoredRelayAutomationSettings stored;
  stored.size = sizeof(StoredRelayAutomationSettings);
  stored.settings = settings;
  sanitizeSettings(stored.settings);
  stored.crc = settingsCrc(stored);

  storageAvailable = storageEraseSector(SETTINGS_ADDRESS);
  if (!storageAvailable) {
    lastSaveOk = false;
    return false;
  }

  lastSaveOk = storageWriteBytes(SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
  storageAvailable = storageAvailable && lastSaveOk;
  return lastSaveOk;
}

bool relayAutomationLoadSettings() {
  StoredRelayAutomationSettings stored;
  storageAvailable = storageReadBytes(SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored));
  if (!storageAvailable || stored.magic != SETTINGS_MAGIC || stored.version != SETTINGS_VERSION ||
      stored.size != sizeof(StoredRelayAutomationSettings) || stored.crc != settingsCrc(stored)) {
    settings = RelayAutomationSettings();
    settingsLoadedFromStorage = false;
    return false;
  }

  settings = stored.settings;
  sanitizeSettings(settings);
  settingsLoadedFromStorage = true;
  return true;
}

bool relayAutomationSetMode(RelayAutomationDevice device, RelayAutomationMode mode) {
  mode = sanitizeMode(mode);
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      settings.outdoorLightMode = mode;
      break;
    case RelayAutomationDevice::ExhaustFan:
      settings.exhaustFanMode = mode;
      break;
    case RelayAutomationDevice::MotionLight1:
      settings.motionLight1Mode = mode;
      motionLight1HoldUntil = 0;
      break;
    case RelayAutomationDevice::MotionLight2:
      settings.motionLight2Mode = mode;
      motionLight2HoldUntil = 0;
      break;
  }

  if (mode == RelayAutomationMode::Manual) {
    applyManualState(device);
  }

  return relayAutomationSaveSettings();
}

bool relayAutomationSetManualState(RelayAutomationDevice device, bool enabled) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      settings.outdoorLightManualState = enabled;
      break;
    case RelayAutomationDevice::ExhaustFan:
      settings.exhaustFanManualState = enabled;
      break;
    case RelayAutomationDevice::MotionLight1:
      settings.motionLight1ManualState = enabled;
      break;
    case RelayAutomationDevice::MotionLight2:
      settings.motionLight2ManualState = enabled;
      break;
  }

  applyManualState(device);
  return relayAutomationSaveSettings();
}

bool relayAutomationToggleManualState(RelayAutomationDevice device) {
  return relayAutomationSetManualState(device, !getManualState(device));
}

bool relayAutomationPulseDoorLock() {
  const bool ok = relaySet(RELAY_DOOR_LOCK, true);
  if (ok) {
    doorLockPulseActive = true;
    doorLockPulseEndsAt = millis() + settings.doorLockPulseMs;
  }
  return ok;
}

bool relayAutomationPulseGarageLock() {
  const bool ok = relaySet(RELAY_GARAGE_LOCK, true);
  if (ok) {
    garageLockPulseActive = true;
    garageLockPulseEndsAt = millis() + settings.garageLockPulseMs;
  }
  return ok;
}

const char *relayAutomationModeName(RelayAutomationMode mode) {
  return mode == RelayAutomationMode::Automatic ? "automatic" : "manual";
}

bool relayAutomationParseDevice(const String &value, RelayAutomationDevice &device) {
  if (value == "outdoorLight") {
    device = RelayAutomationDevice::OutdoorLight;
    return true;
  }
  if (value == "exhaustFan") {
    device = RelayAutomationDevice::ExhaustFan;
    return true;
  }
  if (value == "motionLight1") {
    device = RelayAutomationDevice::MotionLight1;
    return true;
  }
  if (value == "motionLight2") {
    device = RelayAutomationDevice::MotionLight2;
    return true;
  }
  return false;
}

bool relayAutomationParseMode(const String &value, RelayAutomationMode &mode) {
  if (value == "automatic" || value == "auto") {
    mode = RelayAutomationMode::Automatic;
    return true;
  }
  if (value == "manual") {
    mode = RelayAutomationMode::Manual;
    return true;
  }
  return false;
}
