/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SM-T311 / lt013g WM1811 primary audio HAL for Android 11.
 * Copyright (C) 2026 Pawel Jarczak
 *
 * Design:
 * - stock-stable 44.1 kHz / stereo / S16 primary output on TAB3 PCM0;
 * - one 44.1 kHz hardware stereo capture, downmixed to Android mono;
 * - complete stock WM1811 routing for speaker, earpiece, jack 3/4-pole,
 *   all microphones, call AIF2, Bluetooth SCO AIF3, dock and AUX digital;
 * - no per-app DSP and no HAL resampler. AudioFlinger performs normal
 *   Android mixing, navigation ducking/mixing and format conversion;
 * - stock SM-T311 TinyHAL topology and calibrated WM1811 endpoint gains;
 * - cached ALSA controls and no redundant integer control writes.
 */

#define LOG_TAG "audio_hw_lt013g"

#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/audio_effect.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <system/audio.h>
#include <tinyalsa/asoundlib.h>

#include "t311_ril_audio.h"

#define LT_MAX_CARDS 8
#define LT_PCM_DEVICE 0
#define LT_MODEM_PCM_DEVICE 1
#define LT_BT_PCM_DEVICE 2
#define LT_VOICE_NB_RATE 8000
#define LT_VOICE_WB_RATE 16000
#define LT_VOICE_PERIOD_SIZE 160
#define LT_VOICE_PERIOD_COUNT 2
#define LT_RIL_PATH_EARPIECE 0x01
#define LT_RIL_PATH_HEADSET 0x02
#define LT_RIL_PATH_BLUETOOTH 0x04
#define LT_RIL_PATH_SPEAKER 0x06
#define LT_RIL_PATH_HEADPHONE 0x07
#define LT_RIL_PATH_BLUETOOTH_NO_NR 0x08
#define LT_RIL_SOUND_VOICE 0x01
#define LT_RIL_SOUND_SPEAKER 0x11
#define LT_RIL_SOUND_HEADSET 0x31
#define LT_RIL_SOUND_BTVOICE 0x41

#define LT_OUT_SCO_MASK (AUDIO_DEVICE_OUT_BLUETOOTH_SCO | \
                         AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET | \
                         AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT)

/* Android input device constants share AUDIO_DEVICE_BIT_IN.  Comparing the
 * raw values with bitwise AND makes every input look like every other input.
 * Strip the direction bit before testing the actual device bits. */
static inline audio_devices_t lt_input_payload(audio_devices_t devices)
{
    return devices & ~AUDIO_DEVICE_BIT_IN;
}

static inline bool lt_has_input_device(audio_devices_t devices,
                                       audio_devices_t target)
{
    return (lt_input_payload(devices) & lt_input_payload(target)) != 0;
}
#define LT_RATE 44100
#define LT_OUT_CHANNELS 2
#define LT_IN_HW_CHANNELS 2
#define LT_PERIOD_SIZE 1024
#define LT_PERIOD_COUNT 4
#define LT_CTL_CACHE_SIZE 512

struct lt_audio_device;

struct lt_ctl_cache_slot {
    const char *name;
    struct mixer_ctl *ctl;
};

struct lt_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;
    struct pcm_config config;
    struct pcm *pcm;
    struct lt_audio_device *dev;
    audio_devices_t devices;
    bool standby;
    uint64_t written_frames;
    uint64_t write_errors;
    char last_pcm_error[128];
};

struct lt_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;
    struct pcm_config config;
    struct pcm *pcm;
    struct lt_audio_device *dev;
    audio_devices_t devices;
    audio_source_t source;
    bool standby;
    int16_t *stereo_buffer;
    size_t stereo_buffer_frames;
    int64_t read_frames;
    int64_t capture_time_ns;
    uint64_t read_errors;
    unsigned int capture_channel;
    char last_pcm_error[128];
};

struct lt_audio_device {
    struct audio_hw_device hw_device;
    pthread_mutex_t lock;
    struct mixer *mixer;
    int card;
    struct lt_stream_out *active_output;
    struct lt_stream_in *active_input;
    bool mic_mute;
    audio_mode_t mode;
    float voice_volume;
    struct lt_ctl_cache_slot ctl_cache[LT_CTL_CACHE_SIZE];
    bool voip_mode;
    bool samsung_voip_mode;
    bool camcorder_mute;
    struct pcm *pcm_voice_rx;
    struct pcm *pcm_voice_tx;
    struct pcm *pcm_sco_rx;
    struct pcm *pcm_sco_tx;
    struct t311_ril_audio ril;
    audio_devices_t call_devices;
    bool in_call;
    bool wb_amr;
    bool bluetooth_nrec;
    bool bluetooth_wb;
    bool noise_suppression;
};


struct route_setting {
    const char *name;
    int value;
    const char *enum_value;
    bool required;
};

#define ROUTE_INT(name_, value_) \
    { .name = (name_), .value = (value_), .enum_value = NULL, .required = true }
#define ROUTE_OPT_INT(name_, value_) \
    { .name = (name_), .value = (value_), .enum_value = NULL, .required = false }
#define ROUTE_ENUM(name_, value_) \
    { .name = (name_), .value = 0, .enum_value = (value_), .required = true }

static unsigned int lt_ctl_hash(const char *name)
{
    unsigned int hash = 2166136261u;
    while (*name != '\0') {
        hash ^= (unsigned char)*name++;
        hash *= 16777619u;
    }
    return hash;
}

static int build_control_cache(struct lt_audio_device *adev)
{
    const unsigned int count = mixer_get_num_ctls(adev->mixer);
    for (unsigned int i = 0; i < count; ++i) {
        struct mixer_ctl *ctl = mixer_get_ctl(adev->mixer, i);
        if (ctl == NULL)
            continue;
        const char *name = mixer_ctl_get_name(ctl);
        if (name == NULL || *name == '\0')
            continue;
        unsigned int slot = lt_ctl_hash(name) & (LT_CTL_CACHE_SIZE - 1);
        for (unsigned int probe = 0; probe < LT_CTL_CACHE_SIZE; ++probe) {
            struct lt_ctl_cache_slot *entry = &adev->ctl_cache[(slot + probe) & (LT_CTL_CACHE_SIZE - 1)];
            if (entry->name == NULL) {
                entry->name = name;
                entry->ctl = ctl;
                break;
            }
        }
    }
    return 0;
}

static struct mixer_ctl *find_control(struct lt_audio_device *adev, const char *name)
{
    unsigned int slot = lt_ctl_hash(name) & (LT_CTL_CACHE_SIZE - 1);
    for (unsigned int probe = 0; probe < LT_CTL_CACHE_SIZE; ++probe) {
        struct lt_ctl_cache_slot *entry = &adev->ctl_cache[(slot + probe) & (LT_CTL_CACHE_SIZE - 1)];
        if (entry->name == NULL)
            return NULL;
        if (strcmp(entry->name, name) == 0)
            return entry->ctl;
    }
    return NULL;
}

static int set_all_int(struct lt_audio_device *adev, const char *name, int value)
{
    struct mixer_ctl *ctl = find_control(adev, name);
    if (ctl == NULL)
        return -ENOENT;

    const unsigned int count = mixer_ctl_get_num_values(ctl);
    if (count == 0)
        return -EINVAL;

    for (unsigned int index = 0; index < count; ++index) {
        if (mixer_ctl_set_value(ctl, index, value) != 0)
            return -EINVAL;
    }
    return 0;
}

static int set_enum(struct lt_audio_device *adev, const char *name, const char *value)
{
    struct mixer_ctl *ctl = find_control(adev, name);
    if (ctl == NULL)
        return -ENOENT;
    if (mixer_ctl_set_enum_by_string(ctl, value) != 0)
        return -EINVAL;
    return 0;
}

static int apply_route(struct lt_audio_device *adev,
                       const struct route_setting *route,
                       size_t count,
                       const char *route_name)
{
    int first_error = 0;

    for (size_t i = 0; i < count; ++i) {
        int ret;
        if (route[i].enum_value != NULL)
            ret = set_enum(adev, route[i].name, route[i].enum_value);
        else
            ret = set_all_int(adev, route[i].name, route[i].value);

        if (ret != 0) {
            if (route[i].required && first_error == 0)
                first_error = ret;
            if (!route[i].required)
                ALOGW("optional route control failed: route=%s control=%s",
                      route_name, route[i].name);
        }
    }

    if (first_error != 0)
        ALOGE("route failed: %s error=%d", route_name, first_error);
    return first_error;
}

/*
 * Exact media/capture topology taken from the stock SM-T311 TinyHAL:
 * - AIF1.1 -> DAC1;
 * - headphone muxes use DAC, not Mixer;
 * - speaker is the external mono path selected by the machine driver;
 * - main microphone is IN2L, headset microphone is IN1L, back microphone IN1R.
 *
 * The legacy configuration kept most internal codec routes static and only
 * toggled the physical DAPM endpoint switches.  V193 keeps that stock routing model. Voice/SCO use the original 160 x 2 hostless PCM geometry.
 */
static const struct route_setting core_route[] = {
    /* Exact static Core block from the user's T311XXSBQB2 firmware. */
    /* Exact integer values from stock tinyucm.conf: 0,0,0,1. */
    ROUTE_INT("AIF2DACL Source", 0),
    ROUTE_INT("AIF2DACR Source", 0),
    ROUTE_INT("AIF2ADCL Source", 0),
    ROUTE_INT("AIF2ADCR Source", 1),

    ROUTE_INT("IN1L ZC Switch", 0),
    ROUTE_INT("IN1R ZC Switch", 0),
    ROUTE_INT("IN2L ZC Switch", 0),
    ROUTE_INT("IN2R ZC Switch", 0),
    ROUTE_INT("Output ZC Switch", 0),
    ROUTE_INT("Speaker ZC Switch", 0),
    ROUTE_INT("Headphone ZC Switch", 0),

    ROUTE_INT("DAC1L Mixer AIF1.1 Switch", 1),
    ROUTE_INT("DAC1R Mixer AIF1.1 Switch", 1),
    ROUTE_INT("DAC1 Switch", 1),

    ROUTE_INT("AIF2DAC2L Mixer Left Sidetone Switch", 1),
    ROUTE_INT("AIF2DAC2L Mixer Right Sidetone Switch", 0),
    ROUTE_INT("AIF2DAC2R Mixer Left Sidetone Switch", 0),
    ROUTE_INT("AIF2DAC2R Mixer Right Sidetone Switch", 1),
    ROUTE_INT("DAC2 Left Sidetone Volume", 12),
    ROUTE_INT("DAC2 Right Sidetone Volume", 12),
    ROUTE_INT("DAC2 Switch", 1),

    ROUTE_ENUM("Left Headphone Mux", "DAC"),
    ROUTE_ENUM("Right Headphone Mux", "DAC"),

    ROUTE_INT("SPKL DAC1 Switch", 1),
    ROUTE_INT("SPKR DAC1 Switch", 1),
    ROUTE_INT("SPKL Boost SPKL Switch", 1),
    ROUTE_INT("SPKL Boost SPKR Switch", 0),
    ROUTE_INT("SPKR Boost SPKL Switch", 0),
    ROUTE_INT("SPKR Boost SPKR Switch", 1),
    ROUTE_INT("Speaker Mixer Volume", 3),

    ROUTE_INT("Left Output Mixer DAC Switch", 1),
    ROUTE_INT("Right Output Mixer DAC Switch", 1),
    ROUTE_INT("Earpiece Mixer Left Output Switch", 1),
    ROUTE_INT("Earpiece Mixer Right Output Switch", 1),
    ROUTE_INT("Earpiece Switch", 1),

    /* Stock TAB3 analogue dock and AUX/HDMI static lineout topology. */
    ROUTE_OPT_INT("LINEOUT2N Mixer Left Output Switch", 1),
    ROUTE_OPT_INT("LINEOUT2P Mixer Right Output Switch", 1),
    ROUTE_OPT_INT("LINEOUT1N Mixer Right Output Switch", 1),
    ROUTE_OPT_INT("LINEOUT1P Mixer Left Output Switch", 1),

    ROUTE_INT("AIF1ADC1L Mixer ADC/DMIC Switch", 1),
    ROUTE_INT("AIF1ADC1R Mixer ADC/DMIC Switch", 1),

    ROUTE_INT("IN1L PGA IN1LP Switch", 1),
    ROUTE_INT("IN1L PGA IN1LN Switch", 1),
    ROUTE_INT("MIXINL IN1L Volume", 1),
    ROUTE_INT("IN1L Switch", 1),

    ROUTE_INT("IN1R PGA IN1RP Switch", 1),
    ROUTE_INT("IN1R PGA IN1RN Switch", 1),
    ROUTE_INT("MIXINR IN1R Volume", 1),
    ROUTE_INT("IN1R Switch", 1),

    ROUTE_INT("IN2L PGA IN2LP Switch", 1),
    ROUTE_INT("IN2L PGA IN2LN Switch", 1),
    ROUTE_INT("MIXINL IN2L Volume", 1),
    ROUTE_INT("IN2L Switch", 1),

    ROUTE_ENUM("ADCL Mux", "DMIC"),
    ROUTE_ENUM("ADCL Mux", "ADC"),
    ROUTE_ENUM("ADCR Mux", "DMIC"),
    ROUTE_ENUM("ADCR Mux", "ADC"),
    ROUTE_INT("Sidetone HPF Switch", 0),
    ROUTE_ENUM("AIF3ADC Mux", "AIF1ADCDAT"),
};

static const struct route_setting outputs_off_route[] = {
    ROUTE_INT("SPK Switch", 0),
    ROUTE_INT("HP Switch", 0),
    ROUTE_INT("RCV Switch", 0),
    ROUTE_INT("LINE Switch", 0),
    ROUTE_INT("HDMI Switch", 0),
};

static const struct route_setting speaker_route[] = {
    ROUTE_INT("AIF1DAC1 Volume", 96),
    ROUTE_INT("AIF1 Boost Volume", 0),
    ROUTE_INT("DAC1 Volume", 96),
    ROUTE_INT("DAC1 Switch", 1),
    ROUTE_INT("SPKL DAC1 Volume", 1),
    ROUTE_INT("SPKR DAC1 Volume", 1),
    ROUTE_INT("SPKL DAC1 Switch", 1),
    ROUTE_INT("SPKR DAC1 Switch", 1),
    /* Required by the current mainline WM1811 codec path; V164 proved both. */
    ROUTE_INT("SPKL Output Switch", 1),
    ROUTE_INT("SPKR Output Switch", 1),
    ROUTE_INT("Speaker Switch", 1),
    ROUTE_INT("Speaker Mixer Volume", 3),
    ROUTE_INT("Speaker Boost Volume", 5),
    ROUTE_INT("Speaker Volume", 58),
    ROUTE_INT("AIF1DAC1 EQ Switch", 1),
    ROUTE_INT("AIF1DAC1 EQ1 Volume", 9),
    ROUTE_INT("AIF1DAC1 EQ2 Volume", 7),
    ROUTE_INT("AIF1DAC1 EQ3 Volume", 10),
    ROUTE_INT("AIF1DAC1 EQ4 Volume", 13),
    ROUTE_INT("AIF1DAC1 EQ5 Volume", 12),
    ROUTE_INT("AIF1DAC1 3D Stereo Switch", 0),
    ROUTE_INT("SPK Switch", 1),
};

static const struct route_setting speaker_off_route[] = {
    ROUTE_INT("SPK Switch", 0),
    ROUTE_INT("SPKL Output Switch", 0),
    ROUTE_INT("SPKR Output Switch", 0),
    ROUTE_INT("Speaker Mixer Volume", 0),
    ROUTE_INT("Speaker Boost Volume", 0),
    ROUTE_INT("Speaker Volume", 57),
};

static const struct route_setting headphone_route[] = {
    ROUTE_ENUM("Left Headphone Mux", "DAC"),
    ROUTE_ENUM("Right Headphone Mux", "DAC"),
    ROUTE_INT("Headphone ZC Switch", 0),
    ROUTE_INT("AIF1DAC1 Volume", 96),
    ROUTE_INT("AIF1 Boost Volume", 0),
    ROUTE_INT("DAC1 Volume", 96),
    ROUTE_INT("DAC1 Switch", 1),
    ROUTE_INT("Headphone Switch", 1),
    ROUTE_INT("Headphone Volume", 50),
    ROUTE_INT("AIF1DAC1 EQ Switch", 1),
    ROUTE_INT("AIF1DAC1 EQ1 Volume", 16),
    ROUTE_INT("AIF1DAC1 EQ2 Volume", 14),
    ROUTE_INT("AIF1DAC1 EQ3 Volume", 12),
    ROUTE_INT("AIF1DAC1 EQ4 Volume", 14),
    ROUTE_INT("AIF1DAC1 EQ5 Volume", 16),
    ROUTE_INT("AIF1DAC1 3D Stereo Switch", 1),
    ROUTE_INT("AIF1DAC1 3D Stereo Volume", 7),
    ROUTE_INT("HP Switch", 1),
};

static const struct route_setting headphone_off_route[] = {
    ROUTE_INT("HP Switch", 0),
    ROUTE_INT("Headphone Volume", 45),
};

static const struct route_setting earpiece_route[] = {
    ROUTE_INT("AIF1DAC1 Volume", 96),
    ROUTE_INT("AIF1 Boost Volume", 0),
    ROUTE_INT("DAC1 Volume", 96),
    ROUTE_INT("DAC1 Switch", 1),
    ROUTE_INT("Left Output Mixer DAC Volume", 7),
    ROUTE_INT("Right Output Mixer DAC Volume", 7),
    ROUTE_INT("Left Output Mixer DAC Switch", 1),
    ROUTE_INT("Right Output Mixer DAC Switch", 1),
    ROUTE_INT("Output Switch", 1),
    ROUTE_INT("Output Volume", 55),
    ROUTE_INT("Earpiece Volume", 1),
    ROUTE_INT("AIF1DAC1 EQ Switch", 0),
    ROUTE_INT("AIF1DAC1 3D Stereo Switch", 0),
    ROUTE_INT("Earpiece Switch", 1),
    ROUTE_INT("RCV Switch", 1),
};

static const struct route_setting earpiece_off_route[] = {
    ROUTE_INT("RCV Switch", 0),
    ROUTE_INT("Earpiece Switch", 0),
    ROUTE_INT("Output Volume", 57),
};

static const struct route_setting capture_off_route[] = {
    ROUTE_INT("Main Mic Switch", 0),
    ROUTE_INT("Sub Mic Switch", 0),
    ROUTE_INT("Headset Mic Switch", 0),
    ROUTE_INT("MIXINL IN2L Switch", 0),
    ROUTE_INT("MIXINR IN1R Switch", 0),
    ROUTE_INT("MIXINL IN1L Switch", 0),
    ROUTE_INT("AIF1ADC1 HPF Switch", 0),
};

static const struct route_setting builtin_mic_route[] = {
    ROUTE_INT("AIF1ADCL Source", 0),
    ROUTE_INT("AIF1ADCR Source", 0),
    ROUTE_INT("AIF1ADC1 HPF Mode", 1),
    ROUTE_INT("AIF1ADC1 HPF Switch", 1),
    ROUTE_INT("IN2L Volume", 25),
    ROUTE_INT("MIXINL IN2L Volume", 1),
    ROUTE_INT("AIF1ADC1 Volume", 96),
    ROUTE_INT("MIXINL IN2L Switch", 1),
    ROUTE_INT("Main Mic Switch", 1),
};

static const struct route_setting back_mic_route[] = {
    ROUTE_INT("AIF1ADCR Source", 1),
    ROUTE_INT("AIF1ADC1 HPF Mode", 1),
    ROUTE_INT("AIF1ADC1 HPF Switch", 1),
    ROUTE_INT("IN1R Volume", 25),
    ROUTE_INT("MIXINR IN1R Volume", 1),
    ROUTE_INT("AIF1ADC1 Volume", 96),
    ROUTE_INT("MIXINR IN1R Switch", 1),
    ROUTE_INT("Sub Mic Switch", 1),
};

static const struct route_setting headset_mic_route[] = {
    ROUTE_INT("AIF1ADCL Source", 0),
    ROUTE_INT("AIF1ADCR Source", 0),
    ROUTE_INT("AIF1ADC1 HPF Mode", 1),
    ROUTE_INT("AIF1ADC1 HPF Switch", 1),
    ROUTE_INT("IN1L Volume", 25),
    ROUTE_INT("MIXINL IN1L Volume", 1),
    ROUTE_INT("AIF1ADC1 Volume", 96),
    ROUTE_INT("MIXINL IN1L Switch", 1),
    ROUTE_INT("Headset Mic Switch", 1),
};


/* Exact verb/device routing from stock tinyucm.conf, adapted to Android 11. */
static const struct route_setting normal_verb_route[] = {
    ROUTE_ENUM("AIF2DAC Mux", "AIF3DACDAT"),
    ROUTE_INT("DAC1L Mixer AIF2 Switch", 0),
    ROUTE_INT("DAC1R Mixer AIF2 Switch", 0),
    ROUTE_INT("AIF2DAC2L Mixer AIF2 Switch", 0),
    ROUTE_INT("AIF2DAC2R Mixer AIF2 Switch", 0),
};

static const struct route_setting voicecall_verb_route[] = {
    ROUTE_ENUM("AIF2DAC Mux", "AIF2DACDAT"),
    ROUTE_INT("DAC1L Mixer AIF2 Switch", 1),
    ROUTE_INT("DAC1R Mixer AIF2 Switch", 1),
    ROUTE_INT("AIF2DAC Volume", 96),
    ROUTE_INT("AIF2 Boost Volume", 0),
    ROUTE_INT("AIF2ADC HPF Mode", 1),
    ROUTE_INT("AIF2ADC HPF Switch", 1),
};

static const struct route_setting call_main_mic_route[] = {
    ROUTE_INT("Main Mic Switch", 1),
    ROUTE_INT("AIF1ADCL Source", 0),
    ROUTE_INT("AIF1ADCR Source", 0),
    ROUTE_INT("MIXINL IN2L Switch", 1),
    ROUTE_INT("AIF1ADC1 HPF Mode", 1),
    ROUTE_INT("AIF1ADC1 HPF Switch", 1),
    ROUTE_INT("IN2L Volume", 11),
    ROUTE_INT("MIXINL IN2L Volume", 1),
    ROUTE_INT("AIF1ADC1 Volume", 96),
};

static const struct route_setting call_headset_mic_route[] = {
    ROUTE_INT("Headset Mic Switch", 1),
    ROUTE_INT("AIF1ADCL Source", 0),
    ROUTE_INT("AIF1ADCR Source", 0),
    ROUTE_INT("MIXINL IN1L Switch", 1),
    ROUTE_INT("AIF1ADC1 HPF Mode", 1),
    ROUTE_INT("AIF1ADC1 HPF Switch", 1),
    ROUTE_INT("IN1L Volume", 11),
    ROUTE_INT("MIXINL IN1L Volume", 1),
    ROUTE_INT("AIF1ADC1 Volume", 96),
};

static const struct route_setting noise_suppression_route[] = {
    ROUTE_INT("Sub Mic Switch", 1),
    ROUTE_INT("AIF1ADCR Source", 1),
    ROUTE_INT("MIXINR IN1R Switch", 1),
    ROUTE_INT("AIF1ADC1 HPF Mode", 1),
    ROUTE_INT("AIF1ADC1 HPF Switch", 1),
    ROUTE_INT("IN1R Volume", 11),
    ROUTE_INT("MIXINR IN1R Volume", 1),
    ROUTE_INT("AIF1ADC1 Volume", 96),
};


static const struct route_setting sco_output_route[] = {
    ROUTE_ENUM("AIF3ADC Mux", "AIF1ADCDAT"),
    ROUTE_ENUM("AIF2DAC Mux", "AIF3DACDAT"),
    ROUTE_INT("AIF2DAC2L Mixer AIF1.1 Switch", 1),
    ROUTE_INT("AIF2DAC2R Mixer AIF1.1 Switch", 1),
    ROUTE_INT("AIF1DAC1 Volume", 96),
    ROUTE_INT("AIF1 Boost Volume", 0),
    ROUTE_INT("AIF2DAC Volume", 96),
    ROUTE_INT("DAC2 Volume", 96),
    ROUTE_INT("AIF2ADC Volume", 96),
    ROUTE_INT("AIF1DAC1 EQ Switch", 0),
    ROUTE_INT("AIF1DAC1 3D Stereo Switch", 0),
};

static const struct route_setting sco_call_add_route[] = {
    ROUTE_INT("AIF2DAC2L Mixer AIF2 Switch", 1),
    ROUTE_INT("AIF2DAC2R Mixer AIF2 Switch", 1),
};

static const struct route_setting sco_output_off_route[] = {
    ROUTE_INT("AIF2DAC2L Mixer AIF1.1 Switch", 0),
    ROUTE_INT("AIF2DAC2R Mixer AIF1.1 Switch", 0),
    ROUTE_INT("AIF2DAC2L Mixer AIF2 Switch", 0),
    ROUTE_INT("AIF2DAC2R Mixer AIF2 Switch", 0),
};

static const struct route_setting sco_input_off_route[] = {
    ROUTE_INT("AIF1ADC1L Mixer AIF2 Switch", 0),
    ROUTE_INT("AIF1ADC1R Mixer AIF2 Switch", 0),
    ROUTE_ENUM("AIF2ADC Mux", "AIF2ADCDAT"),
};

static const struct route_setting sco_input_route[] = {
    ROUTE_ENUM("AIF2ADC Mux", "AIF3DACDAT"),
    ROUTE_INT("AIF1ADCL Source", 0),
    ROUTE_INT("AIF1ADCR Source", 1),
    ROUTE_INT("AIF1ADC1L Mixer ADC/DMIC Switch", 0),
    ROUTE_INT("AIF1ADC1R Mixer ADC/DMIC Switch", 0),
    ROUTE_INT("AIF1ADC1L Mixer AIF2 Switch", 1),
    ROUTE_INT("AIF1ADC1R Mixer AIF2 Switch", 1),
    ROUTE_INT("AIF1ADC1 Volume", 96),
    ROUTE_INT("AIF2ADC Volume", 96),
    ROUTE_INT("AIF2DAC Volume", 96),
};

/* Stock voice-call recording modifiers.  They expose uplink, downlink or
 * both paths on AIF1 capture without opening the modem PCM directly. */
static const struct route_setting mic_record_mix_route[] = {
    ROUTE_INT("AIF1ADC1L Mixer ADC/DMIC Switch", 1),
    ROUTE_INT("AIF1ADC1R Mixer ADC/DMIC Switch", 1),
    ROUTE_INT("AIF1ADC1L Mixer AIF2 Switch", 0),
    ROUTE_INT("AIF1ADC1R Mixer AIF2 Switch", 0),
};

static const struct route_setting downlink_record_mix_route[] = {
    ROUTE_INT("AIF1ADC1L Mixer ADC/DMIC Switch", 0),
    ROUTE_INT("AIF1ADC1R Mixer ADC/DMIC Switch", 0),
    ROUTE_INT("AIF1ADC1L Mixer AIF2 Switch", 1),
    ROUTE_INT("AIF1ADC1R Mixer AIF2 Switch", 1),
};

static const struct route_setting voicecall_record_mix_route[] = {
    ROUTE_INT("AIF1ADC1L Mixer ADC/DMIC Switch", 1),
    ROUTE_INT("AIF1ADC1R Mixer ADC/DMIC Switch", 1),
    ROUTE_INT("AIF1ADC1L Mixer AIF2 Switch", 1),
    ROUTE_INT("AIF1ADC1R Mixer AIF2 Switch", 1),
};

static const struct route_setting analogue_dock_route[] = {
    ROUTE_OPT_INT("LINEOUT2N Switch", 1),
    ROUTE_OPT_INT("LINEOUT2P Switch", 1),
    ROUTE_OPT_INT("LINEOUT2N Mixer Left Output Switch", 1),
    ROUTE_OPT_INT("LINEOUT2N Mixer Right Output Switch", 0),
    ROUTE_OPT_INT("LINEOUT2P Mixer Right Output Switch", 1),
    ROUTE_OPT_INT("LineoutSwitch Mode", 1),
    ROUTE_INT("AIF1DAC1 Volume", 96),
    ROUTE_INT("AIF1 Boost Volume", 0),
    ROUTE_INT("DAC1 Volume", 96),
    ROUTE_INT("Left Output Mixer DAC Volume", 7),
    ROUTE_INT("Right Output Mixer DAC Volume", 7),
    ROUTE_INT("Output Volume", 56),
    ROUTE_OPT_INT("LINEOUT2 Volume", 1),
    ROUTE_INT("AIF1DAC1 EQ Switch", 0),
    ROUTE_INT("AIF1DAC1 3D Stereo Switch", 0),
    ROUTE_INT("LINE Switch", 1),
};

static const struct route_setting aux_digital_route[] = {
    ROUTE_OPT_INT("LINEOUT1N Switch", 1),
    ROUTE_OPT_INT("LINEOUT1P Switch", 1),
    ROUTE_INT("HDMI Switch", 1),
    ROUTE_INT("AIF1DAC1 Volume", 96),
    ROUTE_INT("AIF1DAC1 EQ Switch", 0),
    ROUTE_INT("AIF1DAC1 3D Stereo Switch", 0),
};

static bool is_sco_output(audio_devices_t devices)
{
    return (devices & LT_OUT_SCO_MASK) != 0;
}

static bool is_sco_input(audio_devices_t devices)
{
    return lt_has_input_device(devices, AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET);
}

static bool is_voice_call_input(audio_devices_t devices)
{
    return lt_has_input_device(devices, AUDIO_DEVICE_IN_VOICE_CALL);
}

static bool is_fm_input(audio_devices_t devices)
{
    return lt_has_input_device(devices, AUDIO_DEVICE_IN_FM_TUNER);
}

static int apply_verb_locked(struct lt_audio_device *adev)
{
    const bool incall = adev->mode == AUDIO_MODE_IN_CALL;
    return apply_route(adev,
                       incall ? voicecall_verb_route : normal_verb_route,
                       incall ? sizeof(voicecall_verb_route) / sizeof(voicecall_verb_route[0])
                              : sizeof(normal_verb_route) / sizeof(normal_verb_route[0]),
                       incall ? "voicecall-verb" : "normal-verb");
}

static int configure_input_gain_locked(struct lt_stream_in *in,
                                       bool back, bool headset)
{
    struct lt_audio_device *adev = in->dev;

    /* Working Android 7.1.2 TinyHAL used one calibrated normal-capture
     * profile for all Android sources.  Source-specific gain guesses caused
     * the stock route to be overwritten after it had been applied. */
    if (set_all_int(adev, "AIF1ADC1 HPF Mode", 1) != 0 ||
        set_all_int(adev, "AIF1ADC1 HPF Switch", 1) != 0 ||
        set_all_int(adev, "AIF1ADC1 Volume", 96) != 0)
        return -EINVAL;

    if (headset) {
        if (set_all_int(adev, "IN1L Volume", 25) != 0 ||
            set_all_int(adev, "MIXINL IN1L Volume", 1) != 0)
            return -EINVAL;
    } else if (back) {
        if (set_all_int(adev, "IN1R Volume", 25) != 0 ||
            set_all_int(adev, "MIXINR IN1R Volume", 1) != 0)
            return -EINVAL;
    } else {
        if (set_all_int(adev, "IN2L Volume", 25) != 0 ||
            set_all_int(adev, "MIXINL IN2L Volume", 1) != 0)
            return -EINVAL;
    }
    return 0;
}

static int init_core_locked(struct lt_audio_device *adev)
{
    int ret = apply_route(adev, core_route,
                          sizeof(core_route) / sizeof(core_route[0]), "core");
    if (apply_route(adev, outputs_off_route,
                    sizeof(outputs_off_route) / sizeof(outputs_off_route[0]),
                    "outputs-off") != 0 && ret == 0)
        ret = -EINVAL;
    if (apply_route(adev, capture_off_route,
                    sizeof(capture_off_route) / sizeof(capture_off_route[0]),
                    "capture-off") != 0 && ret == 0)
        ret = -EINVAL;
    if (apply_verb_locked(adev) != 0 && ret == 0)
        ret = -EINVAL;
    return ret;
}

static void disable_playback_locked(struct lt_audio_device *adev)
{
    (void)apply_route(adev, speaker_off_route,
                      sizeof(speaker_off_route) / sizeof(speaker_off_route[0]),
                      "speaker-off");
    (void)apply_route(adev, headphone_off_route,
                      sizeof(headphone_off_route) / sizeof(headphone_off_route[0]),
                      "headphone-off");
    (void)apply_route(adev, earpiece_off_route,
                      sizeof(earpiece_off_route) / sizeof(earpiece_off_route[0]),
                      "earpiece-off");
    (void)apply_route(adev, sco_output_off_route,
                      sizeof(sco_output_off_route) / sizeof(sco_output_off_route[0]),
                      "sco-output-off");
    (void)apply_route(adev, outputs_off_route,
                      sizeof(outputs_off_route) / sizeof(outputs_off_route[0]),
                      "outputs-off");
}

static int apply_physical_output_gain_locked(struct lt_audio_device *adev,
                                             audio_devices_t devices)
{
    const bool wired = (devices & (AUDIO_DEVICE_OUT_WIRED_HEADSET |
                                   AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) != 0;
    const bool speaker = (devices & AUDIO_DEVICE_OUT_SPEAKER) != 0;
    const bool dual = wired && speaker;

    /* Match the working 7.1.2 TinyHAL: endpoint calibration is fixed in the
     * codec route.  Android software volume and modem SetCallVolume handle
     * user volume; do not retune WM1811 gains on every mode transition. */
    if (dual) {
        int ret = apply_route(adev, speaker_route,
                              sizeof(speaker_route) / sizeof(speaker_route[0]),
                              "dual-speaker");
        if (ret == 0)
            ret = apply_route(adev, headphone_route,
                              sizeof(headphone_route) / sizeof(headphone_route[0]),
                              "dual-headphone");
        return ret;
    }

    if (wired)
        return apply_route(adev, headphone_route,
                           sizeof(headphone_route) / sizeof(headphone_route[0]),
                           "headphone");

    if (devices & AUDIO_DEVICE_OUT_EARPIECE)
        return apply_route(adev, earpiece_route,
                           sizeof(earpiece_route) / sizeof(earpiece_route[0]),
                           "earpiece");

    if (devices & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET)
        return apply_route(adev, analogue_dock_route,
                           sizeof(analogue_dock_route) / sizeof(analogue_dock_route[0]),
                           "analogue-dock");

    if (devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
        return apply_route(adev, aux_digital_route,
                           sizeof(aux_digital_route) / sizeof(aux_digital_route[0]),
                           "aux-digital");

    return apply_route(adev, speaker_route,
                       sizeof(speaker_route) / sizeof(speaker_route[0]),
                       "speaker");
}

static void disable_capture_locked(struct lt_audio_device *adev);
static int route_input_locked(struct lt_stream_in *in);

static audio_devices_t call_devices_locked(struct lt_audio_device *adev)
{
    if (adev->call_devices != AUDIO_DEVICE_NONE)
        return adev->call_devices;
    if (adev->active_output != NULL && adev->active_output->devices != AUDIO_DEVICE_NONE)
        return adev->active_output->devices;
    return AUDIO_DEVICE_OUT_EARPIECE;
}

static int call_ril_path_locked(struct lt_audio_device *adev, audio_devices_t devices)
{
    if (is_sco_output(devices))
        return adev->bluetooth_nrec ? LT_RIL_PATH_BLUETOOTH : LT_RIL_PATH_BLUETOOTH_NO_NR;
    if (devices & AUDIO_DEVICE_OUT_WIRED_HEADSET)
        return LT_RIL_PATH_HEADSET;
    if (devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
        return LT_RIL_PATH_HEADPHONE;
    if (devices & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET |
                   AUDIO_DEVICE_OUT_AUX_DIGITAL))
        return LT_RIL_PATH_SPEAKER;
    return LT_RIL_PATH_EARPIECE;
}

static int call_ril_sound_type_locked(struct lt_audio_device *adev)
{
    audio_devices_t devices = call_devices_locked(adev);
    if (is_sco_output(devices))
        return LT_RIL_SOUND_BTVOICE;
    if (devices & (AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE))
        return LT_RIL_SOUND_HEADSET;
    if (devices & AUDIO_DEVICE_OUT_SPEAKER)
        return LT_RIL_SOUND_SPEAKER;
    return LT_RIL_SOUND_VOICE;
}

static struct pcm_config voice_pcm_config(bool wb)
{
    struct pcm_config config;
    memset(&config, 0, sizeof(config));
    config.channels = 2;
    config.rate = wb ? LT_VOICE_WB_RATE : LT_VOICE_NB_RATE;
    config.period_size = LT_VOICE_PERIOD_SIZE;
    config.period_count = LT_VOICE_PERIOD_COUNT;
    config.format = PCM_FORMAT_S16_LE;
    return config;
}

static void close_sco_pcm_locked(struct lt_audio_device *adev)
{
    if (adev->pcm_sco_rx != NULL) {
        pcm_stop(adev->pcm_sco_rx);
        pcm_close(adev->pcm_sco_rx);
        adev->pcm_sco_rx = NULL;
    }
    if (adev->pcm_sco_tx != NULL) {
        pcm_stop(adev->pcm_sco_tx);
        pcm_close(adev->pcm_sco_tx);
        adev->pcm_sco_tx = NULL;
    }
}

static int open_sco_pcm_locked(struct lt_audio_device *adev)
{
    if (adev->pcm_sco_rx != NULL && adev->pcm_sco_tx != NULL)
        return 0;

    close_sco_pcm_locked(adev);
    struct pcm_config config = voice_pcm_config(adev->bluetooth_wb);
    adev->pcm_sco_rx = pcm_open(adev->card, LT_BT_PCM_DEVICE, PCM_OUT, &config);
    if (adev->pcm_sco_rx == NULL || !pcm_is_ready(adev->pcm_sco_rx))
        goto fail;
    adev->pcm_sco_tx = pcm_open(adev->card, LT_BT_PCM_DEVICE, PCM_IN, &config);
    if (adev->pcm_sco_tx == NULL || !pcm_is_ready(adev->pcm_sco_tx))
        goto fail;
    /* Stock SM-T311 HAL starts both hostless directions independently and
     * does not treat the empty-playback EPIPE from pcm_start(RX) as fatal.
     * Never short-circuit TX: AIF2/AIF3 capture start is required to bring
     * the external CP/BT path up even when RX remains PREPARED. */
    int rx_start = pcm_start(adev->pcm_sco_rx);
    int tx_start = pcm_start(adev->pcm_sco_tx);
    if (rx_start != 0)
        ALOGW("V193 SCO PCM2 RX start nonfatal ret=%d err=%s",
              rx_start, pcm_get_error(adev->pcm_sco_rx));
    if (tx_start != 0)
        ALOGW("V193 SCO PCM2 TX start nonfatal ret=%d err=%s",
              tx_start, pcm_get_error(adev->pcm_sco_tx));
    ALOGI("V193 SCO PCM2 start-results rx=%d tx=%d rate=%u",
          rx_start, tx_start, config.rate);
    return 0;

fail:
    ALOGE("V193 SCO PCM2 failed: rx=%s tx=%s",
          adev->pcm_sco_rx ? pcm_get_error(adev->pcm_sco_rx) : "null",
          adev->pcm_sco_tx ? pcm_get_error(adev->pcm_sco_tx) : "null");
    close_sco_pcm_locked(adev);
    return -ENODEV;
}

static void close_voice_pcm_locked(struct lt_audio_device *adev)
{
    close_sco_pcm_locked(adev);
    if (adev->pcm_voice_rx != NULL) {
        pcm_stop(adev->pcm_voice_rx);
        pcm_close(adev->pcm_voice_rx);
        adev->pcm_voice_rx = NULL;
    }
    if (adev->pcm_voice_tx != NULL) {
        pcm_stop(adev->pcm_voice_tx);
        pcm_close(adev->pcm_voice_tx);
        adev->pcm_voice_tx = NULL;
    }
}

static int open_voice_pcm_locked(struct lt_audio_device *adev)
{
    close_voice_pcm_locked(adev);
    struct pcm_config config = voice_pcm_config(adev->wb_amr);
    adev->pcm_voice_rx = pcm_open(adev->card, LT_MODEM_PCM_DEVICE, PCM_OUT, &config);
    if (adev->pcm_voice_rx == NULL || !pcm_is_ready(adev->pcm_voice_rx))
        goto fail;
    adev->pcm_voice_tx = pcm_open(adev->card, LT_MODEM_PCM_DEVICE, PCM_IN, &config);
    if (adev->pcm_voice_tx == NULL || !pcm_is_ready(adev->pcm_voice_tx))
        goto fail;
    /* Exact stock behavior matters here: pcm_start(RX) on this hostless
     * telephony playback can report EPIPE because userspace never feeds the
     * ALSA playback ring.  The old working HAL ignored that return and still
     * started TX.  Do the same, but retain explicit diagnostics. */
    int rx_start = pcm_start(adev->pcm_voice_rx);
    int tx_start = pcm_start(adev->pcm_voice_tx);
    if (rx_start != 0)
        ALOGW("V193 CALL PCM1 RX start nonfatal ret=%d err=%s",
              rx_start, pcm_get_error(adev->pcm_voice_rx));
    if (tx_start != 0)
        ALOGW("V193 CALL PCM1 TX start nonfatal ret=%d err=%s",
              tx_start, pcm_get_error(adev->pcm_voice_tx));
    ALOGI("V193 CALL PCM1 start-results rx=%d tx=%d rate=%u",
          rx_start, tx_start, config.rate);
    if (is_sco_output(call_devices_locked(adev)) && open_sco_pcm_locked(adev) != 0)
        goto fail;
    ALOGI("V193 CALL PCM1 active rate=%u", config.rate);
    return 0;

fail:
    ALOGE("V193 CALL PCM1 failed: rx=%s tx=%s",
          adev->pcm_voice_rx ? pcm_get_error(adev->pcm_voice_rx) : "null",
          adev->pcm_voice_tx ? pcm_get_error(adev->pcm_voice_tx) : "null");
    close_voice_pcm_locked(adev);
    return -ENODEV;
}

static int route_call_locked(struct lt_audio_device *adev)
{
    audio_devices_t devices = call_devices_locked(adev);
    ALOGI("V193 CALL stage=route-begin devices=0x%x wb=%d", devices, adev->wb_amr);
    disable_playback_locked(adev);
    disable_capture_locked(adev);
    ALOGI("V193 CALL stage=routes-cleared");
    int ret = apply_route(adev, voicecall_verb_route,
                          sizeof(voicecall_verb_route) / sizeof(voicecall_verb_route[0]),
                          "voicecall-verb");
    ALOGI("V193 CALL stage=voice-verb ret=%d", ret);
    if (ret != 0)
        return ret;

    const bool sco_call = is_sco_output(devices);
    const bool headset_call = (devices & AUDIO_DEVICE_OUT_WIRED_HEADSET) != 0;
    const bool two_mic = adev->noise_suppression && !sco_call && !headset_call;

    if (sco_call) {
        ret = apply_route(adev, sco_output_route,
                          sizeof(sco_output_route) / sizeof(sco_output_route[0]),
                          "sco-output");
        if (ret == 0)
            ret = apply_route(adev, sco_call_add_route,
                              sizeof(sco_call_add_route) / sizeof(sco_call_add_route[0]),
                              "sco-call-add");
        if (ret == 0)
            ret = apply_route(adev, sco_input_route,
                              sizeof(sco_input_route) / sizeof(sco_input_route[0]),
                              "sco-input");
        if (ret == 0 && adev->in_call && adev->pcm_voice_rx != NULL &&
            adev->pcm_voice_tx != NULL)
            ret = open_sco_pcm_locked(adev);
    } else {
        close_sco_pcm_locked(adev);
        ret = apply_physical_output_gain_locked(adev, devices);
        if (ret == 0 && (devices & AUDIO_DEVICE_OUT_WIRED_HEADSET))
            ret = apply_route(adev, call_headset_mic_route,
                              sizeof(call_headset_mic_route) / sizeof(call_headset_mic_route[0]),
                              "call-headset-mic");
        else if (ret == 0)
            ret = apply_route(adev, call_main_mic_route,
                              sizeof(call_main_mic_route) / sizeof(call_main_mic_route[0]),
                              "call-main-mic");
        if (ret == 0 && two_mic)
            ret = apply_route(adev, noise_suppression_route,
                              sizeof(noise_suppression_route) / sizeof(noise_suppression_route[0]),
                              "noise-suppression");
    }

    ALOGI("V193 CALL stage=physical-route ret=%d", ret);

    if (ret == 0 && adev->active_input != NULL && !adev->active_input->standby) {
        pthread_mutex_lock(&adev->active_input->lock);
        ret = route_input_locked(adev->active_input);
        pthread_mutex_unlock(&adev->active_input->lock);
    }

    if (ret == 0) {
        int ril_ret = t311_ril_audio_set_path(&adev->ril, call_ril_path_locked(adev, devices));
        if (ril_ret == 0)
            ril_ret = t311_ril_audio_set_two_mic(&adev->ril, two_mic);
        if (ril_ret == 0)
            ril_ret = t311_ril_audio_set_volume(&adev->ril, call_ril_sound_type_locked(adev),
                                                (int)(adev->voice_volume * 5.0f));
        if (ril_ret != 0)
            ALOGW("V191 RIL call route deferred ret=%d", ril_ret);
    }
    ALOGI("V193 CALL route devices=0x%x wb=%d sco_wb=%d nrec=%d twomic=%d ret=%d",
          devices, adev->wb_amr, adev->bluetooth_wb, adev->bluetooth_nrec, two_mic, ret);
    return ret;
}

static void wb_amr_changed(void *data, int enabled)
{
    struct lt_audio_device *adev = data;
    pthread_mutex_lock(&adev->lock);
    bool wb = enabled != 0;
    if (adev->wb_amr != wb) {
        bool old_wb = adev->wb_amr;
        adev->wb_amr = wb;
        if (adev->in_call) {
            close_voice_pcm_locked(adev);
            int ret = route_call_locked(adev);
            if (ret == 0)
                ret = open_voice_pcm_locked(adev);
            if (ret != 0) {
                ALOGE("V191 WB-AMR reopen failed wb=%d ret=%d, restoring wb=%d",
                      wb, ret, old_wb);
                adev->wb_amr = old_wb;
                close_voice_pcm_locked(adev);
                if (route_call_locked(adev) == 0)
                    (void)open_voice_pcm_locked(adev);
            } else {
                ALOGI("V191 WB-AMR applied wb=%d rate=%u", wb,
                      wb ? LT_VOICE_WB_RATE : LT_VOICE_NB_RATE);
            }
        }
    }
    pthread_mutex_unlock(&adev->lock);
}

static int route_output_locked(struct lt_audio_device *adev, audio_devices_t devices)
{
    if (adev->in_call) {
        adev->call_devices = devices;
        return route_call_locked(adev);
    }
    disable_playback_locked(adev);
    /* Core is programmed once at HAL open.  Reapplying dozens of controls on
     * every stream start caused the visible start-up stall. */
    int ret = apply_verb_locked(adev);
    if (ret != 0)
        return ret;

    if (is_sco_output(devices)) {
        ret = apply_route(adev, sco_output_route,
                          sizeof(sco_output_route) / sizeof(sco_output_route[0]),
                          "sco-output");
        if (ret == 0 && adev->mode == AUDIO_MODE_IN_CALL)
            ret = apply_route(adev, sco_call_add_route,
                              sizeof(sco_call_add_route) / sizeof(sco_call_add_route[0]),
                              "sco-call-add");
        if (ret == 0)
            ret = open_sco_pcm_locked(adev);
        ALOGI("V191 output route: bluetooth-sco mode=%d wb=%d card=%d ret=%d",
              adev->mode, adev->bluetooth_wb, adev->card, ret);
    } else {
        if (!adev->in_call &&
            (adev->active_input == NULL || adev->active_input->standby ||
             !is_sco_input(adev->active_input->devices)))
            close_sco_pcm_locked(adev);
        ret = apply_physical_output_gain_locked(adev, devices);
        ALOGI("V191 output route: physical devices=0x%x mode=%d card=%d",
              devices, adev->mode, adev->card);
    }
    return ret;
}

static void disable_capture_locked(struct lt_audio_device *adev)
{
    (void)apply_route(adev, sco_input_off_route,
                      sizeof(sco_input_off_route) / sizeof(sco_input_off_route[0]),
                      "sco-input-off");
    (void)apply_route(adev, capture_off_route,
                      sizeof(capture_off_route) / sizeof(capture_off_route[0]),
                      "capture-off");
}

static int route_input_locked(struct lt_stream_in *in)
{
    struct lt_audio_device *adev = in->dev;
    const audio_devices_t devices = in->devices;
    const bool sco = is_sco_input(devices);
    const bool voice_device = is_voice_call_input(devices);
    const bool fm = is_fm_input(devices);
    const bool headset = lt_has_input_device(devices, AUDIO_DEVICE_IN_WIRED_HEADSET);
    bool back = lt_has_input_device(devices, AUDIO_DEVICE_IN_BACK_MIC);

    if (!headset && !sco && !voice_device && !fm &&
        in->source == AUDIO_SOURCE_CAMCORDER)
        back = true;

    disable_capture_locked(adev);
    /* Core is already initialized.  Only the active verb and endpoint delta
     * are changed here, keeping capture start deterministic and fast. */
    int ret = apply_verb_locked(adev);
    if (ret != 0)
        return ret;

    if (adev->in_call) {
        audio_devices_t call_devices = call_devices_locked(adev);
        if (is_sco_output(call_devices)) {
            ret = apply_route(adev, sco_input_route,
                              sizeof(sco_input_route) / sizeof(sco_input_route[0]),
                              "call-sco-input");
        } else if (call_devices & AUDIO_DEVICE_OUT_WIRED_HEADSET) {
            ret = apply_route(adev, call_headset_mic_route,
                              sizeof(call_headset_mic_route) / sizeof(call_headset_mic_route[0]),
                              "call-headset-mic");
        } else {
            ret = apply_route(adev, call_main_mic_route,
                              sizeof(call_main_mic_route) / sizeof(call_main_mic_route[0]),
                              "call-main-mic");
            if (ret == 0 && adev->noise_suppression)
                ret = apply_route(adev, noise_suppression_route,
                                  sizeof(noise_suppression_route) / sizeof(noise_suppression_route[0]),
                                  "noise-suppression");
        }
        if (ret != 0)
            return ret;
    }

    if (sco) {
        ret = apply_route(adev, sco_input_route,
                          sizeof(sco_input_route) / sizeof(sco_input_route[0]),
                          "sco-input");
        if (ret == 0 && !adev->in_call)
            ret = open_sco_pcm_locked(adev);
        in->capture_channel = 0;
        ALOGI("V191 input route: bluetooth-sco source=%d devices=0x%x wb=%d card=%d ret=%d",
              in->source, devices, adev->bluetooth_wb, adev->card, ret);
        return ret;
    }

    if (!adev->in_call &&
        (adev->active_output == NULL || adev->active_output->standby ||
         !is_sco_output(adev->active_output->devices)))
        close_sco_pcm_locked(adev);

    if (!adev->in_call && !voice_device && !fm &&
        in->source != AUDIO_SOURCE_VOICE_DOWNLINK) {
        if (headset) {
            ret = apply_route(adev, headset_mic_route,
                              sizeof(headset_mic_route) / sizeof(headset_mic_route[0]),
                              "headset-mic");
            if (ret == 0)
                ret = configure_input_gain_locked(in, false, true);
            in->capture_channel = 0;
            ALOGI("V191 input route: headset source=%d devices=0x%x card=%d",
                  in->source, devices, adev->card);
        } else if (back) {
            ret = apply_route(adev, back_mic_route,
                              sizeof(back_mic_route) / sizeof(back_mic_route[0]),
                              "back-mic");
            if (ret == 0)
                ret = configure_input_gain_locked(in, true, false);
            in->capture_channel = 1;
            ALOGI("V191 input route: back source=%d devices=0x%x card=%d",
                  in->source, devices, adev->card);
        } else {
            ret = apply_route(adev, builtin_mic_route,
                              sizeof(builtin_mic_route) / sizeof(builtin_mic_route[0]),
                              "builtin-mic");
            if (ret == 0)
                ret = configure_input_gain_locked(in, false, false);
            if (ret == 0 && adev->noise_suppression)
                ret = apply_route(adev, noise_suppression_route,
                                  sizeof(noise_suppression_route) / sizeof(noise_suppression_route[0]),
                                  "noise-suppression");
            in->capture_channel = 0;
            ALOGI("V191 input route: builtin source=%d devices=0x%x card=%d",
                  in->source, devices, adev->card);
        }
        if (ret != 0)
            return ret;
    }

    if (voice_device || in->source == AUDIO_SOURCE_VOICE_CALL) {
        ret = apply_route(adev, voicecall_record_mix_route,
                          sizeof(voicecall_record_mix_route) /
                          sizeof(voicecall_record_mix_route[0]),
                          "voicecall-record");
        ALOGI("V191 input mix: voice-call both source=%d", in->source);
    } else if (fm || in->source == AUDIO_SOURCE_VOICE_DOWNLINK) {
        ret = apply_route(adev, downlink_record_mix_route,
                          sizeof(downlink_record_mix_route) /
                          sizeof(downlink_record_mix_route[0]),
                          fm ? "fm-downlink-record" : "voice-downlink-record");
        in->capture_channel = 0;
        ALOGI("V191 input mix: downlink source=%d fm=%d", in->source, fm);
    } else {
        ret = apply_route(adev, mic_record_mix_route,
                          sizeof(mic_record_mix_route) /
                          sizeof(mic_record_mix_route[0]),
                          "mic-record");
    }
    return ret;
}

static int find_tab3_card(void)
{
    FILE *fp = fopen("/proc/asound/cards", "r");
    if (fp == NULL) {
        ALOGE("cannot open /proc/asound/cards: %s", strerror(errno));
        return -1;
    }

    char line[256];
    int result = -1;

    while (fgets(line, sizeof(line), fp) != NULL) {
        int card = -1;
        char id[64] = {0};

        if (sscanf(line, " %d [%63[^]]", &card, id) == 2) {
            if (strstr(id, "TAB3") != NULL) {
                result = card;
                break;
            }
        }
    }

    fclose(fp);
    return result;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    const struct lt_stream_out *out = (const struct lt_stream_out *)stream;
    return out->config.rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    const struct lt_stream_out *out = (const struct lt_stream_out *)stream;
    return out->config.period_size * audio_stream_out_frame_size(&out->stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int start_output_stream_locked(struct lt_stream_out *out)
{
    struct lt_audio_device *adev = out->dev;
    int ret = route_output_locked(adev, out->devices);
    if (ret != 0)
        return ret;

    out->pcm = pcm_open(adev->card, LT_PCM_DEVICE, PCM_OUT | PCM_MONOTONIC, &out->config);
    if (out->pcm == NULL || !pcm_is_ready(out->pcm)) {
        ALOGE("cannot open playback PCM: %s", out->pcm ? pcm_get_error(out->pcm) : "null");
        if (out->pcm != NULL) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        if (adev->in_call)
            (void)route_call_locked(adev);
        else
            disable_playback_locked(adev);
        return -ENODEV;
    }
    adev->active_output = out;
    out->standby = false;
    return 0;
}

static int do_output_standby_locked(struct lt_stream_out *out)
{
    if (!out->standby) {
        if (out->pcm != NULL) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        if (out->dev->active_output == out)
            out->dev->active_output = NULL;
        if (out->dev->in_call)
            (void)route_call_locked(out->dev);
        else {
            disable_playback_locked(out->dev);
            if (out->dev->active_input == NULL || out->dev->active_input->standby ||
                !is_sco_input(out->dev->active_input->devices))
                close_sco_pcm_locked(out->dev);
        }
        out->standby = true;
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct lt_stream_out *out = (struct lt_stream_out *)stream;
    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    int ret = do_output_standby_locked(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return ret;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    const struct lt_stream_out *out = (const struct lt_stream_out *)stream;
    dprintf(fd, "lt013g V191 output: standby=%d devices=0x%x written=%" PRIu64
            " errors=%" PRIu64 " last_error=%s\n",
            out->standby, out->devices, out->written_frames,
            out->write_errors,
            out->last_pcm_error[0] ? out->last_pcm_error : "none");
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct lt_stream_out *out = (struct lt_stream_out *)stream;
    struct str_parms *parms = str_parms_create_str(kvpairs);
    char value[32];
    int ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        audio_devices_t devices = (audio_devices_t)strtoul(value, NULL, 0);
        if (devices != AUDIO_DEVICE_NONE) {
            pthread_mutex_lock(&out->dev->lock);
            pthread_mutex_lock(&out->lock);
            if (out->devices != devices) {
                out->devices = devices;
                if (out->dev->in_call) {
                    out->dev->call_devices = devices;
                    ret = route_call_locked(out->dev);
                } else if (!out->standby) {
                    ret = route_output_locked(out->dev, devices);
                } else {
                    ret = 0;
                }
            } else {
                ret = 0;
            }
            pthread_mutex_unlock(&out->lock);
            pthread_mutex_unlock(&out->dev->lock);
        }
    } else {
        ret = 0;
    }
    str_parms_destroy(parms);
    return ret;
}

static char *out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    const struct lt_stream_out *out = (const struct lt_stream_out *)stream;
    return out->config.period_size * out->config.period_count * 1000 / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    /* AudioFlinger software volume preserves one calibrated hardware gain. */
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    struct lt_stream_out *out = (struct lt_stream_out *)stream;
    const size_t frame_size = audio_stream_out_frame_size(stream);
    const size_t frames = bytes / frame_size;
    int ret;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream_locked(out);
        if (ret != 0) {
            pthread_mutex_unlock(&out->dev->lock);
            goto exit;
        }
    }
    pthread_mutex_unlock(&out->dev->lock);

    ret = pcm_write(out->pcm, buffer, bytes);
    if (ret == 0) {
        out->written_frames += frames;
        out->last_pcm_error[0] = '\0';
    } else {
        const char *error = out->pcm ? pcm_get_error(out->pcm) : "PCM is null";
        out->write_errors++;
        snprintf(out->last_pcm_error, sizeof(out->last_pcm_error), "%s", error);
        ALOGE("V191 pcm_write failed: card=%d device=%d bytes=%zu error=%s",
              out->dev->card, LT_PCM_DEVICE, bytes, error);
    }

exit:
    pthread_mutex_unlock(&out->lock);
    if (ret != 0)
        usleep((uint64_t)frames * 1000000 / LT_RATE);
    /*
     * Android primary HALs preserve stream timing on transient ALSA errors.
     * Unlike V165, V191 records every error in logcat and in the HAL dump.
     */
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream, uint32_t *dsp_frames)
{
    const struct lt_stream_out *out = (const struct lt_stream_out *)stream;
    *dsp_frames = (uint32_t)out->written_frames;
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream, int64_t *timestamp)
{
    *timestamp = 0;
    return -EINVAL;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames, struct timespec *timestamp)
{
    const struct lt_stream_out *out = (const struct lt_stream_out *)stream;
    if (out->pcm == NULL)
        return -ENODATA;
    unsigned int avail = 0;
    if (pcm_get_htimestamp(out->pcm, &avail, timestamp) != 0)
        return -EINVAL;
    int64_t presented = (int64_t)out->written_frames -
            (int64_t)(out->config.period_size * out->config.period_count) + avail;
    if (presented < 0)
        return -EINVAL;
    *frames = (uint64_t)presented;
    return 0;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    return LT_RATE;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    return LT_PERIOD_SIZE * sizeof(int16_t);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    return AUDIO_CHANNEL_IN_MONO;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int start_input_stream_locked(struct lt_stream_in *in)
{
    int ret = route_input_locked(in);
    if (ret != 0)
        return ret;
    in->pcm = pcm_open(in->dev->card, LT_PCM_DEVICE, PCM_IN | PCM_MONOTONIC, &in->config);
    if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
        ALOGE("cannot open capture PCM: %s", in->pcm ? pcm_get_error(in->pcm) : "null");
        if (in->pcm != NULL) {
            pcm_close(in->pcm);
            in->pcm = NULL;
        }
        if (in->dev->in_call)
            (void)route_call_locked(in->dev);
        else
            disable_capture_locked(in->dev);
        return -ENODEV;
    }
    in->dev->active_input = in;
    in->standby = false;
    return 0;
}

static int do_input_standby_locked(struct lt_stream_in *in)
{
    if (!in->standby) {
        if (in->pcm != NULL) {
            pcm_close(in->pcm);
            in->pcm = NULL;
        }
        if (in->dev->active_input == in)
            in->dev->active_input = NULL;
        disable_capture_locked(in->dev);
        in->standby = true;
        if (in->dev->in_call)
            (void)route_call_locked(in->dev);
        else if (in->dev->active_output == NULL || in->dev->active_output->standby ||
                 !is_sco_output(in->dev->active_output->devices))
            close_sco_pcm_locked(in->dev);
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct lt_stream_in *in = (struct lt_stream_in *)stream;
    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    int ret = do_input_standby_locked(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return ret;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    const struct lt_stream_in *in = (const struct lt_stream_in *)stream;
    dprintf(fd, "lt013g V191 input: standby=%d devices=0x%x frames=%" PRId64
            " errors=%" PRIu64 " last_error=%s\n",
            in->standby, in->devices, in->read_frames,
            in->read_errors,
            in->last_pcm_error[0] ? in->last_pcm_error : "none");
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct lt_stream_in *in = (struct lt_stream_in *)stream;
    struct str_parms *parms = str_parms_create_str(kvpairs);
    char value[32];
    int ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        audio_devices_t devices = (audio_devices_t)strtoul(value, NULL, 0);
        if (devices != AUDIO_DEVICE_NONE) {
            pthread_mutex_lock(&in->dev->lock);
            pthread_mutex_lock(&in->lock);
            in->devices = devices;
            if (!in->standby)
                ret = route_input_locked(in);
            else
                ret = 0;
            pthread_mutex_unlock(&in->lock);
            pthread_mutex_unlock(&in->dev->lock);
        }
    } else {
        ret = 0;
    }
    str_parms_destroy(parms);
    return ret;
}

static char *in_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer, size_t bytes)
{
    struct lt_stream_in *in = (struct lt_stream_in *)stream;
    const size_t frames = bytes / sizeof(int16_t);
    const size_t hw_bytes = frames * LT_IN_HW_CHANNELS * sizeof(int16_t);
    int ret;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream_locked(in);
        if (ret != 0) {
            pthread_mutex_unlock(&in->dev->lock);
            goto exit;
        }
    }
    pthread_mutex_unlock(&in->dev->lock);

    if (frames > in->stereo_buffer_frames) {
        int16_t *tmp = realloc(in->stereo_buffer, hw_bytes);
        if (tmp == NULL) {
            ret = -ENOMEM;
            goto exit;
        }
        in->stereo_buffer = tmp;
        in->stereo_buffer_frames = frames;
    }

    ret = pcm_read(in->pcm, in->stereo_buffer, hw_bytes);
    if (ret == 0) {
        int16_t *mono = (int16_t *)buffer;
        const unsigned int channel = in->capture_channel > 1 ? 0 : in->capture_channel;
        for (size_t i = 0; i < frames; ++i)
            mono[i] = in->stereo_buffer[i * 2 + channel];
        if (in->dev->mic_mute ||
            (in->source == AUDIO_SOURCE_CAMCORDER && in->dev->camcorder_mute))
            memset(buffer, 0, bytes);
        in->read_frames += frames;
        in->last_pcm_error[0] = '\0';
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        in->capture_time_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    } else {
        const char *error = in->pcm ? pcm_get_error(in->pcm) : "PCM is null";
        in->read_errors++;
        snprintf(in->last_pcm_error, sizeof(in->last_pcm_error), "%s", error);
        ALOGE("V191 pcm_read failed: card=%d device=%d bytes=%zu error=%s",
              in->dev->card, LT_PCM_DEVICE, hw_bytes, error);
    }

exit:
    pthread_mutex_unlock(&in->lock);
    if (ret != 0) {
        memset(buffer, 0, bytes);
        usleep((uint64_t)frames * 1000000 / LT_RATE);
    }
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
                                   int64_t *frames, int64_t *time)
{
    const struct lt_stream_in *in = (const struct lt_stream_in *)stream;
    *frames = in->read_frames;
    *time = in->capture_time_ns;
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
        audio_io_handle_t handle, audio_devices_t devices, audio_output_flags_t flags,
        struct audio_config *config, struct audio_stream_out **stream_out,
        const char *address)
{
    struct lt_audio_device *adev = (struct lt_audio_device *)dev;
    if (config->sample_rate != LT_RATE || config->format != AUDIO_FORMAT_PCM_16_BIT ||
            config->channel_mask != AUDIO_CHANNEL_OUT_STEREO) {
        config->sample_rate = LT_RATE;
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        return -EINVAL;
    }

    struct lt_stream_out *out = calloc(1, sizeof(*out));
    if (out == NULL)
        return -ENOMEM;

    pthread_mutex_init(&out->lock, NULL);
    out->dev = adev;
    out->devices = devices == AUDIO_DEVICE_NONE ? AUDIO_DEVICE_OUT_SPEAKER : devices;
    out->standby = true;
    out->config.channels = LT_OUT_CHANNELS;
    out->config.rate = LT_RATE;
    out->config.period_size = LT_PERIOD_SIZE;
    out->config.period_count = LT_PERIOD_COUNT;
    out->config.format = PCM_FORMAT_S16_LE;
    out->config.start_threshold = LT_PERIOD_SIZE;
    out->config.stop_threshold = 0;
    out->config.silence_threshold = 0;
    out->config.avail_min = LT_PERIOD_SIZE;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    *stream_out = &out->stream;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct lt_stream_out *out = (struct lt_stream_out *)stream;
    out_standby(&out->stream.common);
    pthread_mutex_destroy(&out->lock);
    free(out);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
        audio_io_handle_t handle, audio_devices_t devices, struct audio_config *config,
        struct audio_stream_in **stream_in, audio_input_flags_t flags,
        const char *address, audio_source_t source)
{
    struct lt_audio_device *adev = (struct lt_audio_device *)dev;
    if (config->sample_rate != LT_RATE || config->format != AUDIO_FORMAT_PCM_16_BIT ||
            config->channel_mask != AUDIO_CHANNEL_IN_MONO) {
        config->sample_rate = LT_RATE;
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        config->channel_mask = AUDIO_CHANNEL_IN_MONO;
        return -EINVAL;
    }

    struct lt_stream_in *in = calloc(1, sizeof(*in));
    if (in == NULL)
        return -ENOMEM;

    pthread_mutex_init(&in->lock, NULL);
    in->dev = adev;
    in->devices = devices == AUDIO_DEVICE_NONE ? AUDIO_DEVICE_IN_BUILTIN_MIC : devices;
    in->source = source;
    in->capture_channel = 0;
    in->standby = true;
    in->config.channels = LT_IN_HW_CHANNELS;
    in->config.rate = LT_RATE;
    in->config.period_size = LT_PERIOD_SIZE;
    in->config.period_count = LT_PERIOD_COUNT;
    in->config.format = PCM_FORMAT_S16_LE;
    in->config.start_threshold = 1;
    in->config.stop_threshold = 0;
    in->config.silence_threshold = 0;
    in->config.avail_min = LT_PERIOD_SIZE;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->stream.get_capture_position = in_get_capture_position;

    *stream_in = &in->stream;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                    struct audio_stream_in *stream)
{
    struct lt_stream_in *in = (struct lt_stream_in *)stream;
    in_standby(&in->stream.common);
    free(in->stereo_buffer);
    pthread_mutex_destroy(&in->lock);
    free(in);
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    const struct lt_audio_device *adev = (const struct lt_audio_device *)dev;
    return adev->mixer == NULL ? -ENODEV : 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct lt_audio_device *adev = (struct lt_audio_device *)dev;
    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;

    pthread_mutex_lock(&adev->lock);
    adev->voice_volume = volume;
    if (adev->in_call) {
        int ril_ret = t311_ril_audio_set_volume(&adev->ril, call_ril_sound_type_locked(adev),
                                                (int)(volume * 5.0f));
        if (ril_ret != 0)
            ALOGW("V191 RIL volume deferred ret=%d", ril_ret);
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    return -ENOSYS;
}

static void force_streams_standby_locked(struct lt_audio_device *adev)
{
    struct lt_stream_out *out = adev->active_output;
    if (out != NULL && !out->standby) {
        pthread_mutex_lock(&out->lock);
        (void)do_output_standby_locked(out);
        pthread_mutex_unlock(&out->lock);
    }
    struct lt_stream_in *in = adev->active_input;
    if (in != NULL && !in->standby) {
        pthread_mutex_lock(&in->lock);
        (void)do_input_standby_locked(in);
        pthread_mutex_unlock(&in->lock);
    }
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct lt_audio_device *adev = (struct lt_audio_device *)dev;
    if (mode < AUDIO_MODE_NORMAL || mode > AUDIO_MODE_IN_COMMUNICATION)
        return -EINVAL;

    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        bool entering = mode == AUDIO_MODE_IN_CALL && !adev->in_call;
        bool leaving = adev->in_call && mode != AUDIO_MODE_IN_CALL;
        adev->mode = mode;

        if (entering) {
            ALOGI("V193 CALL stage=enter-begin requested_mode=%d", adev->mode);
            if (adev->active_output != NULL && adev->active_output->devices != AUDIO_DEVICE_NONE)
                adev->call_devices = adev->active_output->devices;
            if (adev->call_devices == AUDIO_DEVICE_OUT_SPEAKER)
                adev->call_devices = AUDIO_DEVICE_OUT_EARPIECE;
            else
                adev->call_devices &= ~AUDIO_DEVICE_OUT_SPEAKER;
            if (adev->call_devices == AUDIO_DEVICE_NONE)
                adev->call_devices = AUDIO_DEVICE_OUT_EARPIECE;
            ALOGI("V193 CALL stage=standby-begin");
            force_streams_standby_locked(adev);
            ALOGI("V193 CALL stage=standby-done");
            int ret = route_call_locked(adev);
            ALOGI("V193 CALL stage=route-done ret=%d", ret);
            if (ret == 0) {
                ALOGI("V193 CALL stage=pcm1-open-begin");
                ret = open_voice_pcm_locked(adev);
                ALOGI("V193 CALL stage=pcm1-open-done ret=%d", ret);
            }
            if (ret == 0) {
                int ril_ret = t311_ril_audio_set_clock(&adev->ril, true);
                if (ril_ret == 0)
                    ril_ret = t311_ril_audio_set_volume(&adev->ril,
                                                        call_ril_sound_type_locked(adev),
                                                        (int)(adev->voice_volume * 5.0f));
                if (ril_ret == 0)
                    ril_ret = t311_ril_audio_set_mute(&adev->ril, adev->mic_mute);
                if (ril_ret != 0)
                    ALOGW("V191 RIL call sync deferred ret=%d", ril_ret);
                else
                    ALOGI("V191 RIL call sync ret=0");
            }
            if (ret != 0) {
                (void)t311_ril_audio_set_two_mic(&adev->ril, false);
                (void)t311_ril_audio_set_clock(&adev->ril, false);
                close_voice_pcm_locked(adev);
                adev->mode = AUDIO_MODE_NORMAL;
                adev->call_devices = AUDIO_DEVICE_NONE;
                disable_capture_locked(adev);
                disable_playback_locked(adev);
                (void)apply_route(adev, normal_verb_route,
                                  sizeof(normal_verb_route) / sizeof(normal_verb_route[0]),
                                  "normal-verb");
                ALOGE("V193 CALL start failed ret=%d", ret);
                pthread_mutex_unlock(&adev->lock);
                return ret;
            }
            adev->in_call = true;
            ALOGI("V193 CALL=START devices=0x%x rate=%u", adev->call_devices,
                  adev->wb_amr ? LT_VOICE_WB_RATE : LT_VOICE_NB_RATE);
        } else if (leaving) {
            adev->in_call = false;
            (void)t311_ril_audio_set_two_mic(&adev->ril, false);
            (void)t311_ril_audio_set_clock(&adev->ril, false);
            close_voice_pcm_locked(adev);
            force_streams_standby_locked(adev);
            adev->call_devices = AUDIO_DEVICE_NONE;
            disable_capture_locked(adev);
            disable_playback_locked(adev);
            (void)apply_route(adev, normal_verb_route,
                              sizeof(normal_verb_route) / sizeof(normal_verb_route[0]),
                              "normal-verb");
            if (adev->active_output != NULL && !adev->active_output->standby)
                (void)route_output_locked(adev, adev->active_output->devices);
            if (adev->active_input != NULL && !adev->active_input->standby)
                (void)route_input_locked(adev->active_input);
            ALOGI("V193 CALL=STOP");
        } else {
            if (adev->active_output != NULL && !adev->active_output->standby)
                (void)route_output_locked(adev, adev->active_output->devices);
            if (adev->active_input != NULL && !adev->active_input->standby)
                (void)route_input_locked(adev->active_input);
        }
        ALOGI("V191 audio mode=%d", mode);
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct lt_audio_device *adev = (struct lt_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->mic_mute = state;
    int ril_ret = adev->in_call ? t311_ril_audio_set_mute(&adev->ril, state) : 0;
    if (ril_ret != 0)
        ALOGW("V191 RIL mute deferred ret=%d", ril_ret);
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    const struct lt_audio_device *adev = (const struct lt_audio_device *)dev;
    *state = adev->mic_mute;
    return 0;
}

static bool parameter_is_true(const char *value)
{
    return value != NULL && (!strcasecmp(value, "true") ||
           !strcasecmp(value, "on") || !strcmp(value, "1"));
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct lt_audio_device *adev = (struct lt_audio_device *)dev;
    struct str_parms *parms = str_parms_create_str(kvpairs ? kvpairs : "");
    char value[64];
    bool reroute = false;

    pthread_mutex_lock(&adev->lock);
    if (str_parms_get_str(parms, "voip", value, sizeof(value)) >= 0) {
        adev->voip_mode = parameter_is_true(value);
        reroute = true;
    }
    if (str_parms_get_str(parms, "voipWifiCalling", value, sizeof(value)) >= 0) {
        adev->samsung_voip_mode = parameter_is_true(value);
        reroute = true;
    }
    if (str_parms_get_str(parms, "camcorder_mute", value, sizeof(value)) >= 0)
        adev->camcorder_mute = parameter_is_true(value);
    if (str_parms_get_str(parms, "bt_headset_nrec", value, sizeof(value)) >= 0) {
        adev->bluetooth_nrec = parameter_is_true(value);
        if (adev->in_call)
            (void)route_call_locked(adev);
    }
    if (str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_SCO_WB, value, sizeof(value)) >= 0) {
        bool enabled = parameter_is_true(value);
        if (adev->bluetooth_wb != enabled) {
            bool old_wb = adev->bluetooth_wb;
            bool sco_active = adev->in_call ? is_sco_output(call_devices_locked(adev)) :
                ((adev->active_output != NULL && !adev->active_output->standby &&
                  is_sco_output(adev->active_output->devices)) ||
                 (adev->active_input != NULL && !adev->active_input->standby &&
                  is_sco_input(adev->active_input->devices)));
            adev->bluetooth_wb = enabled;
            if (sco_active) {
                close_sco_pcm_locked(adev);
                int ret = adev->in_call ? route_call_locked(adev) : open_sco_pcm_locked(adev);
                if (ret != 0) {
                    adev->bluetooth_wb = old_wb;
                    close_sco_pcm_locked(adev);
                    if (adev->in_call)
                        (void)route_call_locked(adev);
                    else
                        (void)open_sco_pcm_locked(adev);
                    ALOGE("V191 BT SCO WB switch failed wb=%d ret=%d", enabled, ret);
                } else {
                    ALOGI("V191 BT SCO WB=%d rate=%u", enabled,
                          enabled ? LT_VOICE_WB_RATE : LT_VOICE_NB_RATE);
                }
            }
        }
    }
    if (str_parms_get_str(parms, "noise_suppression", value, sizeof(value)) >= 0) {
        bool enabled = parameter_is_true(value);
        if (adev->noise_suppression != enabled) {
            adev->noise_suppression = enabled;
            if (adev->in_call)
                (void)route_call_locked(adev);
            else if (adev->active_input != NULL && !adev->active_input->standby) {
                pthread_mutex_lock(&adev->active_input->lock);
                (void)route_input_locked(adev->active_input);
                pthread_mutex_unlock(&adev->active_input->lock);
            }
        }
    }

    if (reroute) {
        if (adev->active_output != NULL && !adev->active_output->standby)
            (void)route_output_locked(adev, adev->active_output->devices);
        if (adev->active_input != NULL && !adev->active_input->standby)
            (void)route_input_locked(adev->active_input);
    }
    pthread_mutex_unlock(&adev->lock);
    str_parms_destroy(parms);
    ALOGI("V191 device parameters: %s voip=%d samsung_voip=%d camcorder_mute=%d nrec=%d sco_wb=%d ns=%d",
          kvpairs ? kvpairs : "", adev->voip_mode, adev->samsung_voip_mode,
          adev->camcorder_mute, adev->bluetooth_nrec, adev->bluetooth_wb,
          adev->noise_suppression);
    return 0;
}

static char *adev_get_parameters(const struct audio_hw_device *dev, const char *keys)
{
    return strdup("");
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    if (config->format != AUDIO_FORMAT_PCM_16_BIT ||
            config->channel_mask != AUDIO_CHANNEL_IN_MONO ||
            config->sample_rate != LT_RATE)
        return 0;
    return LT_PERIOD_SIZE * sizeof(int16_t);
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    const struct lt_audio_device *adev = (const struct lt_audio_device *)device;
    dprintf(fd, "SM-T311 WM1811 V193 HAL: card=%d rate=%d period=%dx%d "
            "mic_mute=%d mode=%d voice_volume=%.3f voip=%d samsung_voip=%d\n",
            adev->card, LT_RATE, LT_PERIOD_SIZE, LT_PERIOD_COUNT,
            adev->mic_mute, adev->mode, adev->voice_volume,
            adev->voip_mode, adev->samsung_voip_mode);
    dprintf(fd, "V191 call=%d devices=0x%x wb=%d nrec=%d ns=%d voice_rx=%d voice_tx=%d sco_rx=%d sco_tx=%d\n",
            adev->in_call, adev->call_devices, adev->wb_amr, adev->bluetooth_nrec,
            adev->noise_suppression,
            adev->pcm_voice_rx != NULL, adev->pcm_voice_tx != NULL,
            adev->pcm_sco_rx != NULL, adev->pcm_sco_tx != NULL);
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct lt_audio_device *adev = (struct lt_audio_device *)device;
    pthread_mutex_lock(&adev->lock);
    if (adev->in_call) {
        (void)t311_ril_audio_set_two_mic(&adev->ril, false);
        (void)t311_ril_audio_set_clock(&adev->ril, false);
    }
    close_voice_pcm_locked(adev);
    adev->in_call = false;
    pthread_mutex_unlock(&adev->lock);
    t311_ril_audio_close(&adev->ril);
    if (adev->mixer != NULL)
        mixer_close(adev->mixer);
    pthread_mutex_destroy(&adev->lock);
    free(adev);
    return 0;
}

static int adev_open(const hw_module_t *module, const char *name, hw_device_t **device)
{
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    struct lt_audio_device *adev = calloc(1, sizeof(*adev));
    if (adev == NULL)
        return -ENOMEM;
    pthread_mutex_init(&adev->lock, NULL);
    adev->card = find_tab3_card();
    if (adev->card < 0 || adev->card >= LT_MAX_CARDS) {
        ALOGE("TAB3 ALSA card not found");
        pthread_mutex_destroy(&adev->lock);
        free(adev);
        return -ENODEV;
    }

    adev->mixer = mixer_open(adev->card);
    if (adev->mixer == NULL) {
        ALOGE("cannot open TAB3 mixer card %d", adev->card);
        pthread_mutex_destroy(&adev->lock);
        free(adev);
        return -ENODEV;
    }

    build_control_cache(adev);

    if (find_control(adev, "DAC1L Mixer AIF1.1 Switch") == NULL ||
        find_control(adev, "SPK Switch") == NULL ||
        find_control(adev, "Main Mic Switch") == NULL) {
        ALOGE("card %d is not the expected TAB3/WM1811 mixer", adev->card);
        mixer_close(adev->mixer);
        pthread_mutex_destroy(&adev->lock);
        free(adev);
        return -ENODEV;
    }

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *)module;
    adev->hw_device.common.close = adev_close;
    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.get_master_volume = adev_get_master_volume;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;
    adev->mode = AUDIO_MODE_NORMAL;
    adev->voice_volume = 1.0f;
    adev->call_devices = AUDIO_DEVICE_NONE;
    adev->bluetooth_nrec = true;
    adev->bluetooth_wb = false;
    int ril_ret = t311_ril_audio_init(&adev->ril, wb_amr_changed, adev);
    if (ril_ret != 0) {
        ALOGE("cannot initialize T311 RIL audio client: %d", ril_ret);
        mixer_close(adev->mixer);
        pthread_mutex_destroy(&adev->lock);
        free(adev);
        return ril_ret;
    }

    /*
     * Program the static stock topology once, then leave physical endpoints
     * disabled until AudioPolicy starts a stream.
     */
    pthread_mutex_lock(&adev->lock);
    int route_ret = init_core_locked(adev);
    pthread_mutex_unlock(&adev->lock);
    if (route_ret != 0) {
        ALOGE("cannot initialize stock SM-T311 WM1811 route");
        t311_ril_audio_close(&adev->ril);
        mixer_close(adev->mixer);
        pthread_mutex_destroy(&adev->lock);
        free(adev);
        return route_ret;
    }

    *device = &adev->hw_device.common;
    ALOGI("SM-T311 WM1811 V193 primary audio HAL opened on card %d", adev->card);
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "SM-T311 WM1811 V193 stock-calibrated complete primary audio HAL",
        .author = "Pawel Jarczak",
        .methods = &hal_module_methods,
    },
};
