#ifndef RDM_H
#define RDM_H

#include <Arduino.h>
#include <vector>

struct RdmSnapshot {
  bool initialized = false;
  bool tagPresent = false;
  bool lastAuthorized = false;
  bool addModeActive = false;
  bool doorUnlockEnabled = true;
  String lastTag;
  String lastEvent = "Waiting for RFID tag";
  uint8_t tagCount = 0;
  uint8_t pendingTagCount = 0;
  uint32_t totalReads = 0;
  uint32_t lastReadMs = 0;
  uint32_t addModeRemainingMs = 0;
};

void rdmBegin();
void rdmLoop();
RdmSnapshot rdmGetSnapshot();

bool rdmAddTag(const String &tag);
bool rdmDeleteTag(const String &tag);
void rdmClearTags();
void rdmEnterAddMode();
void rdmCancelAddMode();
bool rdmSavePendingTags();
bool rdmIsAddModeActive();
bool rdmSetDoorUnlockEnabled(bool enabled);
bool rdmGetDoorUnlockEnabled();
bool rdmIsAuthorized(const String &tag);
bool rdmHasTag(const String &tag);
std::vector<String> rdmGetTags();
std::vector<String> rdmGetPendingTags();
String rdmNormalizeTag(const String &tag);

#endif
