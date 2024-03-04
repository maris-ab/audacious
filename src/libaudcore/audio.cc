/*
 * audio.c
 * Copyright 2009-2024 John Lindgren, Michał Lipski, and Anders Johansson
 * DSD, DoP and FLOAT64 support added by Maris Abele
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <fenv.h>
#include <math.h>
#include <string.h>

#define WANT_AUD_BSWAP
#include "audio.h"
#include "objects.h"

#define SW_VOLUME_RANGE 40 // decibels

struct packed24_t
{
    uint8_t b[3];
    packed24_t& operator = (const uint32_t oi)
    {
        b[0] = oi & 0xff;
        b[1] = (oi >> 8) & 0xff;
        b[2] = (oi >> 16) & 0xff;
        return *this;
    }
};
static_assert(sizeof(packed24_t) == 3, "invalid packed 24-bit type");

template<class Word>
void interlace_loop(const void * const * in, int channels, void * out,
                    int frames)
{
    for (int c = 0; c < channels; c++)
    {
        auto get = (const Word *)in[c];
        const auto end = get + frames;
        auto set = (Word *)out + c;
        while (get < end)
        {
            *set = *get++;
            set += channels;
        }
    }
}

template<class Word>
void deinterlace_loop(const void * in, int channels, void * const * out,
                      int frames)
{
    for (int c = 0; c < channels; c++)
    {
        auto get = (const Word *)in + c;
        auto set = (Word *)out[c];
        const auto end = set + frames;
        while (set < end)
        {
            *set++ = *get;
            get += channels;
        }
    }
}

EXPORT void audio_interlace(const void * const * in, int format, int channels,
                            void * out, int frames)
{
    if (format == FMT_FLOAT)
        interlace_loop<float>(in, channels, out, frames);
    else if (format == FMT_FLOAT64)
        interlace_loop<double>(in, channels, out, frames);
    else
    {
        switch (FMT_SIZEOF(format))
        {
        case 1: // FMT_X8
            interlace_loop<int8_t>(in, channels, out, frames);
            break;

        case 2: // FMT_X16_XE
            interlace_loop<int16_t>(in, channels, out, frames);
            break;

        case 4: // FMT_X24_XE, FMT_X32_XE
            interlace_loop<int32_t>(in, channels, out, frames);
            break;

        case 3: // FMT_X24_3XE
            interlace_loop<packed24_t>(in, channels, out, frames);
            break;
        }
    }
}

EXPORT void audio_deinterlace(const void * in, int format, int channels,
                              void * const * out, int frames)
{
    if (format == FMT_FLOAT)
        deinterlace_loop<float>(in, channels, out, frames);
    else if (format == FMT_FLOAT64)
        deinterlace_loop<double>(in, channels, out, frames);
    else
    {
        switch (FMT_SIZEOF(format))
        {
        case 1: // FMT_X8
            deinterlace_loop<int8_t>(in, channels, out, frames);
            break;

        case 2: // FMT_X16_XE
            deinterlace_loop<int16_t>(in, channels, out, frames);
            break;

        case 4: // FMT_X24_XE, FMT_X32_XE
            deinterlace_loop<int32_t>(in, channels, out, frames);
            break;

        case 3: // FMT_X24_3XE
            deinterlace_loop<packed24_t>(in, channels, out, frames);
            break;
        }
    }
}

static constexpr bool is_le(int format)
{
    return format == FMT_S16_LE || format == FMT_U16_LE ||
           format == FMT_DSD_MSB16_LE ||
           format == FMT_S24_LE || format == FMT_U24_LE ||
           format == FMT_S32_LE || format == FMT_U32_LE ||
           format == FMT_DSD_MSB32_LE ||
           format == FMT_S24_3LE || format == FMT_U24_3LE ||
           format == FMT_DSD_MSB24_3LE;
}

static constexpr bool is_signed(int format)
{
    return (format == FMT_S8 || format == FMT_S16_LE || format == FMT_S16_BE ||
            format == FMT_S24_LE || format == FMT_S24_BE ||
            format == FMT_S32_LE || format == FMT_S32_BE ||
            format == FMT_S24_3LE || format == FMT_S24_3BE);
}

static constexpr unsigned neg_range(int format)
{
    return (format >= FMT_S32_LE && format < FMT_S24_3LE)
               ? 0x80000000
               : (format >= FMT_S24_LE)
                     ? 0x800000
                     : (format >= FMT_S16_LE) ? 0x8000 : 0x80;
}

// For float32 0x7fffff80 = largest representable floating-point value before 2^31
static constexpr unsigned pos_rangef(int format)
{
    return (format >= FMT_S32_LE && format < FMT_S24_3LE)
               ? 0x7fffff80
               : (format >= FMT_S24_LE)
                     ? 0x7fffff
                     : (format >= FMT_S16_LE) ? 0x7fff : 0x7f;
}

// For double - float64
static constexpr unsigned pos_ranged(int format)
{
    return (format >= FMT_S32_LE && format < FMT_S24_3LE)
               ? 0x7fffffff
               : (format >= FMT_S24_LE)
                     ? 0x7fffff
                     : (format >= FMT_S16_LE) ? 0x7fff : 0x7f;
}

// Swap LE BE for foat
template<class T>
T do_swap(T value)
{
    return value;
}
template<>
int16_t do_swap(int16_t value)
{
    return bswap16(value);
}
template<>
int32_t do_swap(int32_t value)
{
    return bswap32(value);
}
// Swap LE BE for DSD
template<>
uint16_t do_swap(uint16_t value)
{
    return bswap16(value);
}
template<>
uint32_t do_swap(uint32_t value)
{
    return bswap32(value);
}

// Integer Converter
template<int format, class Word, class Int>
struct Convert
{
#ifdef WORDS_BIGENDIAN
    static constexpr bool native_le = false;
#else
    static constexpr bool native_le = true;
#endif

    static Int to_int(Word value)
    {
        if (is_le(format) ^ native_le)
            value = do_swap(value);
        if (is_signed(format))
            value += neg_range(format);
        if (format >= FMT_S24_LE && format <= FMT_U24_BE)
            value &= 0xffffff; // ignore high byte

        return value - neg_range(format);
    }

    static Word to_word(Int value)
    {
        if (!is_signed(format))
            value += neg_range(format);
        if (format >= FMT_S24_LE && format <= FMT_U24_BE)
            value &= 0xffffff; // zero high byte
        if (is_le(format) ^ native_le)
            value = do_swap(value);

        return value;
    }
};

// Integer Converter for packed24_t
template<int format>
struct Convert<format, packed24_t, int32_t>
{
    static int32_t to_int(packed24_t value)
    {
        uint8_t hi, mid, lo;

        if (is_le(format))
            hi = value.b[2], mid = value.b[1], lo = value.b[0];
        else
            hi = value.b[0], mid = value.b[1], lo = value.b[2];

        if (!is_signed(format))
            hi -= 0x80;

        return (int8_t(hi) << 16) | (mid << 8) | lo;
    }

    static packed24_t to_word(int32_t value)
    {
        auto hi = uint8_t(value >> 16),
            mid = uint8_t(value >> 8),
             lo = uint8_t(value);

        if (!is_signed(format))
            hi += 0x80;

        if (is_le(format))
            return {{lo, mid, hi}};
        else
            return {{hi, mid, lo}};
    }
};

// From in to Float32 audio
template<int format, class Word, class Int = Word>
void audio_convert_loop(const void * in_, float * out, int samples)
{
    auto in = (const Word *)in_;
    const auto end = in + samples;
    while (in < end)
    {
        Int value = Convert<format, Word, Int>::to_int(*in++);
        *out++ = value * (1.0f / neg_range(format));
    }
}

// From Float32 audio to out
template<int format, class Word, class Int = Word>
void audio_convert_loop(const float * in, void * out_, int samples)
{
    auto out = (Word *)out_;
    const auto end = in + samples;
    while (in < end)
    {
        float f = (*in++) * neg_range(format);
        f = aud::clamp(f, -(float)neg_range(format), (float)pos_rangef(format));
        *out++ = Convert<format, Word, Int>::to_word(lrintf(f));
    }
}

// From in to Float64 audio
template<int format, class Word, class Int = Word>
void audio_convert_loop(const void * in_, double * out, int samples)
{
    auto in = (const Word *)in_;
    const auto end = in + samples;
    while (in < end)
    {
        Int value = Convert<format, Word, Int>::to_int(*in++);
        *out++ = value * (1.0 / neg_range(format));
    }
}

// From Float64 audio to out
template<int format, class Word, class Int = Word>
void audio_convert_loop(const double * in, void * out_, int samples)
{
    auto out = (Word *)out_;
    const auto end = in + samples;
    while (in < end)
    {
        double df = (*in++) * neg_range(format);
        df = aud::clamp(df, -(double)neg_range(format), (double)pos_ranged(format));
        *out++ = Convert<format, Word, Int>::to_word(lrint(df));
    }
}

// From in FloatXX to Float audio
template<int format, class Word>
void float_convert_loop(const void * in_, audio_sample * out, int samples)
{
    if (format == FMT_AUDIO_SAMPLE)
    {   // Perform only copy if format match
        memcpy(out, in_, samples * sizeof (audio_sample));
    }
    else
    {
        auto in = (const Word *)in_;
        const auto end = in + samples;
        while (in < end)
        {
            *out++ = (Word)(*in++);
        }
    }
}

// From Float audio to out FloatXX
template<int format, class Word>
void float_convert_loop(const audio_sample * in, void * out_, int samples)
{
    if (format == FMT_AUDIO_SAMPLE)
    {   // Perform only copy if format match
        memcpy(out_, in, samples * sizeof (audio_sample));
    }
    else
    {
        auto out = (Word *)out_;
        const auto end = in + samples;
        while (in < end)
        {
            *out++ = (Word)(*in++);
        }
    }
}

// Match FloatXX conversion
template<class Iformat, class Oformat>
void audio_convert(const Iformat * in, Oformat * out, int samples, int format)
{
    switch (format)
    {
    case FMT_FLOAT:
        float_convert_loop<FMT_FLOAT, float>(in, out, samples);
        break;
    case FMT_FLOAT64:
        float_convert_loop<FMT_FLOAT64, double>(in, out, samples);
        break;

    case FMT_S8:
        audio_convert_loop<FMT_S8, int8_t>(in, out, samples);
        break;
    case FMT_U8:
        audio_convert_loop<FMT_U8, int8_t>(in, out, samples);
        break;

    case FMT_S16_LE:
        audio_convert_loop<FMT_S16_LE, int16_t>(in, out, samples);
        break;
    case FMT_S16_BE:
        audio_convert_loop<FMT_S16_BE, int16_t>(in, out, samples);
        break;
    case FMT_U16_LE:
        audio_convert_loop<FMT_U16_LE, int16_t>(in, out, samples);
        break;
    case FMT_U16_BE:
        audio_convert_loop<FMT_U16_BE, int16_t>(in, out, samples);
        break;

    case FMT_S24_LE:
        audio_convert_loop<FMT_S24_LE, int32_t>(in, out, samples);
        break;
    case FMT_S24_BE:
        audio_convert_loop<FMT_S24_BE, int32_t>(in, out, samples);
        break;
    case FMT_U24_LE:
        audio_convert_loop<FMT_U24_LE, int32_t>(in, out, samples);
        break;
    case FMT_U24_BE:
        audio_convert_loop<FMT_U24_BE, int32_t>(in, out, samples);
        break;

    case FMT_S32_LE:
        audio_convert_loop<FMT_S32_LE, int32_t>(in, out, samples);
        break;
    case FMT_S32_BE:
        audio_convert_loop<FMT_S32_BE, int32_t>(in, out, samples);
        break;
    case FMT_U32_LE:
        audio_convert_loop<FMT_U32_LE, int32_t>(in, out, samples);
        break;
    case FMT_U32_BE:
        audio_convert_loop<FMT_U32_BE, int32_t>(in, out, samples);
        break;

    case FMT_S24_3LE:
        audio_convert_loop<FMT_S24_3LE, packed24_t, int32_t>(in, out, samples);
        break;
    case FMT_S24_3BE:
        audio_convert_loop<FMT_S24_3BE, packed24_t, int32_t>(in, out, samples);
        break;
    case FMT_U24_3LE:
        audio_convert_loop<FMT_U24_3LE, packed24_t, int32_t>(in, out, samples);
        break;
    case FMT_U24_3BE:
        audio_convert_loop<FMT_U24_3BE, packed24_t, int32_t>(in, out, samples);
        break;
    }
}

// Convert from in to FloatXX audio
EXPORT void audio_from_int (const void * in, int format, audio_sample * out, int samples)
{
    audio_convert<void, audio_sample>(in, out, samples, format);
}

// Convert from FloatXX audio to out
EXPORT void audio_to_int (const audio_sample * in, void * out, int format, int samples)
{
    int save = fegetround();
    fesetround(FE_TONEAREST);

    audio_convert<audio_sample, void>(in, out, samples, format);

    fesetround(save);
}

// From in to DSD audio
template<int format, class Word>
void dsd_audio_convert_loop_in(const void * in_, uint8_t * out, int samples, int channels, bool is_dop = false)
{
#ifdef WORDS_BIGENDIAN
    static constexpr bool native_le = false;
#else
    static constexpr bool native_le = true;
#endif
    auto in = (const Word *)in_;
    const auto end = in + samples;
    int channel = 0;
    unsigned int fsizeof = FMT_SIZEOF(format);
    while (in < end)
    {
        Word value = *(in++);
        // Swap
        if (!is_le(format) ^ native_le)
            value = do_swap(value);
        // Interleave DSD by channels
        for (unsigned int i=0; i < channels*fsizeof; i+=channels)
        {
            *(out+i) = (uint8_t) value & 0xff;
            value >>= 8;
        }
        out++;
        if(++channel >= channels)
        {
            channel = 0;
            out += (fsizeof-1)*channels;
        }
    }
}

// From DSD audio and DoP to out
template<int format, class Word, class Int = Word>
void dsd_audio_convert_loop_out(const uint8_t * in, void * out_, int samples, int channels, bool is_dop = false)
{
#ifdef WORDS_BIGENDIAN
    static constexpr bool native_le = false;
#else
    static constexpr bool native_le = true;
#endif
    auto out = (Word *)out_;
    const auto end = in + samples;
    int channel = 0;
    uint8_t  marker = 0x05; // DoP marker
    unsigned int fsizeof = FMT_SIZEOF(format);
    if (is_dop)
        fsizeof = 2;
    while (in < end)
    {
        Int value = 0;
        if (is_dop)
            value = marker;
        // Interleave DSD by channels
        for (unsigned int i=0; i < channels*fsizeof; i+=channels)
        {
            value <<= 8;
            value = value | *(in+i);
        }
        if ((is_dop && format >= FMT_S32_LE && format <= FMT_U32_BE)
            || format == FMT_DSD_MSB24_3BE
            || format == FMT_U24_3BE
            || format == FMT_S24_3BE)
            // Align DoP to 4 bytes
            value <<= 8;
        // Swap
        if (is_le(format) ^ native_le)
            value = do_swap(value);
        *(out++) = value;
        in++;
        if(++channel >= channels)
        {
            channel = 0;
            marker = ~marker;
            in += (fsizeof-1)*channels;
        }
    }
}

// Bit reverse DSD LSB Least Significant Bit first
static const unsigned char reverse_tab[16] = {
  0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
  0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf};

// Convert from in to DSD audio
EXPORT void dsdaudio_from_in(const void * in, int format, void * out_, int samples, int channels)
{
    uint8_t * out = (uint8_t *)out_;
    switch (format)
    {
    case FMT_DSD_MSB8: // Perform only copy if format match
        memcpy(out_, in, samples);
        break;
    case FMT_DSD_LSB8: // Perform copy and swap bits
        {
            uint8_t * inp = (uint8_t *)in;
            const uint8_t * endp = inp + samples;
            while (inp < endp)
            {   // Bit reverse DSD LSB Least Significant Bit first
                uint8_t val = *(inp++);
                *(out++) = ((reverse_tab[val & 0b1111] << 4) | reverse_tab[val >> 4]);
            }
        }
        break;

    case FMT_DSD_MSB16_LE:
        dsd_audio_convert_loop_in<FMT_DSD_MSB16_LE, uint16_t>(in, out, samples, channels);
        break;
    case FMT_DSD_MSB16_BE:
        dsd_audio_convert_loop_in<FMT_DSD_MSB16_BE, uint16_t>(in, out, samples, channels);
        break;

    case FMT_DSD_MSB32_LE:
        dsd_audio_convert_loop_in<FMT_DSD_MSB32_LE, uint32_t>(in, out, samples, channels);
        break;
    case FMT_DSD_MSB32_BE:
        dsd_audio_convert_loop_in<FMT_DSD_MSB32_BE, uint32_t>(in, out, samples, channels);
        break;
    }
}

// Convert from DSD audio to out
EXPORT void dsdaudio_to_out(const void * in_, void * out, int format, int samples, int channels)
{
    const uint8_t * in = (const uint8_t *)in_;
    switch (format)
    {
    case FMT_DSD_MSB8: // Perform only copy if format match
        memcpy(out, in_, samples);
        break;
    case FMT_DSD_LSB8: // Perform copy and swap bits
        {
            uint8_t * outp = (uint8_t *)out;
            const uint8_t * endp = in + samples;
            while (in < endp)
            {   // Bit reverse DSD LSB Least Significant Bit first
                uint8_t val = *(in++);
                *(outp++) = ((reverse_tab[val & 0b1111] << 4) | reverse_tab[val >> 4]);
            }
        }
        break;

    case FMT_DSD_MSB16_LE:
        dsd_audio_convert_loop_out<FMT_DSD_MSB16_LE, uint16_t>(in, out, samples, channels);
        break;
    case FMT_DSD_MSB16_BE:
        dsd_audio_convert_loop_out<FMT_DSD_MSB16_BE, uint16_t>(in, out, samples, channels);
        break;

    case FMT_DSD_MSB32_LE:
        dsd_audio_convert_loop_out<FMT_DSD_MSB32_LE, uint32_t>(in, out, samples, channels);
        break;
    case FMT_DSD_MSB32_BE:
        dsd_audio_convert_loop_out<FMT_DSD_MSB32_BE, uint32_t>(in, out, samples, channels);
        break;

    case FMT_DSD_MSB24_3LE:
        dsd_audio_convert_loop_out<FMT_DSD_MSB24_3LE, packed24_t, uint32_t>(in, out, samples, channels);
        break;
    case FMT_DSD_MSB24_3BE:
        dsd_audio_convert_loop_out<FMT_DSD_MSB24_3BE, packed24_t, uint32_t>(in, out, samples, channels);
        break;

    case FMT_U24_LE: // DoP padded to 4 bytes
    case FMT_S24_LE:
        dsd_audio_convert_loop_out<FMT_U24_LE, uint32_t>(in, out, samples, channels, true);
        break;
    case FMT_U24_BE:
    case FMT_S24_BE:
        dsd_audio_convert_loop_out<FMT_U24_BE, uint32_t>(in, out, samples, channels, true);
        break;

    case FMT_U32_LE: // DoP 4 bytes
    case FMT_S32_LE:
        dsd_audio_convert_loop_out<FMT_U32_LE, uint32_t>(in, out, samples, channels, true);
        break;
    case FMT_U32_BE:
    case FMT_S32_BE:
        dsd_audio_convert_loop_out<FMT_U32_BE, uint32_t>(in, out, samples, channels, true);
        break;

    case FMT_U24_3LE: // DoP 3 bytes
    case FMT_S24_3LE:
        dsd_audio_convert_loop_out<FMT_U24_3LE, packed24_t, uint32_t>(in, out, samples, channels, true);
        break;
    case FMT_U24_3BE:
    case FMT_S24_3BE:
        dsd_audio_convert_loop_out<FMT_U24_3BE, packed24_t, uint32_t>(in, out, samples, channels, true);
        break;
    }
}

// Audio amplify
template<class audio_float>
void audio_amplify(audio_float * data, int channels, int frames,
                          const float * factors)
{
    const audio_float * end = data + channels * frames;
    int channel;

    while (data < end)
    {
        for (channel = 0; channel < channels; channel++)
        {
            *data = *data * factors[channel];
            data++;
        }
    }
}

// Audio amplify for StereoVolume
template<class audio_float>
void audio_amplify(audio_float * data, int channels, int frames,
                          StereoVolume volume)
{
    if (channels < 1 || channels > AUD_MAX_CHANNELS)
        return;

    if (volume.left == 100 && volume.right == 100)
        return;

    float lfactor = 0, rfactor = 0;
    float factors[AUD_MAX_CHANNELS];

    if (volume.left > 0)
        lfactor =
            powf(10, (float)SW_VOLUME_RANGE * (volume.left - 100) / 100 / 20);
    if (volume.right > 0)
        rfactor =
            powf(10, (float)SW_VOLUME_RANGE * (volume.right - 100) / 100 / 20);

    if (channels == 2)
    {
        factors[0] = lfactor;
        factors[1] = rfactor;
    }
    else
    {
        for (int c = 0; c < channels; c++)
            factors[c] = aud::max(lfactor, rfactor);
    }

    audio_amplify<audio_float>(data, channels, frames, factors);
}

// Float32 version used in some Output Plugins for volume control
EXPORT void audio_amplify(float * data, int channels, int frames,
                          StereoVolume volume)
{
audio_amplify<float>(data, channels, frames, volume);
}

EXPORT void audio_amplify(float * data, int channels, int frames,
                          const float * factors)
{
audio_amplify<float>(data, channels, frames, factors);
}

// Float64 version
#ifdef DEF_AUDIO_FLOAT64
EXPORT void audio_amplify(double * data, int channels, int frames,
                          StereoVolume volume)
{
audio_amplify<double>(data, channels, frames, volume);
}

EXPORT void audio_amplify(double * data, int channels, int frames,
                          const float * factors)
{
audio_amplify<double>(data, channels, frames, factors);
}
#endif

/* linear approximation of y = sin(x) */
/* contributed by Anders Johansson */
EXPORT void audio_soft_clip(audio_sample * data, int samples)
{
    const audio_sample * end = data + samples;

    while (data < end)
    {
        audio_sample x = *data;
        audio_sample y = fabs(x);

        if (y <= 0.4)
            ; /* (0, 0.4) -> (0, 0.4) */
        else if (y <= 0.7)
            y = 0.8 * y + 0.08; /* (0.4, 0.7) -> (0.4, 0.64) */
        else if (y <= 1.0)
            y = 0.7 * y + 0.15; /* (0.7, 1) -> (0.64, 0.85) */
        else if (y <= 1.3)
            y = 0.4 * y + 0.45; /* (1, 1.3) -> (0.85, 0.97) */
        else if (y <= 1.5)
            y = 0.15 * y + 0.775; /* (1.3, 1.5) -> (0.97, 1) */
        else
            y = 1.0; /* (1.5, inf) -> 1 */

        *data++ = (x > 0) ? y : -y;
    }
}
