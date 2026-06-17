#ifndef MP3_H
#define MP3_H

#include <Arduino.h>

struct Mp3Snapshot {
  bool initialized = false;
  bool playing = false;
  uint8_t folder = 1;
  uint8_t file = 0;
  uint8_t totalFiles = 255;
  uint8_t volume = 25;
  uint32_t lastCommandMs = 0;
};

void mp3Begin();
void mp3Loop();
Mp3Snapshot mp3GetSnapshot();

bool mp3Play();
bool mp3PlayFile(uint8_t folder, uint8_t file);
bool mp3Next();
bool mp3Previous();
bool mp3Pause();
bool mp3Stop();
bool mp3SetVolume(uint8_t volume);

#endif
