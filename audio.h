#ifndef AUDIO_H_
#define AUDIO_H_

namespace audio {

struct System;
System *startup();
void shutdown(System *system);

} // namespace audio

#endif
