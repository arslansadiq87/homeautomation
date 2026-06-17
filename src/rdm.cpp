#include <Arduino.h>
#include <Preferences.h>
#include <SoftwareSerial.h>
#include <vector>

#include "pinout.h"
#include "rdm.h"
#include "relay_automation.h"
#include "modules.h"

namespace {
constexpr uint32_t RFID_SETTINGS_MAGIC = 0x52464944;
constexpr uint16_t RFID_SETTINGS_VERSION = 1;
constexpr uint32_t RFID_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 8192UL;
constexpr uint32_t RDM_BAUD_RATE = 9600;
constexpr uint8_t RDM_FRAME_LENGTH = 14;
constexpr uint8_t RDM_TAG_HEX_LENGTH = 10;
constexpr uint8_t RDM_CHECKSUM_HEX_LENGTH = 2;
constexpr uint8_t RDM_MAX_TAGS = 32;
constexpr uint32_t RDM_TAG_HOLD_MS = 3000;
constexpr uint32_t RDM_DUPLICATE_SUPPRESS_MS = 2000;
constexpr uint32_t RDM_ADD_MODE_TIMEOUT_MS = 30000;
constexpr uint16_t RDM_SCAN_BEEP_HZ = 1800;
constexpr uint16_t RDM_AUTHORIZED_BEEP_HZ = 2600;
constexpr uint32_t RDM_SCAN_BEEP_MS = 80;
constexpr uint32_t RDM_AUTHORIZED_BEEP_MS = 120;
constexpr uint32_t RDM_AUTHORIZED_BEEP_DELAY_MS = 160;

EspSoftwareSerial::UART rdmSerial;
Preferences preferences;
RdmSnapshot snapshot;
std::vector<String> authorizedTags;
std::vector<String> pendingTags;
uint8_t frame[RDM_FRAME_LENGTH];
uint8_t frameIndex = 0;
bool addModeActive = false;
uint32_t addModeExpiresAt = 0;
String lastProcessedTag;
uint32_t lastProcessedMs = 0;
bool authorizedBeepPending = false;
uint32_t authorizedBeepAt = 0;
bool doorUnlockEnabled = true;

struct StoredRfidSettings {
  uint32_t magic = RFID_SETTINGS_MAGIC;
  uint16_t version = RFID_SETTINGS_VERSION;
  uint16_t size = 0;
  bool doorUnlockEnabled = true;
  uint8_t tagCount = 0;
  char tags[RDM_MAX_TAGS][RDM_TAG_HEX_LENGTH + 1] = {};
  uint32_t crc = 0;
};

uint32_t fnv1a(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t settingsCrc(const StoredRfidSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredRfidSettings) - sizeof(stored.crc));
}

String normalizeHex(String tag) {
  tag.trim();
  tag.toUpperCase();

  String normalized;
  normalized.reserve(tag.length());
  for (size_t i = 0; i < tag.length(); i++) {
    const char c = tag[i];
    if (isxdigit(c)) {
      normalized += c;
    }
  }
  return normalized;
}

uint8_t hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return 0;
}

uint8_t parseHexByte(const uint8_t *data) {
  return static_cast<uint8_t>((hexValue(static_cast<char>(data[0])) << 4) | hexValue(static_cast<char>(data[1])));
}

bool isHexAscii(const uint8_t *data, uint8_t length) {
  for (uint8_t i = 0; i < length; i++) {
    if (!isxdigit(static_cast<char>(data[i]))) {
      return false;
    }
  }
  return true;
}

bool saveTags() {
  StoredRfidSettings stored;
  stored.size = sizeof(StoredRfidSettings);
  stored.doorUnlockEnabled = doorUnlockEnabled;
  stored.tagCount = min(static_cast<size_t>(RDM_MAX_TAGS), authorizedTags.size());

  for (uint8_t i = 0; i < stored.tagCount; i++) {
    authorizedTags[i].toCharArray(stored.tags[i], RDM_TAG_HEX_LENGTH + 1);
  }

  stored.crc = settingsCrc(stored);
  if (!storageEraseSector(RFID_SETTINGS_ADDRESS) ||
      !storageWriteBytes(RFID_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored))) {
    snapshot.lastEvent = "RFID storage save failed";
    return false;
  }

  snapshot.tagCount = authorizedTags.size();
  snapshot.doorUnlockEnabled = doorUnlockEnabled;
  return true;
}

uint32_t addModeRemainingMs() {
  if (!addModeActive) {
    return 0;
  }

  const int32_t remaining = static_cast<int32_t>(addModeExpiresAt - millis());
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

void extendAddModeTimeout() {
  addModeExpiresAt = millis() + RDM_ADD_MODE_TIMEOUT_MS;
}

bool pendingHasTag(const String &tag) {
  const String normalized = rdmNormalizeTag(tag);
  for (const String &pendingTag : pendingTags) {
    if (pendingTag == normalized) {
      return true;
    }
  }
  return false;
}

void exitAddMode(const String &event, bool clearPending) {
  addModeActive = false;
  snapshot.addModeActive = false;
  snapshot.addModeRemainingMs = 0;
  if (clearPending) {
    pendingTags.clear();
  }
  snapshot.pendingTagCount = pendingTags.size();
  snapshot.lastEvent = event;
}

bool loadTagsFromWinbond() {
  authorizedTags.clear();
  doorUnlockEnabled = true;

  StoredRfidSettings stored;
  if (!storageReadBytes(RFID_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != RFID_SETTINGS_MAGIC || stored.version != RFID_SETTINGS_VERSION ||
      stored.size != sizeof(StoredRfidSettings) || stored.crc != settingsCrc(stored)) {
    snapshot.tagCount = 0;
    snapshot.doorUnlockEnabled = doorUnlockEnabled;
    return false;
  }

  doorUnlockEnabled = stored.doorUnlockEnabled;
  for (uint8_t i = 0; i < stored.tagCount && authorizedTags.size() < RDM_MAX_TAGS; i++) {
    const String tag = normalizeHex(String(stored.tags[i]));
    if (tag.length() == RDM_TAG_HEX_LENGTH && !rdmHasTag(tag)) {
      authorizedTags.push_back(tag);
    }
  }

  snapshot.tagCount = authorizedTags.size();
  snapshot.doorUnlockEnabled = doorUnlockEnabled;
  return true;
}

void loadLegacyTagsFromPreferences() {
  authorizedTags.clear();

  String stored = preferences.getString("tags", "");
  while (stored.length() > 0 && authorizedTags.size() < RDM_MAX_TAGS) {
    const int comma = stored.indexOf(',');
    String tag = comma >= 0 ? stored.substring(0, comma) : stored;
    tag = normalizeHex(tag);
    if (tag.length() == RDM_TAG_HEX_LENGTH && !rdmHasTag(tag)) {
      authorizedTags.push_back(tag);
    }
    if (comma < 0) {
      break;
    }
    stored = stored.substring(comma + 1);
  }

  snapshot.tagCount = authorizedTags.size();
  snapshot.doorUnlockEnabled = doorUnlockEnabled;
  if (!authorizedTags.empty()) {
    saveTags();
  }
}

bool parseFrame(const uint8_t *data, String &tag) {
  if (data[0] != 0x02 || data[RDM_FRAME_LENGTH - 1] != 0x03) {
    return false;
  }

  if (!isHexAscii(data + 1, RDM_TAG_HEX_LENGTH + RDM_CHECKSUM_HEX_LENGTH)) {
    return false;
  }

  uint8_t checksum = 0;
  for (uint8_t i = 0; i < RDM_TAG_HEX_LENGTH; i += 2) {
    checksum ^= parseHexByte(data + 1 + i);
  }

  const uint8_t expectedChecksum = parseHexByte(data + 1 + RDM_TAG_HEX_LENGTH);
  if (checksum != expectedChecksum) {
    return false;
  }

  tag = "";
  tag.reserve(RDM_TAG_HEX_LENGTH);
  for (uint8_t i = 0; i < RDM_TAG_HEX_LENGTH; i++) {
    tag += static_cast<char>(data[1 + i]);
  }
  tag = normalizeHex(tag);
  return tag.length() == RDM_TAG_HEX_LENGTH;
}

void addPendingTag(const String &tag) {
  if (rdmHasTag(tag) || pendingHasTag(tag)) {
    snapshot.lastAuthorized = rdmHasTag(tag);
    snapshot.lastEvent = "Tag already exists";
    extendAddModeTimeout();
    return;
  }

  if (authorizedTags.size() + pendingTags.size() >= RDM_MAX_TAGS) {
    snapshot.lastEvent = "Tag storage full";
    extendAddModeTimeout();
    return;
  }

  pendingTags.push_back(tag);
  snapshot.pendingTagCount = pendingTags.size();
  snapshot.lastAuthorized = false;
  snapshot.lastEvent = "Tag scanned";
  extendAddModeTimeout();
}

void triggerDoorUnlockFromRfid() {
  if (doorUnlockEnabled) {
    relayAutomationPulseDoorLock();
  }
}

void beepForScan() {
  buzzerBeep(RDM_SCAN_BEEP_HZ, RDM_SCAN_BEEP_MS);
}

void scheduleAuthorizedBeep() {
  authorizedBeepPending = true;
  authorizedBeepAt = millis() + RDM_AUTHORIZED_BEEP_DELAY_MS;
}

void serviceAuthorizedBeep() {
  if (!authorizedBeepPending || static_cast<int32_t>(millis() - authorizedBeepAt) < 0) {
    return;
  }

  authorizedBeepPending = false;
  buzzerBeep(RDM_AUTHORIZED_BEEP_HZ, RDM_AUTHORIZED_BEEP_MS);
}

void handleTag(const String &tag) {
  const uint32_t now = millis();
  if (tag == lastProcessedTag && now - lastProcessedMs < RDM_DUPLICATE_SUPPRESS_MS) {
    snapshot.tagPresent = true;
    return;
  }

  lastProcessedTag = tag;
  lastProcessedMs = now;
  snapshot.lastTag = tag;
  snapshot.lastReadMs = now;
  snapshot.totalReads++;
  snapshot.tagPresent = true;
  beepForScan();

  if (addModeActive) {
    addPendingTag(tag);
    return;
  }

  snapshot.lastAuthorized = rdmIsAuthorized(tag);
  if (snapshot.lastAuthorized) {
    if (doorUnlockEnabled) {
      triggerDoorUnlockFromRfid();
      scheduleAuthorizedBeep();
      snapshot.lastEvent = "Door unlocked";
    } else {
      snapshot.lastEvent = "RFID door unlock disabled";
    }
  } else {
    snapshot.lastEvent = "Unauthorized tag";
  }
}

void processByte(uint8_t value) {
  if (value == 0x02) {
    frameIndex = 0;
  }

  if (frameIndex >= RDM_FRAME_LENGTH) {
    frameIndex = 0;
  }

  frame[frameIndex++] = value;
  if (frameIndex != RDM_FRAME_LENGTH) {
    return;
  }

  String tag;
  if (parseFrame(frame, tag)) {
    handleTag(tag);
  } else {
    snapshot.lastEvent = "Invalid RFID frame";
  }

  frameIndex = 0;
}
}

void rdmBegin() {
  preferences.begin("rdm6300", false);
  snapshot.initialized = true;
  snapshot.lastEvent = "Waiting for RFID tag";
  if (!loadTagsFromWinbond()) {
    loadLegacyTagsFromPreferences();
  }

  rdmSerial.begin(RDM_BAUD_RATE, EspSoftwareSerial::SWSERIAL_8N1, PIN_RDM6300_RX, -1);
  rdmSerial.enableIntTx(false);

  Serial.print("RDM6300 RFID reader started on RX GPIO");
  Serial.println(PIN_RDM6300_RX);
}

void rdmLoop() {
  serviceAuthorizedBeep();

  if (addModeActive && addModeRemainingMs() == 0) {
    exitAddMode("Add mode timeout", true);
  }

  while (rdmSerial.available()) {
    processByte(static_cast<uint8_t>(rdmSerial.read()));
    yield();
  }

  if (snapshot.tagPresent && millis() - snapshot.lastReadMs > RDM_TAG_HOLD_MS) {
    snapshot.tagPresent = false;
  }
}

RdmSnapshot rdmGetSnapshot() {
  snapshot.tagCount = authorizedTags.size();
  snapshot.pendingTagCount = pendingTags.size();
  snapshot.addModeActive = addModeActive;
  snapshot.addModeRemainingMs = addModeRemainingMs();
  snapshot.doorUnlockEnabled = doorUnlockEnabled;
  return snapshot;
}

bool rdmAddTag(const String &tag) {
  const String normalized = rdmNormalizeTag(tag);
  if (normalized.length() != RDM_TAG_HEX_LENGTH || authorizedTags.size() >= RDM_MAX_TAGS || rdmHasTag(normalized)) {
    return false;
  }

  authorizedTags.push_back(normalized);
  if (!saveTags()) {
    authorizedTags.pop_back();
    return false;
  }
  snapshot.lastEvent = "RFID tag added";
  return true;
}

bool rdmDeleteTag(const String &tag) {
  const String normalized = rdmNormalizeTag(tag);
  for (auto it = authorizedTags.begin(); it != authorizedTags.end(); ++it) {
    if (*it == normalized) {
      const String removed = *it;
      authorizedTags.erase(it);
      if (!saveTags()) {
        authorizedTags.push_back(removed);
        return false;
      }
      snapshot.lastEvent = "RFID tag deleted";
      if (snapshot.lastTag == normalized) {
        snapshot.lastAuthorized = false;
      }
      return true;
    }
  }
  return false;
}

void rdmClearTags() {
  const std::vector<String> previousTags = authorizedTags;
  authorizedTags.clear();
  if (!saveTags()) {
    authorizedTags = previousTags;
    return;
  }
  snapshot.lastAuthorized = false;
  snapshot.lastEvent = "All RFID tags cleared";
}

void rdmEnterAddMode() {
  pendingTags.clear();
  addModeActive = true;
  snapshot.addModeActive = true;
  snapshot.pendingTagCount = 0;
  snapshot.lastAuthorized = false;
  snapshot.lastEvent = "Add mode active: scan tags";
  extendAddModeTimeout();
}

void rdmCancelAddMode() {
  exitAddMode("Ready", true);
}

bool rdmSavePendingTags() {
  bool savedAny = false;

  for (const String &tag : pendingTags) {
    if (tag.length() == RDM_TAG_HEX_LENGTH && !rdmHasTag(tag) && authorizedTags.size() < RDM_MAX_TAGS) {
      authorizedTags.push_back(tag);
      savedAny = true;
    }
  }

  pendingTags.clear();
  if (!saveTags()) {
    return false;
  }
  exitAddMode("Tags saved", false);
  return true;
}

bool rdmIsAddModeActive() {
  return addModeActive;
}

bool rdmSetDoorUnlockEnabled(bool enabled) {
  doorUnlockEnabled = enabled;
  if (!saveTags()) {
    doorUnlockEnabled = !enabled;
    snapshot.doorUnlockEnabled = doorUnlockEnabled;
    return false;
  }

  snapshot.lastEvent = doorUnlockEnabled ? "RFID door unlock enabled" : "RFID door unlock disabled";
  return true;
}

bool rdmGetDoorUnlockEnabled() {
  return doorUnlockEnabled;
}

bool rdmIsAuthorized(const String &tag) {
  return rdmHasTag(tag);
}

bool rdmHasTag(const String &tag) {
  const String normalized = rdmNormalizeTag(tag);
  for (const String &authorizedTag : authorizedTags) {
    if (authorizedTag == normalized) {
      return true;
    }
  }
  return false;
}

std::vector<String> rdmGetTags() {
  return authorizedTags;
}

std::vector<String> rdmGetPendingTags() {
  return pendingTags;
}

String rdmNormalizeTag(const String &tag) {
  return normalizeHex(tag);
}
