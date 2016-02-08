#include "wave_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#if defined(__GNUC__)
#define ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

#define RIFF_TAG        0x46464952 // the string "RIFF"
#define WAVE_DESCRIPTOR 0x45564157 // the string "WAVE"
#define FMT_TAG         0x20746D66 // the string "fmt "
#define FACT_TAG        0x74636166 // the string "fact"
#define DATA_TAG        0x61746164 // the string "data"

static u32 pad_chunk_size(u32 x) {
    // Chunks are aligned to even-numbered offsets, so the chunk actually
    // contains the specified number of bytes plus 0 or 1 padding byte.
    // So, round up the byte count to the nearest even number so the correct
    // number of bytes gets processed.
    return (x + 1) & ~1;
}

typedef enum {
    FORMAT_NONE,
    FORMAT_INTEGER,
    FORMAT_IEEE754_FLOAT, // Institute for Electrical and Electronic Engineers' Standard 754 for Floating-Point Arithmetic
    FORMAT_MS_ADPCM, // Microsoft Adaptive Differential Pulse-Code Modulation
} Format;

typedef struct {
    s16 coefficients[256][2];
    u16 num_coefficients;
    u16 frames_per_block;
} MsAdpcmData;

struct WaveDecoder {
    MsAdpcmData ms_adpcm_data;
    struct {
        void *buffer;
        u32 buffer_size;
        u32 frames;
        u32 start;
    } decoded;
    struct {
        void *block;
        u32 block_size;
    } encoded;

    s64 data_chunk_position; // where the data chunk is in the file, as an offset in bytes;
    FILE *file;
    bool end_of_file;
    u32 frame_count;
    u32 frames_left;

    Format format;
    u32 sample_rate;
    u16 block_alignment;
    u8 bits_per_sample;
    u8 channels;
};

static ALWAYS_INLINE u8 extract8(WaveDecoder *decoder) {
   int byte = fgetc(decoder->file);
   if (byte == EOF) {
       decoder->end_of_file = true;
       return 0;
   }
   return byte;
}

static u16 extract16(WaveDecoder *decoder) {
    u16 x;
    x = extract8(decoder);
    x += (u16) extract8(decoder) << 8;
    return x;
}

static u32 extract32(WaveDecoder *decoder) {
    u32 x;
    x = extract8(decoder);
    x += (u32) extract8(decoder) << 8;
    x += (u32) extract8(decoder) << 16;
    x += (u32) extract8(decoder) << 24;
    return x;
}

static size_t extract_bytes(WaveDecoder *decoder, u8 *data, size_t bytes) {
    size_t bytes_read = fread(data, 1, bytes, decoder->file);
    if (bytes_read != bytes) {
        decoder->end_of_file = true;
    }
    return bytes_read;
}

static void skip_bytes(WaveDecoder *decoder, size_t bytes) {
    fseek(decoder->file, bytes, SEEK_CUR);
}

static size_t get_ms_adpcm_specific(WaveDecoder *decoder) {
    size_t bytes_read;

    u16 frames_per_block = extract16(decoder);
    /* assert(frames_per_block == (block_alignment - 7 * num_channels) * 8 /
             (bits_per_sample * num_channels) + 2); */
    decoder->ms_adpcm_data.frames_per_block = frames_per_block;

    u16 num_coefficients = extract16(decoder);
    assert(num_coefficients < 256);
    decoder->ms_adpcm_data.num_coefficients = num_coefficients;
    u16 i;
    for (i = 0; i < num_coefficients; ++i) {
        decoder->ms_adpcm_data.coefficients[i][0] = (s16) extract16(decoder);
        decoder->ms_adpcm_data.coefficients[i][1] = (s16) extract16(decoder);
    }

    bytes_read = 2 * sizeof(u16);
    bytes_read += 2 * sizeof(s16) * num_coefficients;

    return bytes_read;
}

#define WAVE_FORMAT_PCM        0x0001
#define WAVE_FORMAT_ADPCM      0x0002
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

typedef struct {
    u32 data1;
    u16 data2;
    u16 data3;
    u8 data4[8];
} GUID;

#define DEFINE_WAVE_FORMAT_GUID(name, type) \
    static const GUID name = \
        { (u32) type, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } }

DEFINE_WAVE_FORMAT_GUID(STATIC_KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_PCM);
#define KSDATAFORMAT_SUBTYPE_PCM STATIC_KSDATAFORMAT_SUBTYPE_PCM

DEFINE_WAVE_FORMAT_GUID(STATIC_KSDATAFORMAT_SUBTYPE_ADPCM, WAVE_FORMAT_ADPCM);
#define KSDATAFORMAT_SUBTYPE_ADPCM STATIC_KSDATAFORMAT_SUBTYPE_ADPCM

DEFINE_WAVE_FORMAT_GUID(STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_IEEE_FLOAT);
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT STATIC_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT

#define GUID_EQUALS(a, b) (!memcmp(&(a), &(b), sizeof(GUID)))

static bool read_format_chunk(WaveDecoder *decoder, u32 chunk_size) {
    u16 format_type = extract16(decoder);
    u16 num_channels = extract16(decoder);
    u32 sample_rate = extract32(decoder);
    u32 average_bytes_per_second = extract32(decoder);
    u16 block_alignment = extract16(decoder);
    u16 bits_per_sample = extract16(decoder);

    decoder->channels = num_channels;
    decoder->sample_rate = sample_rate;
    decoder->bits_per_sample = bits_per_sample;
    decoder->block_alignment = block_alignment;

    switch (format_type) {
        case WAVE_FORMAT_PCM: {
            decoder->format = FORMAT_INTEGER;

            // Format extension is optional, but if it IS there it needs to
            // be skipped in order to process the next chunk.
            if (chunk_size >= 18) {
                u16 extension_size = extract16(decoder);
                if (extension_size > 0) {
                    skip_bytes(decoder, extension_size);
                }
            }
        } break;

        case WAVE_FORMAT_ADPCM: {
            decoder->format = FORMAT_MS_ADPCM;

            u16 extension_size = extract16(decoder);
            size_t bytes_read = get_ms_adpcm_specific(decoder);
            if (extension_size > bytes_read) {
                skip_bytes(decoder, extension_size - bytes_read);
            }
        } break;

        case WAVE_FORMAT_IEEE_FLOAT: {
            decoder->format = FORMAT_IEEE754_FLOAT;

            // Pull out the required extension size and skip if there is an
            // extension.

            u16 extension_size = extract16(decoder);
            if (extension_size > 0) {
                skip_bytes(decoder, extension_size);
            }
        } break;

        case WAVE_FORMAT_EXTENSIBLE: {
            u16 extension_size = extract16(decoder);

            u16 valid_bits_per_sample = extract16(decoder);
            u32 channel_mask = extract32(decoder);

            GUID guid = {0};
            guid.data1 = extract32(decoder);
            guid.data2 = extract16(decoder);
            guid.data3 = extract16(decoder);
            extract_bytes(decoder, guid.data4, 8);

            if (GUID_EQUALS(guid, KSDATAFORMAT_SUBTYPE_PCM)) {
                decoder->format = FORMAT_INTEGER;

            } else if (GUID_EQUALS(guid, KSDATAFORMAT_SUBTYPE_ADPCM)) {
                decoder->format = FORMAT_MS_ADPCM;
                size_t bytes_read = get_ms_adpcm_specific(decoder);
                extension_size -= 22 + bytes_read;

            } else if (GUID_EQUALS(guid, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                decoder->format = FORMAT_IEEE754_FLOAT;

            } else {
                // The wave format extension sub-type is not recognised.
                return false;
            }

            if (extension_size > 22) {
                skip_bytes(decoder, 22);
            }
        } break;

        default: {
            // The wave format is not recognised.
            return false;
        } break;
    }

    return true;
}

static bool ready_for_data_chunk(WaveDecoder *decoder, u32 chunk_size) {
    // The data chunk has been found. But, since the format and fact chunks are
    // supposed to be before the data chunk, there needs to be validation here
    // that those chunks have already been processed. So, check that the
    // specification has been filled out.
    if (decoder->format == FORMAT_NONE) {
        return false;
    }

    // Since the specification doesn't necessarily require WAVE_FORMAT_PCM
    // files to have a fact chunk, the frame count may not be set, so ensure
    // that it is.
    if (decoder->format == FORMAT_INTEGER &&
        decoder->frame_count == 0) {
        decoder->frame_count = chunk_size / decoder->block_alignment;
    }

    decoder->data_chunk_position = ftell(decoder->file);
    decoder->frames_left = decoder->frame_count;

    if (decoder->format == FORMAT_MS_ADPCM) {
        decoder->encoded.block_size = decoder->block_alignment;
        decoder->decoded.buffer_size = sizeof(s16) * decoder->channels *
                                       decoder->ms_adpcm_data.frames_per_block;
    } else {
        int frame_count = 2048; // arbitrary block size
        int sample_count = frame_count * decoder->channels;
        int block_size = (decoder->bits_per_sample / 8) * sample_count;
        decoder->encoded.block_size = block_size;
        decoder->decoded.buffer_size = block_size;
    }

    decoder->encoded.block = malloc(decoder->encoded.block_size);
    decoder->decoded.buffer = malloc(decoder->decoded.buffer_size);

    decoder->decoded.frames = 0;
    decoder->decoded.start = 0;

    return true;
}

static bool determine_format_and_ready(WaveDecoder *decoder) {
    // Loop through all the RIFF chunks in the file until a waveform chunk is
    // found.
    while (!decoder->end_of_file) {
        u32 riff_tag = extract32(decoder);
        u32 riff_chunk_size = extract32(decoder);
        if (riff_tag == RIFF_TAG) {
            u32 riff_descriptor = extract32(decoder);
            if (riff_descriptor == WAVE_DESCRIPTOR) {
                // This is a waveform chunk, so process each of the
                // sub-chunks inside it until a "data" sub-chunk is found.

                u32 riff_chunk_position = sizeof(riff_descriptor);
                while (riff_chunk_position < riff_chunk_size) {
                    u32 tag = extract32(decoder);
                    u32 chunk_size = pad_chunk_size(extract32(decoder));
                    riff_chunk_position += sizeof(tag) + sizeof(chunk_size) +
                                           chunk_size;
                    switch (tag) {
                        case FMT_TAG: {
                            if (!read_format_chunk(decoder, chunk_size)) {
                                return false;
                            }
                        } break;

                        case FACT_TAG: {
                            decoder->frame_count = extract32(decoder);
                        } break;

                        case DATA_TAG:
                            return ready_for_data_chunk(decoder, chunk_size);

                        default: {
                            // Skip all unused WAVE sub-chunks.
                            skip_bytes(decoder, chunk_size);
                        } break;
                    }
                }
            } else {
                // Since the descriptor indicates this isn't a WAVE chunk, skip
                // this RIFF chunk and keep looking.

                u32 padded_size = pad_chunk_size(riff_chunk_size) -
                                  sizeof riff_descriptor;
                skip_bytes(decoder, padded_size);
            }
        } else {
            // Since the RIFF chunk tag isn't used or recognized, skip over
            // this chunk.

            u32 padded_size = pad_chunk_size(riff_chunk_size);
            skip_bytes(decoder, padded_size);
        }
    }

    return false;
}

typedef struct {
    u16 delta;
    s16 sample1;
    s16 sample2;
    u8 predictor;
} MsAdpcmState;

static s16 ms_adpcm_decode_sample(MsAdpcmState *state, u8 code,
                                  const s16 *coefficient_set) {
    const s32 MAX_S16 = 32767;
    const s32 MIN_S16 = -32768;
    const s32 adaption_table[16] = {
        230, 230, 230, 230, 307, 409, 512, 614,
        768, 614, 512, 409, 307, 230, 230, 230
    };

    s32 predicted_sample;
    predicted_sample = (state->sample1 * coefficient_set[0] +
                        state->sample2 * coefficient_set[1]) / 256;
    if (code & 0x08) {
        predicted_sample += state->delta * (code - 0x10);
    } else {
        predicted_sample += state->delta * code;
    }
    if (predicted_sample < MIN_S16) {
        predicted_sample = MIN_S16;
    } else if (predicted_sample > MAX_S16) {
        predicted_sample = MAX_S16;
    }

    s32 delta;
    delta = ((s32) state->delta * adaption_table[code]) / 256;
    if (delta < 16) {
        delta = 16;
    }

    state->delta = delta;
    state->sample2 = state->sample1;
    state->sample1 = predicted_sample;

    return (s16) predicted_sample;
}

static u32 ms_adpcm_decode_block(MsAdpcmData *adpcm_data, int channels,
                                 u8 *encoded, s16 *decoded) {
    u32 output_length = adpcm_data->frames_per_block * sizeof(s16) *
                        channels;

    MsAdpcmState *state[2];
    MsAdpcmState decoder_state[2];
    state[0] = &decoder_state[0];
    if (channels == 2) {
        state[1] = &decoder_state[1];
    } else {
        state[1] = &decoder_state[0];
    }

    int i;
    for (i = 0; i < channels; ++i) {
        state[i]->predictor = *encoded++;
        assert(state[i]->predictor < adpcm_data->num_coefficients);
    }

    for (i = 0; i < channels; ++i) {
        state[i]->delta = (encoded[1] << 8) | encoded[0];
        encoded += sizeof(u16);
    }

    for (i = 0; i < channels; ++i) {
        state[i]->sample1 = (encoded[1] << 8) | encoded[0];
        encoded += sizeof(s16);
    }
    for (i = 0; i < channels; ++i) {
        state[i]->sample2 = (encoded[1] << 8) | encoded[0];
        encoded += sizeof(s16);
    }

    s16 *coefficient[2];
    coefficient[0] = adpcm_data->coefficients[state[0]->predictor];
    coefficient[1] = adpcm_data->coefficients[state[1]->predictor];

    for (i = 0; i < channels; ++i) {
        *decoded++ = state[i]->sample2;
    }
    for (i = 0; i < channels; ++i) {
        *decoded++ = state[i]->sample1;
    }

    int samples_remaining = (adpcm_data->frames_per_block - 2) * channels;
    while (samples_remaining > 0) {
        u8 code;
        code = *encoded >> 4;
        *decoded++ = ms_adpcm_decode_sample(state[0], code, coefficient[0]);
        code = *encoded & 0x0F;
        *decoded++ = ms_adpcm_decode_sample(state[1], code, coefficient[1]);

        ++encoded;
        samples_remaining -= 2;
    }

    return output_length;
}

static ALWAYS_INLINE u8 pull_u8(const void *buffer) {
    return ((const u8 *) buffer)[0];
}

static ALWAYS_INLINE u16 pull_u16(const void *buffer) {
    const u8 *b = (const u8 *) buffer;
    u16 x;
    x = b[0];
    x += (u16) b[1] << 8;
    return x;
}

static ALWAYS_INLINE u32 pull_u32(const void *buffer) {
    const u8 *b = (const u8 *) buffer;
    u32 x;
    x = b[0];
    x += (u32) b[1] << 8;
    x += (u32) b[2] << 16;
    x += (u32) b[3] << 24;
    return x;
}

static ALWAYS_INLINE float pull_float(const void *buffer) {
    const u8 *b = (const u8 *) buffer;
    u32 x;
    x = b[0];
    x += (u32) b[1] << 8;
    x += (u32) b[2] << 16;
    x += (u32) b[3] << 24;
    return *((float *) &x);
}

static ALWAYS_INLINE double pull_double(const void *buffer) {
    const u8 *b = (const u8 *) buffer;
    u64 x;
    x = b[0];
    x += (u64) b[1] << 8;
    x += (u64) b[2] << 16;
    x += (u64) b[3] << 24;
    x += (u64) b[4] << 32;
    x += (u64) b[5] << 40;
    x += (u64) b[6] << 48;
    x += (u64) b[7] << 56;
    return *((double *) &x);
}

static size_t round_up(size_t x, size_t multiple) {
    return x + multiple - 1 - (x - 1) % multiple;
}

static void fetch_and_decode_block(WaveDecoder *decoder) {
    // Fill the internal buffer with sample data bytes from file.

    size_t bytes_requested;
    bytes_requested = decoder->encoded.block_size;
    size_t data_bytes_left;
    if (decoder->format == FORMAT_MS_ADPCM) {
        data_bytes_left = (decoder->frames_left * decoder->channels) / 2;
        data_bytes_left = round_up(data_bytes_left,
                                   decoder->ms_adpcm_data.frames_per_block);
    } else {
        data_bytes_left = decoder->frames_left * decoder->block_alignment;
    }
    if (bytes_requested > data_bytes_left) {
        bytes_requested = data_bytes_left;
    }

    if (bytes_requested == 0) {
        return;
    }

    size_t bytes_got = extract_bytes(decoder, decoder->encoded.block,
                                     bytes_requested);

    // The bytes that are fetched from file are in little-endian order,
    // so for linear PCM formats, this is used to convert to native-endian
    // order in-place inside the encoded buffer.
#define STRAIGHTEN(type)                                         \
    const type *encoded = (const type *) decoder->encoded.block; \
    type *decoded = (type *) decoder->decoded.buffer;            \
    int i;                                                       \
    for (i = 0; i < samples; ++i) {                              \
        decoded[i] = pull_##type(encoded + i);                   \
    }

    u32 decoded_frames = 0;

    switch (decoder->format) {
        case FORMAT_INTEGER: {
            int samples = bytes_got / (decoder->bits_per_sample / 8);
            switch (decoder->bits_per_sample) {
                case 8:  { STRAIGHTEN(u8);  } break;
                case 16: { STRAIGHTEN(u16); } break;
                case 32: { STRAIGHTEN(u32); } break;
            }
            decoded_frames = bytes_got / decoder->block_alignment;
        } break;

        case FORMAT_IEEE754_FLOAT: {
            int samples = bytes_got / (decoder->bits_per_sample / 8);
            switch (decoder->bits_per_sample) {
                case 32: { STRAIGHTEN(float);  } break;
                case 64: { STRAIGHTEN(double); } break;
            }
            decoded_frames = bytes_got / decoder->block_alignment;
        } break;

        case FORMAT_MS_ADPCM: {
            ms_adpcm_decode_block(&decoder->ms_adpcm_data, decoder->channels,
                                  decoder->encoded.block,
                                  decoder->decoded.buffer);
            decoded_frames = decoder->ms_adpcm_data.frames_per_block;
            if (decoded_frames > decoder->frames_left) {
                decoded_frames = decoder->frames_left;
            }
        } break;

        default: {
            assert(false);
        } break;
    }

#undef STRAIGHTEN

    decoder->decoded.start = 0;
    decoder->decoded.frames = decoded_frames;
    decoder->frames_left -= decoded_frames;
}

static ALWAYS_INLINE float format_u8(u8 value) {
    const float scale = 1.0f / 255.0f;
    return (float) value * scale;
}

static ALWAYS_INLINE float format_s16(s16 value) {
    const float scale = (float) (1.0 / 32767.5);
    return ((float) value + 0.5f) * scale;
}

static ALWAYS_INLINE float format_s32(s32 value) {
    const float scale = (float) (1.0 / 2147483647.5);
    return ((float) value + 0.5f) * scale;
}

static ALWAYS_INLINE float format_float(float value) {
    return value;
}

static ALWAYS_INLINE float format_double(double value) {
    return (float) value;
}

int wave_decode_interleaved(WaveDecoder *decoder, int out_channels,
                            float *buffer, int sample_count) {
    int frames_decoded = 0;

    int channels = decoder->channels;
    if (channels > out_channels) {
        channels = out_channels;
    }
    int frame_count = sample_count / out_channels;
    int frames_remaining = frame_count;

#define FORMAT_FRAMES(type, frames)                                     \
    const type *decoded_buffer = (const type *)decoder->decoded.buffer; \
    int i;                                                              \
    for (i = 0; i < (frames); ++i) {                                    \
        int x = decoder->channels * (decoder->decoded.start + i);       \
        int j;                                                          \
        for (j = 0; j < channels; ++j) {                                \
            *buffer++ = format_##type(decoded_buffer[x + j]);           \
        }                                                               \
        for (; j < out_channels; ++j) {                                 \
            *buffer++ = 0;                                              \
        }                                                               \
    }

    while (frames_remaining > 0) {
        if (decoder->frames_left == 0) {
            // The data chunk of the file has no more samples to
            // decode.
            break;
        }
        if (decoder->decoded.frames == 0) {
            // If the intermediate buffer for encoded bytes is empty,
            // fetch another whole buffer-full from the file.
            fetch_and_decode_block(decoder);
        }

        int frames = frames_remaining;
        if (decoder->decoded.frames < frames_remaining) {
            frames = decoder->decoded.frames;
        }
        switch (decoder->format) {
            case FORMAT_INTEGER: {
                switch (decoder->bits_per_sample) {
                    case 8: { FORMAT_FRAMES(u8, frames); } break;
                    case 16: { FORMAT_FRAMES(s16, frames); } break;
                    case 32: { FORMAT_FRAMES(s32, frames); } break;
                }
            } break;

            case FORMAT_IEEE754_FLOAT: {
                switch (decoder->bits_per_sample) {
                    case 32: { FORMAT_FRAMES(float, frames); } break;
                    case 64: { FORMAT_FRAMES(double, frames); } break;
                }
            } break;

            case FORMAT_MS_ADPCM: {
                FORMAT_FRAMES(s16, frames);
            } break;
        }

        decoder->decoded.frames -= frames;
        decoder->decoded.start += frames;
        frames_remaining -= frames;
        frames_decoded += frames;
    }

#undef FORMAT_FRAMES

    return frames_decoded;
}

void wave_seek_start(WaveDecoder *decoder) {
    decoder->decoded.frames = 0;
    decoder->decoded.start = 0;
    fseek(decoder->file, decoder->data_chunk_position, SEEK_SET);
    decoder->frames_left = decoder->frame_count;
}

WaveDecoder *wave_open_file(const char *filename) {
    WaveDecoder *decoder;

    decoder = calloc(1, sizeof(WaveDecoder));
    if (!decoder) {
        // Failed to allocate the memory needed to decode the wave file.
        return NULL;
    }

    FILE *file;
    file = fopen(filename, "rb");
    if (!file) {
        // The file was not able to be opened.
        free(decoder);
        return NULL;
    }
    decoder->file = file;

    if (!determine_format_and_ready(decoder)) {
        // File was not of a supported format or was corrupted in some way that
        // would prevent it from being decoded properly.
        fclose(decoder->file);
        free(decoder);
        return NULL;
    }

    return decoder;
}

void wave_close_file(WaveDecoder *decoder) {
    if (decoder) {
        if (decoder->file) {
            fclose(decoder->file);
        }
        if (decoder->encoded.block) {
            free(decoder->encoded.block);
        }
        if (decoder->decoded.buffer) {
            free(decoder->decoded.buffer);
        }
        free(decoder);
    }
}
