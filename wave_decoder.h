#ifndef WAVE_DECODER_H_
#define WAVE_DECODER_H_

struct WaveDecoder;

WaveDecoder *wave_open_file(const char *filename);
void wave_close_file(WaveDecoder *decoder);
int wave_decode_interleaved(WaveDecoder *decoder, int out_channels,
							float *buffer, int sample_count);
void wave_seek_start(WaveDecoder *decoder);
int wave_channels(WaveDecoder *decoder);

#endif
