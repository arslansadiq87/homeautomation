#include <Arduino.h>

#include "mp3.h"
#include "pinout.h"

namespace {
constexpr uint32_t MP3_BAUD_RATE = 9600;
constexpr uint8_t MP3_DEFAULT_FOLDER = 1;
constexpr uint8_t MP3_FIRST_FILE = 1;
constexpr uint8_t MP3_TOTAL_FILES = 255;
constexpr uint8_t MP3_DEFAULT_VOLUME = 25;
constexpr uint8_t MP3_MAX_VOLUME = 30;
constexpr uint16_t MP3_COMMAND_GAP_MS = 80;

constexpr uint8_t MP3_CMD_SET_VOLUME = 0x06;
constexpr uint8_t MP3_CMD_PLAY_FOLDER_FILE = 0x0F;
constexpr uint8_t MP3_CMD_PAUSE = 0x0E;
constexpr uint8_t MP3_CMD_STOP = 0x16;

HardwareSerial mp3Serial(1);
Mp3Snapshot snapshot;
struct QueuedMp3Command {
  bool active = false;
  uint8_t command = 0;
  uint16_t parameter = 0;
};
QueuedMp3Command commandQueue[8];
uint8_t commandHead = 0;
uint8_t commandTail = 0;

bool queueFull() {
  return static_cast<uint8_t>((commandTail + 1) % 8) == commandHead;
}

bool queueEmpty() {
  return commandHead == commandTail;
}

bool queueCommand(uint8_t command, uint16_t parameter = 0) {
  if (!snapshot.initialized || queueFull()) {
    return false;
  }

  commandQueue[commandTail].active = true;
  commandQueue[commandTail].command = command;
  commandQueue[commandTail].parameter = parameter;
  commandTail = (commandTail + 1) % 8;
  return true;
}

bool writeCommandNow(uint8_t command, uint16_t parameter = 0) {

  const uint8_t parameterHigh = static_cast<uint8_t>(parameter >> 8);
  const uint8_t parameterLow = static_cast<uint8_t>(parameter & 0xFF);
  const uint16_t checksum = static_cast<uint16_t>(
    0 - (0xFF + 0x06 + command + 0x00 + parameterHigh + parameterLow)
  );

  const uint8_t packet[] = {
    0x7E,
    0xFF,
    0x06,
    command,
    0x00,
    parameterHigh,
    parameterLow,
    static_cast<uint8_t>(checksum >> 8),
    static_cast<uint8_t>(checksum & 0xFF),
    0xEF,
  };

  const size_t written = mp3Serial.write(packet, sizeof(packet));
  snapshot.lastCommandMs = millis();
  return written == sizeof(packet);
}

void serviceCommandQueue() {
  if (queueEmpty()) {
    return;
  }

  const uint32_t now = millis();
  if (snapshot.lastCommandMs != 0 && now - snapshot.lastCommandMs < MP3_COMMAND_GAP_MS) {
    return;
  }

  const QueuedMp3Command queued = commandQueue[commandHead];
  commandQueue[commandHead].active = false;
  commandHead = (commandHead + 1) % 8;
  if (queued.active) {
    writeCommandNow(queued.command, queued.parameter);
  }
}

bool sendCommand(uint8_t command, uint16_t parameter = 0) {
  return queueCommand(command, parameter);
}

bool sendPlaySequence(uint8_t folder, uint8_t file, uint8_t volume, bool force) {
  if (force && snapshot.playing) {
    sendCommand(MP3_CMD_STOP);
    snapshot.playing = false;
  }

  if (!mp3SetVolume(volume)) {
    return false;
  }

  return sendCommand(MP3_CMD_PLAY_FOLDER_FILE, (static_cast<uint16_t>(folder) << 8) | file);
}

}

void mp3Begin() {
  snapshot.folder = MP3_DEFAULT_FOLDER;
  snapshot.file = 0;
  snapshot.totalFiles = MP3_TOTAL_FILES;
  snapshot.volume = MP3_DEFAULT_VOLUME;
  snapshot.playing = false;

  mp3Serial.begin(MP3_BAUD_RATE, SERIAL_8N1, PIN_MP3_RX, PIN_MP3_TX);
  snapshot.initialized = true;
  mp3SetVolume(snapshot.volume);

  Serial.print("YX5300 MP3 player initialized on RX ");
  Serial.print(PIN_MP3_RX);
  Serial.print(", TX ");
  Serial.println(PIN_MP3_TX);
}

void mp3Loop() {
  serviceCommandQueue();
}

Mp3Snapshot mp3GetSnapshot() {
  return snapshot;
}

bool mp3Play() {
  return mp3PlayFile(MP3_DEFAULT_FOLDER, MP3_FIRST_FILE);
}

bool mp3PlayFile(uint8_t folder, uint8_t file) {
  if (folder == 0 || file == 0 || file > MP3_TOTAL_FILES) {
    return false;
  }

  const bool ok = sendPlaySequence(folder, file, snapshot.volume, true);
  if (ok) {
    snapshot.folder = folder;
    snapshot.file = file;
    snapshot.playing = true;
  }
  return ok;
}

bool mp3Next() {
  const uint8_t currentFile = snapshot.file == 0 ? MP3_FIRST_FILE : snapshot.file;
  const uint8_t nextFile = currentFile >= MP3_TOTAL_FILES ? MP3_FIRST_FILE : currentFile + 1;
  return mp3PlayFile(snapshot.folder, nextFile);
}

bool mp3Previous() {
  const uint8_t currentFile = snapshot.file == 0 ? MP3_FIRST_FILE : snapshot.file;
  const uint8_t previousFile = currentFile <= MP3_FIRST_FILE ? MP3_TOTAL_FILES : currentFile - 1;
  return mp3PlayFile(snapshot.folder, previousFile);
}

bool mp3Pause() {
  const bool ok = sendCommand(MP3_CMD_PAUSE);
  if (ok) {
    snapshot.playing = false;
  }
  return ok;
}

bool mp3Stop() {
  const bool ok = sendCommand(MP3_CMD_STOP);
  if (ok) {
    snapshot.playing = false;
  }
  return ok;
}

bool mp3SetVolume(uint8_t volume) {
  const uint8_t boundedVolume = constrain(volume, 0, MP3_MAX_VOLUME);
  const bool ok = sendCommand(MP3_CMD_SET_VOLUME, boundedVolume);
  if (ok) {
    snapshot.volume = boundedVolume;
  }
  return ok;
}
