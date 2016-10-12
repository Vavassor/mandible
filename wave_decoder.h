#pragma once

struct WaveDecoder;

enum class WaveOpenError {
    None,
    Out_Of_Memory,
    Format_Chunk_Unread,
};

struct WaveMemory {
    void* block;
    int block_size;
    int position;
};

struct Stack;

WaveDecoder* wave_open_file(const char* filename, WaveOpenError* error,
                            WaveMemory* memory, Stack* stack);
void wave_close_file(WaveDecoder* decoder);
int wave_decode_interleaved(WaveDecoder* decoder, int out_channels,
							float* buffer, int sample_count);
void wave_seek_start(WaveDecoder* decoder);
int wave_channels(WaveDecoder* decoder);
