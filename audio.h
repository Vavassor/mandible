#ifndef AUDIO_H_
#define AUDIO_H_

namespace audio {

struct System;
System *startup();
void shutdown(System *system);
void play_once(System *system, const char *filename);

} // namespace audio

#endif
