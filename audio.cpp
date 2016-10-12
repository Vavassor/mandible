#include "audio.h"

#include "sized_types.h"
#include "atomic.h"
#include "logging.h"
#include "string_utilities.h"
#include "array_macros.h"
#include "memory.h"
#include "wave_decoder.h"
#include "profile.h"
#include "assert.h"
#include "asset_handling.h"

#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

#include <alsa/asoundlib.h>

#include <pthread.h>

namespace audio {

// Formatting Functions........................................................

enum Format {
    FORMAT_U8,  // Unsigned 8-bit Integer
    FORMAT_S8,  // Signed 8-bit Integer
    FORMAT_U16, // Unsigned 16-bit Integer
    FORMAT_S16, // Signed 16-bit Integer
    FORMAT_U24, // Unsigned 24-bit Integer
    FORMAT_S24, // Signed 24-bit Integer
    FORMAT_U32, // Unsigned 32-bit Integer
    FORMAT_S32, // Signed 32-bit Integer
    FORMAT_F32, // 32-bit Floating-point
    FORMAT_F64, // 64-bit Floating-point
};

static int format_byte_count(Format format) {
    switch (format) {
        case FORMAT_U8:
        case FORMAT_S8:
            return 1;

        case FORMAT_U16:
        case FORMAT_S16:
            return 2;

        case FORMAT_U24:
        case FORMAT_S24:
            return 3;

        case FORMAT_U32:
        case FORMAT_S32:
        case FORMAT_F32:
            return 4;

        case FORMAT_F64:
            return 8;
    }
    return 0;
}

struct Int24 {
    s32 x;
};
typedef Int24 s24; // A 32-bit value pretending to be 24-bits

inline s8 convert_to_s8(float value) {
    return value * 127.5f - 0.5f;
}

inline s16 convert_to_s16(float value) {
    return value * 32767.5f - 0.5f;
}

inline s24 convert_to_s24(float value) {
    return { static_cast<s32>(value * 8388607.5f - 0.5f) };
}

inline s32 convert_to_s32(float value) {
    return value * 2147483647.5f - 0.5f;
}

inline float convert_to_float(float value) {
    return value;
}

inline double convert_to_double(float value) {
    return value;
}

struct ConversionInfo {
    struct {
        Format format;
        int stride;
    } in, out;
    int channels;
};

#define DEFINE_CONVERT_BUFFER(type)                                                                  \
static void convert_buffer_to_##type(const float* in, type* out, int frames, ConversionInfo* info) { \
    for (int i = 0; i < frames; ++i) {                                                               \
        for (int j = 0; j < info->channels; ++j) {                                                   \
            out[j] = convert_to_##type(in[j]);                                                       \
        }                                                                                            \
        in += info->in.stride;                                                                       \
        out += info->out.stride;                                                                     \
    }                                                                                                \
}

DEFINE_CONVERT_BUFFER(s8);
DEFINE_CONVERT_BUFFER(s16);
DEFINE_CONVERT_BUFFER(s24);
DEFINE_CONVERT_BUFFER(s32);
DEFINE_CONVERT_BUFFER(float);
DEFINE_CONVERT_BUFFER(double);

// This function does format conversion, input/output channel compensation, and
// data interleaving/deinterleaving.
static void format_buffer_from_float(float* in_samples, void* out_samples, int frames, ConversionInfo* info) {
    PROFILE_SCOPED();

    switch (info->in.format) {
        case FORMAT_S8:
            convert_buffer_to_s8(in_samples, static_cast<s8*>(out_samples), frames, info);
            break;
        case FORMAT_S16:
            convert_buffer_to_s16(in_samples, static_cast<s16*>(out_samples), frames, info);
            break;
        case FORMAT_S24:
            convert_buffer_to_s24(in_samples, static_cast<s24*>(out_samples), frames, info);
            break;
        case FORMAT_S32:
            convert_buffer_to_s32(in_samples, static_cast<s32*>(out_samples), frames, info);
            break;
        case FORMAT_F32:
            convert_buffer_to_float(in_samples, static_cast<float*>(out_samples), frames, info);
            break;
        case FORMAT_F64:
            convert_buffer_to_double(in_samples, static_cast<double*>(out_samples), frames, info);
            break;
    }
}

#if 0
// @Unused: but very useful for testing!

static const float tau = 6.28318530717958647692f;

static float pitch_to_frequency(int pitch) {
    return 440.0f * pow(2.0f, static_cast<float>(pitch - 69) / 12.0f);
}

static void generate_sine_samples(void* samples, int count, int channels,
                                  u32 sample_rate, double time,
                                  int pitch, float amplitude) {
    float frequency = pitch_to_frequency(pitch);
    float theta = tau * frequency;
    float* out = static_cast<float*>(samples);
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < channels; ++j) {
            float t = static_cast<float>(i) / sample_rate + time;
            out[i * channels + j] = amplitude * sin(theta * t);
        }
    }
}
#endif

// Advanced Linux Sound Architecture device back-end...........................

static int finalize_hw_params(snd_pcm_t* pcm_handle,
                              snd_pcm_hw_params_t* hw_params, bool override,
                              u64* frames) {
    int status;

    status = snd_pcm_hw_params(pcm_handle, hw_params);
    if (status < 0) {
        return -1;
    }

    snd_pcm_uframes_t buffer_size;
    status = snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
    if (status < 0) {
        return -1;
    }
    if (!override && buffer_size != *frames * 2) {
        return -1;
    }
    *frames = buffer_size / 2;

    return 0;
}

static int set_period_size(snd_pcm_t* pcm_handle,
                           snd_pcm_hw_params_t* hw_params, bool override,
                           u64* frames) {
    int status;

    snd_pcm_hw_params_t* hw_params_copy;
    snd_pcm_hw_params_alloca(&hw_params_copy);
    snd_pcm_hw_params_copy(hw_params_copy, hw_params);

    if (!override) {
        return -1;
    }

    snd_pcm_uframes_t nearest_frames = *frames;
    status = snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params_copy,
                                                    &nearest_frames, nullptr);
    if (status < 0) {
        return -1;
    }

    unsigned int periods = 2;
    status = snd_pcm_hw_params_set_periods_near(pcm_handle, hw_params_copy,
                                                &periods, nullptr);
    if (status < 0) {
        return -1;
    }

    return finalize_hw_params(pcm_handle, hw_params_copy, override, frames);
}

static int set_buffer_size(snd_pcm_t* pcm_handle,
                           snd_pcm_hw_params_t* hw_params, bool override,
                           u64* frames) {
    int status;

    snd_pcm_hw_params_t* hw_params_copy;
    snd_pcm_hw_params_alloca(&hw_params_copy);
    snd_pcm_hw_params_copy(hw_params_copy, hw_params);

    if (!override) {
        return -1;
    }

    snd_pcm_uframes_t nearest_frames;
    nearest_frames = *frames * 2;
    status = snd_pcm_hw_params_set_buffer_size_near(pcm_handle, hw_params_copy,
                                                    &nearest_frames);
    if (status < 0) {
        return -1;
    }

    return finalize_hw_params(pcm_handle, hw_params_copy, override, frames);
}

static const int test_format_count = 5;

static Format test_formats[test_format_count] = {
    FORMAT_F64,
    FORMAT_F32,
    FORMAT_S32,
    FORMAT_S16,
    FORMAT_S8,
};

snd_pcm_format_t get_equivalent_format(Format format) {
    switch (format) {
        case FORMAT_U8:  return SND_PCM_FORMAT_U8;
        case FORMAT_S8:  return SND_PCM_FORMAT_S8;
        case FORMAT_U16: return SND_PCM_FORMAT_U16;
        case FORMAT_S16: return SND_PCM_FORMAT_S16;
        case FORMAT_U24: return SND_PCM_FORMAT_U24;
        case FORMAT_S24: return SND_PCM_FORMAT_S24;
        case FORMAT_U32: return SND_PCM_FORMAT_U32;
        case FORMAT_S32: return SND_PCM_FORMAT_S32;
        case FORMAT_F32: return SND_PCM_FORMAT_FLOAT;
        case FORMAT_F64: return SND_PCM_FORMAT_FLOAT64;
    }
    return SND_PCM_FORMAT_UNKNOWN;
}

struct Specification {
    u64 size;
    u64 frames;
    Format format;
    u32 sample_rate;
    u8 channels;
    u8 silence;
};

static void fill_remaining_specification(Specification* specification) {
    switch (specification->format) {
        case FORMAT_U8:
            specification->silence = 0x80;
            break;
        default:
            specification->silence = 0x00;
            break;
    }
    specification->size = format_byte_count(specification->format) *
                          specification->channels * specification->frames;
}

static bool open_device(const char* name, Specification* specification,
                        snd_pcm_t** out_pcm_handle) {
    int status;

    snd_pcm_t* pcm_handle;
    status = snd_pcm_open(&pcm_handle, name, SND_PCM_STREAM_PLAYBACK,
                          SND_PCM_NONBLOCK);
    if (status < 0) {
        LOG_ERROR("Couldn't open audio device \"%s\". %s", name,
                  snd_strerror(status));
        return false;
    }
    *out_pcm_handle = pcm_handle;

    snd_pcm_hw_params_t* hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    status = snd_pcm_hw_params_any(pcm_handle, hw_params);
    if (status < 0) {
        LOG_ERROR("Couldn't get the hardware configuration. %s",
                  snd_strerror(status));
        return false;
    }

    status = snd_pcm_hw_params_set_access(pcm_handle, hw_params,
                                          SND_PCM_ACCESS_RW_INTERLEAVED);
    if (status < 0) {
        LOG_ERROR("Couldn't set the hardware to interleaved access. %s",
                  snd_strerror(status));
        return false;
    }

    Format test_format;
    status = -1;
    for (int i = 0; status < 0 && i < test_format_count; ++i) {
        test_format = test_formats[i];
        status = 0;
        snd_pcm_format_t pcm_format = get_equivalent_format(test_format);
        if (pcm_format == SND_PCM_FORMAT_UNKNOWN) {
            status = -1;
        } else {
            status = snd_pcm_hw_params_set_format(pcm_handle, hw_params,
                                                  pcm_format);
        }
    }
    if (status < 0) {
        LOG_ERROR("Failed to obtain a suitable hardware audio format.");
        return false;
    }
    specification->format = test_format;

    unsigned int channels = specification->channels;
    status = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels);
    if (status < 0) {
        status = snd_pcm_hw_params_get_channels(hw_params, &channels);
        if (status < 0) {
            LOG_ERROR("Couldn't set the channel count. %s",
                      snd_strerror(status));
            return false;
        }
        specification->channels = channels;
    }

    unsigned int resample = 1;
    status = snd_pcm_hw_params_set_rate_resample(pcm_handle, hw_params,
                                                 resample);
    if (status < 0) {
        LOG_ERROR("Failed to enable resampling. %s", snd_strerror(status));
        return false;
    }

    unsigned int rate = specification->sample_rate;
    status = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate,
                                             nullptr);
    if (status < 0) {
        LOG_ERROR("Couldn't set the sample rate. %s", snd_strerror(status));
        return false;
    }
    if (rate != specification->sample_rate) {
        LOG_ERROR("Couldn't obtain the desired sample rate for the device.");
        return false;
    }
    specification->sample_rate = rate;

    if (set_period_size(pcm_handle, hw_params, false, &specification->frames) < 0 &&
        set_buffer_size(pcm_handle, hw_params, false, &specification->frames) < 0) {
        if (set_period_size(pcm_handle, hw_params, true, &specification->frames) < 0) {
            LOG_ERROR("Couldn't set the desired period size and buffer size.");
            return false;
        }
    }

    snd_pcm_sw_params_t* sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    status = snd_pcm_sw_params_current(pcm_handle, sw_params);
    if (status < 0) {
        LOG_ERROR("Couldn't obtain the software configuration. %s",
                  snd_strerror(status));
        return false;
    }

    status = snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params,
                                             specification->frames);
    if (status < 0) {
        LOG_ERROR("Couldn't set the minimum available samples. %s",
                  snd_strerror(status));
        return false;
    }
    status = snd_pcm_sw_params_set_start_threshold(pcm_handle, sw_params, 1);
    if (status < 0) {
        LOG_ERROR("Couldn't set the start threshold. %s",
                  snd_strerror(status));
        return false;
    }
    status = snd_pcm_sw_params(pcm_handle, sw_params);
    if (status < 0) {
        LOG_ERROR("Couldn't set software audio parameters. %s",
                  snd_strerror(status));
        return false;
    }

    fill_remaining_specification(specification);

    return true;
}

static void close_device(snd_pcm_t* pcm_handle) {
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
    }
}

// Stream functions............................................................
//     for streaming audio from file sources, right now Vorbis from .ogg files
//     and PCM and ADPCM inside .wav files

struct Stream {
    enum class DecoderType {
        Vorbis,
        Wave,
    } decoder_type;

    union {
        struct {
            stb_vorbis* decoder;
        } vorbis;
        struct {
            WaveDecoder* decoder;
        } wave;
    };

    void* encoded_samples;
    int channels;
    float* decoded_samples;
    float volume;
    bool looping;
    bool finished;
    StreamId id;
};

static const int UNUSED_STREAM_ID = 0;

static Stream::DecoderType decoder_type_from_file_extension(const char* extension) {
         if (strings_match(extension, "wav")) return Stream::DecoderType::Wave;
    else if (strings_match(extension, "ogg")) return Stream::DecoderType::Vorbis;
    return Stream::DecoderType::Wave;
}

static void fill_with_silence(float* samples, u8 silence, u64 count) {
    set_memory(samples, silence, sizeof(float) * count);
}

static const int max_streams = 16;

struct StreamManager {
    Stream streams[max_streams];
    int stream_count;
};

static int close_stream(Heap* heap, StreamManager* manager, int stream_index) {
    ASSERT(stream_index >= 0 && stream_index < manager->stream_count);

    PROFILE_SCOPED();

    Stream* stream = manager->streams + stream_index;
    switch (stream->decoder_type) {
        case Stream::DecoderType::Vorbis: {
            stb_vorbis_close(stream->vorbis.decoder);
            break;
        }
        case Stream::DecoderType::Wave: {
            wave_close_file(stream->wave.decoder);
            break;
        }
    }
    heap_deallocate(heap, stream->decoded_samples);
    heap_deallocate(heap, stream->encoded_samples);

    int last = manager->stream_count - 1;
    if (manager->stream_count > 1 && stream_index != last) {
        manager->streams[stream_index] = manager->streams[last];
    }
    manager->stream_count -= 1;

    return stream_index - 1;
}

static void close_stream_by_id(Heap* heap, StreamManager* manager,
                               StreamId stream_id) {
    FOR_N (i, manager->stream_count) {
        Stream* stream = manager->streams + i;
        if (stream->id == stream_id) {
            i = close_stream(heap, manager, i);
        }
    }
}

static void close_all_streams(Heap* heap, StreamManager* manager) {
    FOR_N (i, manager->stream_count) {
        i = close_stream(heap, manager, i);
    }
}

static void close_finished_streams(Heap* heap, StreamManager* manager) {
    FOR_N (i, manager->stream_count) {
        Stream* stream = manager->streams + i;
        if (stream->finished) {
            i = close_stream(heap, manager, i);
        }
    }
}

static void open_stream(StreamManager* stream_manager,
                        const char* filename, u64 samples_to_decode,
                        float volume, bool looping, StreamId id,
                        Heap* heap, Stack* stack) {

    ASSERT(stream_manager->stream_count < ARRAY_COUNT(stream_manager->streams));

    PROFILE_SCOPED();

    Stream* stream = stream_manager->streams + stream_manager->stream_count;

    const char* file_extension = find_string(filename, ".") + 1;
    stream->decoder_type = decoder_type_from_file_extension(file_extension);

    int encoded_buffer_size = KIBIBYTES(128);
    void* encoded_buffer = nullptr;

    switch (stream->decoder_type) {
        case Stream::DecoderType::Vorbis: {
            stb_vorbis* decoder;
            int open_error;
            do {
                encoded_buffer = heap_reallocate(heap, encoded_buffer,
                                                 encoded_buffer_size);
                if (!encoded_buffer) {
                    LOG_ERROR("Could not obtain appropriate memory to load the "
                              "Vorbis file %s.", filename);
                    // @Incomplete: nothing actually happens to fail?
                }
                open_error = 0;
                stb_vorbis_alloc alloc;
                alloc.alloc_buffer_length_in_bytes = encoded_buffer_size;
                alloc.alloc_buffer = static_cast<char*>(encoded_buffer);
                decoder = stb_vorbis_open_filename(filename, &open_error,
                                                   &alloc, stack);
                encoded_buffer_size += KIBIBYTES(16);
            } while(!decoder && open_error == VORBIS_outofmem);
            if (!decoder || open_error) {
                LOG_ERROR("Vorbis file %s failed to load: %i", filename,
                          open_error);
                heap_deallocate(heap, encoded_buffer);
                // @Incomplete: nothing actually happens to fail this case?
            }
            stream->vorbis.decoder = decoder;
            stream->encoded_samples = encoded_buffer;

            stb_vorbis_info info = stb_vorbis_get_info(stream->vorbis.decoder);
            stream->channels = info.channels;
            break;
        }
        case Stream::DecoderType::Wave: {
            WaveDecoder* decoder;
            WaveOpenError open_error = WaveOpenError::None;
            do {
                encoded_buffer = heap_reallocate(heap, encoded_buffer,
                                                 encoded_buffer_size);
                if (!encoded_buffer) {
                    LOG_ERROR("Could not obtain appropriate memory to load "
                              "the Wave file %s.", filename);
                    // @Incomplete: fail state not handled
                }
                open_error = WaveOpenError::None;
                WaveMemory alloc;
                alloc.block = encoded_buffer;
                alloc.block_size = encoded_buffer_size;
                decoder = wave_open_file(filename, &open_error, &alloc, stack);
                encoded_buffer_size += KIBIBYTES(16);
            } while(!decoder && open_error == WaveOpenError::Out_Of_Memory);
            if (!decoder || open_error != WaveOpenError::None) {
                LOG_ERROR("Wave file %s failed to load: %i", filename, open_error);
                heap_deallocate(heap, encoded_buffer);
                // @Incomplete: nothing actually happens to fail this case?
            }
            stream->wave.decoder = decoder;
            stream->channels = wave_channels(decoder);
            stream->encoded_samples = encoded_buffer;
            break;
        }
    }

    stream->decoded_samples = ALLOCATE(heap, float, samples_to_decode);
    stream->volume = volume;
    stream->looping = looping;
    stream->finished = false;
    stream->id = id;
    stream_manager->stream_count += 1;
}

static void decode_streams(StreamManager* stream_manager, int frames) {
    PROFILE_SCOPED();

    FOR_N (i, stream_manager->stream_count) {
        Stream* stream = stream_manager->streams + i;
        int channels = stream->channels;
        int samples_needed = channels * frames;
        switch (stream->decoder_type) {
            case Stream::DecoderType::Vorbis: {
                int frames_decoded = stb_vorbis_get_samples_float_interleaved(stream->vorbis.decoder, channels, stream->decoded_samples, samples_needed);
                if (frames_decoded < frames) {
                    samples_needed -= frames_decoded * channels;
                    float* more_samples = stream->decoded_samples + frames_decoded * channels;
                    if (stream->looping) {
                        stb_vorbis_seek_start(stream->vorbis.decoder);
                        stb_vorbis_get_samples_float_interleaved(stream->vorbis.decoder, channels, more_samples, samples_needed);
                    } else {
                        fill_with_silence(more_samples, 0, samples_needed);
                        stream->finished = true;
                    }
                }
                break;
            }
            case Stream::DecoderType::Wave: {
                int frames_decoded = wave_decode_interleaved(stream->wave.decoder, channels, stream->decoded_samples, samples_needed);
                if (frames_decoded < frames) {
                    samples_needed -= frames_decoded * channels;
                    float* more_samples = stream->decoded_samples + frames_decoded * channels;
                    if (stream->looping) {
                        wave_seek_start(stream->wave.decoder);
                        wave_decode_interleaved(stream->wave.decoder, channels, more_samples, samples_needed);
                    } else {
                        fill_with_silence(more_samples, 0, samples_needed);
                        stream->finished = true;
                    }
                }
                break;
            }
        }
    }
}

static float clamp(float x, float min, float max) {
    return (x < min) ? min : (x > max) ? max : x;
}

static void mix_streams(StreamManager* stream_manager, float* mix_buffer, int frames, int channels) {

    PROFILE_SCOPED();

    int samples = frames * channels;

    // Mix the streams' samples into the given buffer.
    FOR_N(i, stream_manager->stream_count) {
        Stream* stream = stream_manager->streams + i;
        if (channels < stream->channels) {
            FOR_N(j, frames) {
                FOR_N(k, channels) {
                    mix_buffer[j*channels+k] += stream->volume * stream->decoded_samples[j*stream->channels];
                }
            }
        } else if (channels > stream->channels) {
            ASSERT(stream->channels == 1); // @Incomplete: This path doesn't actually handle stereo-to-surround mixing
            FOR_N(j, frames) {
                float sample = stream->volume * stream->decoded_samples[j*stream->channels];
                FOR_N(k, channels) {
                    mix_buffer[j*channels+k] += sample;
                }
            }
        } else {
            FOR_N(j, samples) {
                mix_buffer[j] += stream->volume * stream->decoded_samples[j];
            }
        }
    }

    // Clip the final amplitude of each sample to the range [-1,1].
    FOR_N(i, samples) {
        mix_buffer[i] = clamp(mix_buffer[i], -1.0f, 1.0f);
    }
}

// Message Queue...............................................................

struct Message {
    enum class Code {
        Play_Once,
        Start_Stream,
        Stop_Stream,
    } code;

    union {
        struct {
            char filename[128];
            float volume;
        } play_once;

        struct {
            char filename[128];
            StreamId stream_id;
            float volume;
        } start_stream;

        struct {
            StreamId stream_id;
        } stop_stream;
    };
};

static const int max_messages = 32;

struct MessageQueue {
    Message messages[max_messages];
    AtomicInt head;
    AtomicInt tail;
};

static bool was_empty(MessageQueue* queue) {
    return atomic_int_load(&queue->head) == atomic_int_load(&queue->tail);
}

static bool was_full(MessageQueue* queue) {
    int next_tail = atomic_int_load(&queue->tail);
    next_tail = (next_tail + 1) % max_messages;
    return next_tail == atomic_int_load(&queue->head);
}

static bool enqueue_message(MessageQueue* queue, Message* message) {
    int current_tail = atomic_int_load(&queue->tail);
    int next_tail = (current_tail + 1) % max_messages;
    if (next_tail != atomic_int_load(&queue->head)) {
        queue->messages[current_tail] = *message;
        atomic_int_store(&queue->tail, next_tail);
        return true;
    }
    return false;
}

static bool dequeue_message(MessageQueue* queue, Message* message) {
    int current_head = atomic_int_load(&queue->head);
    if (current_head == atomic_int_load(&queue->tail)) {
        return false;
    }
    *message = queue->messages[current_head];
    atomic_int_store(&queue->head, (current_head + 1) % max_messages);
    return true;
}

// System Functions............................................................

namespace {
    Stack perm_stack;
    Stack temp_stack;
    Heap heap;
    Heap profile_heap;
    StreamManager stream_manager;
    MessageQueue message_queue;
    ConversionInfo conversion_info;
    Specification specification; // device description
    snd_pcm_t* pcm_handle;
    float* mixed_samples;
    void* devicebound_samples;
    pthread_t thread;
    AtomicFlag quit;
    double time;
    StreamId stream_id_seed;
}

static void* run_mixer_thread(void* argument) {
    static_cast<void>(argument);

    // Obtain the memory needed.
    {
        bool good = true;
        good &= stack_create(&perm_stack, KIBIBYTES(2050));
        good &= stack_create_on_stack(&temp_stack, &perm_stack, KIBIBYTES(32));
        good &= heap_create_on_stack(&heap, &perm_stack, KIBIBYTES(2000));
        good &= heap_create_on_stack(&profile_heap, &perm_stack, KIBIBYTES(16));
        if (!good) {
            LOG_ERROR("Failed to obtain enough memory needed to run the audio "
                      "thread.");
            // @Incomplete: probably should exit?
        }
    }

    PROFILE_THREAD_ENTER(&profile_heap);

    specification.channels = 2;
    specification.format = FORMAT_S16;
    specification.sample_rate = 44100;
    specification.frames = 1024;
    fill_remaining_specification(&specification);
    if (!open_device("default", &specification, &pcm_handle)) {
        LOG_ERROR("Failed to open audio device.");
        // @Incomplete: probably should exit?
    }

    u64 samples = specification.channels * specification.frames;

    // Setup mixing.
    mixed_samples = ALLOCATE(&heap, float, samples);
    devicebound_samples = ALLOCATE(&heap, u8, specification.size);
    fill_with_silence(mixed_samples, specification.silence, samples);

    conversion_info.channels = specification.channels;
    conversion_info.in.format = FORMAT_F32;
    conversion_info.in.stride = conversion_info.channels;
    conversion_info.out.format = specification.format;
    conversion_info.out.stride = conversion_info.channels;

    int frame_size = conversion_info.channels
                   * format_byte_count(conversion_info.out.format);

    while (atomic_flag_test_and_set(&quit)) {
        // Process any messages from the main thread.
        {
            Message message;
            while (dequeue_message(&message_queue, &message)) {
                switch (message.code) {
                    case Message::Code::Play_Once: {
                        open_stream(&stream_manager,
                                    message.play_once.filename, samples,
                                    message.play_once.volume, false,
                                    UNUSED_STREAM_ID, &heap, &temp_stack);
                        break;
                    }
                    case Message::Code::Start_Stream: {
                        open_stream(&stream_manager,
                                    message.start_stream.filename, samples,
                                    message.start_stream.volume, true,
                                    message.start_stream.stream_id, &heap,
                                    &temp_stack);
                        break;
                    }
                    case Message::Code::Stop_Stream: {
                        close_stream_by_id(&heap, &stream_manager,
                                           message.stop_stream.stream_id);
                        break;
                    }
                }
            }
        }

        decode_streams(&stream_manager, specification.frames);

        fill_with_silence(mixed_samples, specification.silence, samples);
        mix_streams(&stream_manager, mixed_samples,
                    specification.frames, specification.channels);

        format_buffer_from_float(mixed_samples, devicebound_samples,
                                 specification.frames, &conversion_info);

        PROFILE_BEGIN_NAMED("audio::run_mixer_thread/waiting");

        int stream_ready = snd_pcm_wait(pcm_handle, 150);
        if (!stream_ready) {
            LOG_ERROR("ALSA device waiting timed out!");
        }

        PROFILE_END();

        PROFILE_BEGIN_NAMED("audio::run_mixer_thread/writing");

        u8* buffer = static_cast<u8*>(devicebound_samples);
        snd_pcm_uframes_t frames_left = specification.frames;
        while (frames_left > 0) {
            int frames_written = snd_pcm_writei(pcm_handle, buffer,
                                                frames_left);
            if (frames_written < 0) {
                int status = frames_written;
                if (status == -EAGAIN) {
                    continue;
                }
                status = snd_pcm_recover(pcm_handle, status, 0);
                if (status < 0) {
                    break;
                }
                continue;
            }
            buffer += frames_written * frame_size;
            frames_left -= frames_written;
        }

        PROFILE_END();

        close_finished_streams(&heap, &stream_manager);

        double delta_time = static_cast<double>(specification.frames) /
                            static_cast<double>(specification.sample_rate);
        time += delta_time;
    }

    close_all_streams(&heap, &stream_manager);

    // Clear up mixer data.
    close_device(pcm_handle);
    heap_deallocate(&heap, mixed_samples);
    heap_deallocate(&heap, devicebound_samples);

    PROFILE_THREAD_EXIT();

    // Check the memory usage before finishing.
    {
        size_t used, total;

        HeapInfo info = heap_get_info(&heap);
        ASSERT(info.used_blocks == 0);
        used = info.used_blocks * sizeof(Heap::Block);
        total = info.total_blocks * sizeof(Heap::Block);
        LOG_DEBUG("audio: allocated on heap: %zu/%zu B", used, total);
    }

    stack_destroy(&perm_stack);

    LOG_DEBUG("Audio thread shut down.");

    return nullptr;
}

bool startup() {
    atomic_flag_test_and_set(&quit);
    int result = pthread_create(&thread, nullptr, run_mixer_thread, nullptr);
    return result == 0;
}

void shutdown() {
    // Signal the mixer thread to quit and wait here for it to finish.
    atomic_flag_clear(&quit);
    pthread_join(thread, nullptr);
}

void play_once(const char* filename, float volume) {
    Message message;
    message.code = Message::Code::Play_Once;
    copy_string(message.play_once.filename, sizeof message.play_once.filename,
                filename);
    message.play_once.volume = volume;
    enqueue_message(&message_queue, &message);
}

static StreamId generate_stream_id() {
    stream_id_seed += 1;
    if (stream_id_seed == UNUSED_STREAM_ID) {
        // Reserve stream ids of 0 for streams that don't need to be
        // identified or referred to outside of the audio system.
        stream_id_seed = 1;
    }
    return stream_id_seed;
}

void start_stream(const char* filename, float volume,
                  StreamId* out_stream_id) {
    StreamId stream_id = generate_stream_id();

    Message message;
    message.code = Message::Code::Start_Stream;
    copy_string(message.start_stream.filename,
                sizeof message.start_stream.filename, filename);
    message.start_stream.stream_id = stream_id;
    message.start_stream.volume = volume;
    enqueue_message(&message_queue, &message);

    *out_stream_id = stream_id;
}

void stop_stream(StreamId stream_id) {
    Message message;
    message.code = Message::Code::Stop_Stream;
    message.stop_stream.stream_id = stream_id;
    enqueue_message(&message_queue, &message);
}

} // namespace audio
