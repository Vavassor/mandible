#ifndef AUDIO_H_
#define AUDIO_H_

namespace audio {

typedef unsigned int StreamId;

struct System;
System *startup();
void shutdown(System *system);
void play_once(System *system, const char *filename, float volume);

void start_stream(System *system, const char *filename, float volume, StreamId *stream_id);
void stop_stream(System *system, StreamId stream_id);

} // namespace audio

#endif
