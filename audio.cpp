#include "audio.h"

#include "sized_types.h"
#include "atomic.h"
#include "logging.h"
#include "wave_decoder.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <alsa/asoundlib.h>

#include <pthread.h>

#include <cstdlib>
#include <cstring>
#include <cmath>

#define ALLOCATE_STRUCT(type) \
    static_cast<type *>(std::malloc(sizeof(type)));

#define DEALLOCATE_STRUCT(s) \
    std::free(s);

#define CLEAR_STRUCT(s) \
    std::memset((s), 0, sizeof *(s));

#define ALLOCATE_ARRAY(type, count) \
    static_cast<type *>(std::malloc(sizeof(type) * (count)));

#define DEALLOCATE_ARRAY(a) \
    std::free(a);

namespace audio {

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

static std::size_t format_byte_count(Format format) {
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
typedef Int24 s24;

// Converts numbers between numeric types.
template <typename From, typename To>
inline To convert(From value) {
    // if template types are both the same, just do a regular copy
    return value;
}

// - to signed 8-bit integer

template<>
inline s8 convert<s16, s8>(s16 value) {
    return (value >> 8) & 0xFF;
}

template<>
inline s8 convert<s24, s8>(s24 value) {
    return (value.x >> 16) & 0xFF;
}

template<>
inline s8 convert<s32, s8>(s32 value) {
    return (value >> 24) & 0xFF;
}

template<>
inline s8 convert<float, s8>(float value) {
    return value * 127.5f - 0.5f;
}

template<>
inline s8 convert<double, s8>(double value) {
    return value * 127.5 - 0.5;
}

// - to signed 16-bit integer

template<>
inline s16 convert<s8, s16>(s8 value) {
    return value << 8;
}

template<>
inline s16 convert<s24, s16>(s24 value) {
    return (value.x >> 8) & 0xFFFF;
}

template<>
inline s16 convert<s32, s16>(s32 value) {
    return (value >> 16) & 0xFFFF;
}

template<>
inline s16 convert<float, s16>(float value) {
    return value * 32767.5f - 0.5f;
}

template<>
inline s16 convert<double, s16>(double value) {
    return value * 32767.5 - 0.5;
}

// - to signed 24-bit integer

template<>
inline s24 convert<s8, s24>(s8 value) {
    return { value << 16 };
}

template<>
inline s24 convert<s16, s24>(s16 value) {
    return { value << 8 };
}

template<>
inline s24 convert<s32, s24>(s32 value) {
    return { (value >> 8) & 0xFFFFFF };
}

template<>
inline s24 convert<float, s24>(float value) {
    return { static_cast<s32>(value * 8388607.5f - 0.5f) };
}

template<>
inline s24 convert<double, s24>(double value) {
    return { static_cast<s32>(value * 8388607.5 - 0.5) };
}

// - to signed 32-bit integer

template<>
inline s32 convert<s8, s32>(s8 value) {
    return value << 24;
}

template<>
inline s32 convert<s16, s32>(s16 value) {
    return value << 16;
}

template<>
inline s32 convert<s24, s32>(s24 value) {
    return value.x << 8;
}

template<>
inline s32 convert<float, s32>(float value) {
    return value * 2147483647.5f - 0.5f;
}

template<>
inline s32 convert<double, s32>(double value) {
    return value * 2147483647.5 - 0.5;
}

// - to float

template<>
inline float convert<s8, float>(s8 value) {
    static const float scale = static_cast<float>(1.0 / 127.5);
    return (static_cast<float>(value) + 0.5f) * scale;
}

template<>
inline float convert<s16, float>(s16 value) {
    static const float scale = static_cast<float>(1.0 / 32767.5);
    return (static_cast<float>(value) + 0.5f) * scale;
}

template<>
inline float convert<s24, float>(s24 value) {
    static const float scale = static_cast<float>(1.0 / 8388607.5);
    return (static_cast<float>(value.x) + 0.5f) * scale;
}

template<>
inline float convert<s32, float>(s32 value) {
    static const float scale = static_cast<float>(1.0 / 2147483647.5);
    return (static_cast<float>(value) + 0.5f) * scale;
}

// - to double

template<>
inline double convert<s8, double>(s8 value) {
    static const double scale = 1.0 / 127.5;
    return (static_cast<double>(value) + 0.5) * scale;
}

template<>
inline double convert<s16, double>(s16 value) {
    static const double scale = 1.0 / 32767.5;
    return (static_cast<double>(value) + 0.5) * scale;
}

template<>
inline double convert<s24, double>(s24 value) {
    static const double scale = 1.0 / 8388607.5;
    return (static_cast<double>(value.x) + 0.5) * scale;
}

template<>
inline double convert<s32, double>(s32 value) {
    static const double scale = 1.0 / 2147483647.5;
    return (static_cast<double>(value) + 0.5) * scale;
}

struct ConversionInfo {
    struct {
        Format format;
        int stride;
    } in, out;
    int channels;
};

template <typename From, typename To>
static void convert_buffer(const From *in, To *out, int frames,
                           ConversionInfo *info) {
    for (int i = 0; i < frames; ++i) {
        for (int j = 0; j < info->channels; ++j) {
            out[j] = convert<From, To>(in[j]);
        }
        in += info->in.stride;
        out += info->out.stride;
    }
}

template <typename From>
static void convert_from_source_format(From *in, void *out, int frames, ConversionInfo *info) {
    switch (info->out.format) {
        case FORMAT_S8:
            convert_buffer<From, s8>(in, static_cast<s8 *>(out), frames, info);
            break;
        case FORMAT_S16:
            convert_buffer<From, s16>(in, static_cast<s16 *>(out), frames, info);
            break;
        case FORMAT_S24:
            convert_buffer<From, s24>(in, static_cast<s24 *>(out), frames, info);
            break;
        case FORMAT_S32:
            convert_buffer<From, s32>(in, static_cast<s32 *>(out), frames, info);
            break;
        case FORMAT_F32:
            convert_buffer<From, float>(in, static_cast<float *>(out), frames, info);
            break;
        case FORMAT_F64:
            convert_buffer<From, double>(in, static_cast<double *>(out), frames, info);
            break;
    }
}

// This function does format conversion, input/output channel compensation, and
// data interleaving/deinterleaving.
static void convert_format(void *in_samples, void *out_samples, int frames, ConversionInfo *info) {
    switch (info->in.format) {
        case FORMAT_S8:
            convert_from_source_format<s8>(static_cast<s8 *>(in_samples), out_samples, frames, info);
            break;
        case FORMAT_S16:
            convert_from_source_format<s16>(static_cast<s16 *>(in_samples), out_samples, frames, info);
            break;
        case FORMAT_S24:
            convert_from_source_format<s24>(static_cast<s24 *>(in_samples), out_samples, frames, info);
            break;
        case FORMAT_S32:
            convert_from_source_format<s32>(static_cast<s32 *>(in_samples), out_samples, frames, info);
            break;
        case FORMAT_F32:
            convert_from_source_format<float>(static_cast<float *>(in_samples), out_samples, frames, info);
            break;
        case FORMAT_F64:
            convert_from_source_format<double>(static_cast<double *>(in_samples), out_samples, frames, info);
            break;
    }
}

#define F_TAU 6.28318530717958647692f

static float pitch_to_frequency(int pitch) {
    return 440.0f * pow(2.0f, static_cast<float>(pitch - 69) / 12.0f);
}

static void generate_sine_samples(void *samples, int count, int channels,
                                  u32 sample_rate, double time,
                                  int pitch, float amplitude) {
    float frequency = pitch_to_frequency(pitch);
    float theta = F_TAU * frequency;
    float *out = reinterpret_cast<float *>(samples);
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < channels; ++j) {
            float t = static_cast<float>(i) / sample_rate + time;
            out[i * channels + j] = amplitude * std::sin(theta * t);
        }
    }
}

#if 0
static pa_context *connect_context(pa_mainloop *mainloop, const char *name) {
    pa_context *context;

    pa_mainloop_api *mainloop_api = pa_mainloop_get_api(mainloop);
    context = pa_context_new(mainloop_api, name);
    if (!context) {
        LOG_ERROR("Failed to create new context.");
        return NULL;
    }

    int result = pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);
    if (result < 0) {
        LOG_ERROR("Context did not connect: %s.", pa_strerror(result));
        pa_context_unref(context);
        return NULL;
    }

    // Wait until either the newly-connected context is ready or there is an
    // error detected.

    pa_context_state_t state;
    do {
        if (pa_mainloop_iterate(mainloop, 1, NULL) < 0) {
            LOG_ERROR("Context did not connect: pa_mainloop_iterate failed.");
            pa_context_unref(context);
            return NULL;
        }
        state = pa_context_get_state(context);
        if (!PA_CONTEXT_IS_GOOD(state)) {
            LOG_ERROR("Context could not connect.");
            pa_context_unref(context);
            return NULL;
        }
    } while (state != PA_CONTEXT_READY);

    return context;
}

static pa_stream *connect_stream(pa_mainloop *mainloop,
                                 pa_context *context,
                                 pa_buffer_attr *attributes,
                                 pa_stream_flags_t flags,
                                 pa_sample_spec *sample_spec,
                                 pa_channel_map *channel_map) {
    pa_stream *stream;

    stream = pa_stream_new(context, "Playback", sample_spec, channel_map);
    if (!stream) {
        LOG_ERROR("Failed to create new stream. %s",
                  pa_strerror(pa_context_errno(context)));
        return NULL;
    }

    if (pa_stream_connect_playback(stream, NULL, attributes, flags, NULL,
                                   NULL) < 0) {
        LOG_ERROR("Failed to connect to newly-created stream. %s",
                  pa_strerror(pa_context_errno(context)));
        pa_stream_unref(stream);
        return NULL;
    }

    pa_stream_state_t state;
    do {
        if (pa_mainloop_iterate(mainloop, 1, NULL) < 0) {
            LOG_ERROR("Stream did not connect: pa_mainloop_iterate failed.");
            pa_stream_unref(stream);
            return NULL;
        }
        state = pa_stream_get_state(stream);
        if (!PA_STREAM_IS_GOOD(state)) {
            LOG_ERROR("Stream could not connect.");
            pa_stream_unref(stream);
            return NULL;
        }
    } while (state != PA_STREAM_READY);

    return stream;
}

static bool open_pulseaudio(pa_mainloop **out_mainloop,
                            pa_context **out_context,
                            pa_stream **out_stream,
                            u8 channels,
                            pa_sample_format_t sample_format,
                            u32 sample_rate) {

    pa_mainloop *mainloop;
    mainloop = pa_mainloop_new();
    if (!mainloop) {
        LOG_ERROR("Failed to create new mainloop.");
        return false;
    }

    pa_context *context;
    context = connect_context(mainloop, "mandible");
    if (!context) {
        pa_mainloop_free(mainloop);
        return false;
    }

    pa_stream *stream;
    pa_stream_flags_t flags = static_cast<pa_stream_flags_t>(
                              PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
                              PA_STREAM_FIX_CHANNELS);
    pa_sample_spec spec;
    spec.channels = channels;
    spec.format = sample_format;
    spec.rate = sample_rate;
    stream = connect_stream(mainloop, context, NULL, flags, &spec, NULL);
    if (!stream) {
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
        return false;
    }

    *out_mainloop = mainloop;
    *out_context = context;
    *out_stream = stream;
    return true;
}

static void close_pulseaudio(pa_mainloop *mainloop, pa_context *context,
                             pa_stream *stream) {
    if (stream) {
        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
    }
    if (context) {
        pa_context_disconnect(context);
        pa_context_unref(context);
    }
    if (mainloop) {
        pa_mainloop_free(mainloop);
    }
}

static void wait_for_stream_space(pa_mainloop *mainloop, pa_context *context,
                                  pa_stream *stream, size_t desired_bytes) {
    for (;;) {
        if (pa_context_get_state(context) != PA_CONTEXT_READY ||
            pa_stream_get_state(stream) != PA_STREAM_READY ||
            pa_mainloop_iterate(mainloop, 1, NULL) < 0) {
            return;
        }
        if (pa_stream_writable_size(stream) >= desired_bytes) {
            return;
        }
    }
}

static void *run_mixer_thread(void *argument) {
    System *system = static_cast<System *>(argument);

    if (!open_pulseaudio(&system->mainloop, &system->context, &system->stream,
                         system->channels, PA_SAMPLE_S16NE,
                         system->sample_rate)) {
        LOG_ERROR("PulseAudio initialization failed!");
    }

    size_t format_size = sample_format_size(system->devicebound_format);
    size_t frame_size = format_size *  system->channels;
    size_t update_size = system->mixed_sample_count * frame_size;

    while (atomic_flag_test_and_set(&system->quit)) {
        size_t amount_to_write = update_size;
        while (amount_to_write > 0) {
            size_t stream_buffer_size = amount_to_write;
            void *stream_buffer;
            pa_stream_begin_write(system->stream, &stream_buffer,
                                  &stream_buffer_size);

            int samples_to_mix = stream_buffer_size / frame_size;
            generate_sine_samples(system->mixed_samples, samples_to_mix,
                                  system->channels, system->sample_rate,
                                  system->time, 70, 0.5f);

            convert_format(system->mixed_samples, stream_buffer,
                           samples_to_mix, &system->conversion_info);

            pa_stream_write(system->stream, stream_buffer, stream_buffer_size,
                            NULL, 0, PA_SEEK_RELATIVE);
            amount_to_write -= stream_buffer_size;
        }

        wait_for_stream_space(system->mainloop, system->context,
                              system->stream, update_size);

        double delta_time = static_cast<double>(system->mixed_sample_count) /
                            static_cast<double>(system->sample_rate);
        system->time += delta_time;
    }

    close_pulseaudio(system->mainloop, system->context, system->stream);

    return NULL;
}
#endif

static int finalize_hw_params(snd_pcm_t *pcm_handle,
                              snd_pcm_hw_params_t *hw_params, bool override,
                              u64 *frames) {
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

static int set_period_size(snd_pcm_t *pcm_handle,
                           snd_pcm_hw_params_t *hw_params, bool override,
                           u64 *frames) {
    int status;

    snd_pcm_hw_params_t *hw_params_copy;
    snd_pcm_hw_params_alloca(&hw_params_copy);
    snd_pcm_hw_params_copy(hw_params_copy, hw_params);

    if (!override) {
        return -1;
    }

    snd_pcm_uframes_t nearest_frames = *frames;
    status = snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params_copy,
                                                    &nearest_frames, NULL);
    if (status < 0) {
        return -1;
    }

    unsigned int periods = 2;
    status = snd_pcm_hw_params_set_periods_near(pcm_handle, hw_params_copy,
                                                &periods, NULL);
    if (status < 0) {
        return -1;
    }

    return finalize_hw_params(pcm_handle, hw_params_copy, override, frames);
}

static int set_buffer_size(snd_pcm_t *pcm_handle,
                           snd_pcm_hw_params_t *hw_params, bool override,
                           u64 *frames) {
    int status;

    snd_pcm_hw_params_t *hw_params_copy;
    snd_pcm_hw_params_alloca(&hw_params_copy);
    snd_pcm_hw_params_copy(hw_params_copy, hw_params);

    if (!override) {
        return -1;
    }

    snd_pcm_uframes_t nearest_frames;
    nearest_frames = *frames * 2;
    status = snd_pcm_hw_params_set_buffer_size_near(pcm_handle,
                                                    hw_params_copy,
                                                    &nearest_frames);
    if (status < 0) {
        return -1;
    }

    return finalize_hw_params(pcm_handle, hw_params_copy, override, frames);
}

#define TEST_FORMAT_COUNT 5
static Format test_formats[TEST_FORMAT_COUNT] = {
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

static void fill_remaining_specification(Specification *specification) {
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

static bool open_device(const char *name, Specification *specification,
                        snd_pcm_t **out_pcm_handle) {
    int status;

    snd_pcm_t *pcm_handle;
    status = snd_pcm_open(&pcm_handle, name, SND_PCM_STREAM_PLAYBACK,
                          SND_PCM_NONBLOCK);
    if (status < 0) {
        LOG_ERROR("Couldn't open audio device \"%s\". %s", name,
                  snd_strerror(status));
        return false;
    }
    *out_pcm_handle = pcm_handle;

    snd_pcm_hw_params_t *hw_params;
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
    for (int i = 0; status < 0 && i < TEST_FORMAT_COUNT; ++i) {
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

    unsigned int rate = specification->sample_rate;
    status = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate,
                                             NULL);
    if (status < 0) {
        LOG_ERROR("Couldn't set the sample rate. %s", snd_strerror(status));
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

    snd_pcm_sw_params_t *sw_params;
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

    snd_pcm_nonblock(pcm_handle, 0);

    return true;
}

static void close_device(snd_pcm_t *pcm_handle) {
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
    }
}

struct Stream {
    enum DecoderType {
        DECODER_VORBIS,
        DECODER_WAVE,
    };
    DecoderType decoder_type;
    union {
        struct {
            stb_vorbis *decoder;
        } vorbis;
        struct {
            WaveDecoder *decoder;
        } wave;
    };
    float *decoded_samples;
    float volume;
};

#define FOR_N(index, n) \
    for (auto (index) = 0; (index) < (n); ++(index))

static void fill_with_silence(float *samples, u8 silence, u64 count) {
    std::memset(samples, silence, sizeof(float) * count);
}

#define MAX_STREAMS 2

struct StreamManager {
    Stream streams[MAX_STREAMS];
    int stream_count;
};

static void initialise_stream_manager(StreamManager *stream_manager) {
    std::memset(stream_manager, 0, sizeof *stream_manager);
    FOR_N(i, MAX_STREAMS) {
        Stream *stream = stream_manager->streams + i;
        stream->vorbis.decoder = NULL;
        stream->decoded_samples = NULL;
    }
}

static void close_all_streams(StreamManager *stream_manager) {
    FOR_N(i, stream_manager->stream_count) {
        Stream *stream = stream_manager->streams + i;
        switch (stream->decoder_type) {
            case Stream::DECODER_VORBIS: {
                stb_vorbis_close(stream->vorbis.decoder);
            } break;

            case Stream::DECODER_WAVE: {
                wave_close_file(stream->wave.decoder);
            } break;
        }
        DEALLOCATE_ARRAY(stream->decoded_samples);
    }
}

static void open_stream(StreamManager *stream_manager, const char *filename,
                        Stream::DecoderType decoder_type,
                        u64 samples_to_decode) {
    Stream *stream = stream_manager->streams + stream_manager->stream_count;
    stream->decoder_type = decoder_type;

    switch (stream->decoder_type) {
        case Stream::DECODER_VORBIS: {
            stb_vorbis *decoder;
            int open_error = 0;
            decoder = stb_vorbis_open_filename(filename, &open_error, NULL);
            if (!decoder || open_error) {
                LOG_ERROR("Vorbis file %s failed to load: %i", filename,
                          open_error);
            }
            stream->vorbis.decoder = decoder;
        } break;

        case Stream::DECODER_WAVE: {
            WaveDecoder *decoder;
            decoder = wave_open_file(filename);
            if (!decoder) {
                LOG_ERROR("Wave file %s failed to load.", filename);
            }
            stream->wave.decoder = decoder;
        } break;
    }

    stream->decoded_samples = ALLOCATE_ARRAY(float, samples_to_decode);
    stream->volume = 1.0f;
    ++stream_manager->stream_count;
}

static void decode_streams(StreamManager *stream_manager, int frames,
                           int channels) {
    int samples_to_decode = channels * frames;
    FOR_N(i, stream_manager->stream_count) {
        Stream *stream = stream_manager->streams + i;
        switch (stream->decoder_type) {
            case Stream::DECODER_VORBIS: {
                int frames_decoded =
                        stb_vorbis_get_samples_float_interleaved(stream->vorbis.decoder,
                                                                 channels,
                                                                 stream->decoded_samples,
                                                                 samples_to_decode);
                if (frames_decoded < frames) {
                    samples_to_decode -= frames_decoded * channels;
                    stb_vorbis_seek_start(stream->vorbis.decoder);
                    stb_vorbis_get_samples_float_interleaved(stream->vorbis.decoder,
                                                             channels,
                                                             stream->decoded_samples,
                                                             samples_to_decode);
                }
            } break;

            case Stream::DECODER_WAVE: {
                int frames_decoded = wave_decode_interleaved(stream->wave.decoder,
                                                             channels,
                                                             stream->decoded_samples,
                                                             samples_to_decode);
                if (frames_decoded < frames) {
                    samples_to_decode -= frames_decoded * channels;
                    wave_seek_start(stream->wave.decoder);
                    wave_decode_interleaved(stream->wave.decoder, channels,
                                            stream->decoded_samples,
                                            samples_to_decode);
                }
            } break;
        }
    }
}

static float clamp(float x, float min, float max) {
    return (x < min) ? min : (x > max) ? max : x;
}

static void mix_streams(StreamManager *stream_manager, float *mix_buffer,
                        int samples) {
    // Mix the streams' samples into the given buffer.
    FOR_N(i, stream_manager->stream_count) {
        Stream *stream = stream_manager->streams + i;
        FOR_N(j, samples) {
            mix_buffer[j] += stream->volume * stream->decoded_samples[j];
        }
    }

    // Clip the final amplitude of each sample to the range [-1,1].
    FOR_N(i, samples) {
        mix_buffer[i] = clamp(mix_buffer[i], -1.0f, 1.0f);
    }
}

struct System {
    StreamManager stream_manager;
    ConversionInfo conversion_info;
    Specification specification;
    snd_pcm_t *pcm_handle;
    float *mixed_samples;
    void *devicebound_samples;
    pthread_t thread;
    AtomicFlag quit;
    double time;
};

static void *run_mixer_thread(void *argument) {
    System *system = static_cast<System *>(argument);

    Specification specification;
    specification.channels = 2;
    specification.format = FORMAT_S16;
    specification.sample_rate = 44100;
    specification.frames = 2048;
    fill_remaining_specification(&specification);
    if (!open_device("default", &specification, &system->pcm_handle)) {
        LOG_ERROR("Failed to open audio device.");
    }
    system->specification = specification;

    std::size_t samples = specification.channels * specification.frames;

    // Setup streams.

    initialise_stream_manager(&system->stream_manager);
    open_stream(&system->stream_manager, "Assets/grass_adpcm.wav",
                Stream::DECODER_WAVE, samples);

    // Setup mixing.

    system->mixed_samples = ALLOCATE_ARRAY(float, samples);
    system->devicebound_samples = ALLOCATE_ARRAY(u8, specification.size);
    fill_with_silence(system->mixed_samples, specification.silence, samples);

    system->conversion_info.channels = specification.channels;
    system->conversion_info.in.format = FORMAT_F32;
    system->conversion_info.in.stride = system->conversion_info.channels;
    system->conversion_info.out.format = system->specification.format;
    system->conversion_info.out.stride = system->conversion_info.channels;

    int frame_size = system->conversion_info.channels *
                     format_byte_count(system->conversion_info.out.format);

    while (atomic_flag_test_and_set(&system->quit)) {
        decode_streams(&system->stream_manager, system->specification.frames,
                       system->specification.channels);

        fill_with_silence(system->mixed_samples, specification.silence, samples);
        mix_streams(&system->stream_manager, system->mixed_samples, samples);

        convert_format(system->mixed_samples, system->devicebound_samples,
                       system->specification.frames, &system->conversion_info);

        u8 *buffer = static_cast<u8 *>(system->devicebound_samples);
        snd_pcm_uframes_t frames_left = system->specification.frames;
        while (frames_left > 0) {
            int status = snd_pcm_writei(system->pcm_handle, buffer, frames_left);
            if (status < 0) {
                if (status == -EAGAIN) {
                    continue;
                }
                status = snd_pcm_recover(system->pcm_handle, status, 0);
                if (status < 0) {
                    break;
                }
                continue;
            }
            buffer += status * frame_size;
            frames_left -= status;
        }

        double delta_time = static_cast<double>(system->specification.frames) /
                            static_cast<double>(system->specification.sample_rate);
        system->time += delta_time;
    }

    close_all_streams(&system->stream_manager);

    // Clear up mixer data.
    close_device(system->pcm_handle);
    DEALLOCATE_ARRAY(system->mixed_samples);
    DEALLOCATE_ARRAY(system->devicebound_samples);

    return NULL;
}

System *startup() {
    System *system = ALLOCATE_STRUCT(System);
    if (!system) {
        LOG_ERROR("Failed to allocate memory for the audio system.");
    }
    CLEAR_STRUCT(system);

    atomic_flag_test_and_set(&system->quit);
    pthread_create(&system->thread, NULL, run_mixer_thread, system);

    return system;
}

void shutdown(System *system) {
    // Signal the mixer thread to quit and wait here for it to finish.
    atomic_flag_clear(&system->quit);
    pthread_join(system->thread, NULL);

    // Destroy the system.
    DEALLOCATE_STRUCT(system);
}

} // namespace audio
