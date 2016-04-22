#pragma once

namespace audio {

typedef unsigned int StreamId;

bool startup();
void shutdown();
void play_once(const char* filename, float volume);

void start_stream(const char* filename, float volume, StreamId* stream_id);
void stop_stream(StreamId stream_id);

} // namespace audio
