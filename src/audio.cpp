// SPG2xx audio: 16-channel PCM/ADPCM synth, envelopes, beat timer, and the sample ring
// buffer feeding raylib. Ported from MAME (src/devices/machine/spg2xx_audio.cpp,
// BSD-3-Clause, Ryan Holtz / Jonathan Gevaryahu; src/devices/sound/imaadpcm.cpp,
// BSD-3-Clause, Andrew Gardner / Aaron Giles).

#include "common.h"

uint16_t audio_regs[0x200];
uint16_t audio_phase_regs[0x200];
uint16_t audio_ctrl_regs[0x20];
double audio_channel_rate[16];
double audio_channel_rate_accum[16];
uint8_t audio_sample_shift[16];
uint32_t audio_sample_count[16];
uint32_t audio_rampdown_frame[16];
uint32_t audio_envclk_frame[16];
uint32_t audio_envelope_addr_rt[16];
int32_t audio_ima_signal[16];
int32_t audio_ima_step[16];
uint16_t audio_adpcm36_remaining[16];
uint16_t audio_adpcm36_header[16];
uint16_t audio_curr_beat_base_count = 0;
int32_t audio_adpcm36_prevsamp[16][2];

// Per-channel registers (audio_regs, offset 0x000 in real spg2xx)
enum {
    AUDIO_WAVE_ADDR             = 0x000,

    AUDIO_MODE                  = 0x001,
    AUDIO_WADDR_HIGH_MASK       = 0x003f,
    AUDIO_LADDR_HIGH_MASK       = 0x0fc0,
    AUDIO_LADDR_HIGH_SHIFT      = 6,
    AUDIO_TONE_MODE_MASK        = 0x3000,
    AUDIO_TONE_MODE_SHIFT       = 12,
    AUDIO_TONE_MODE_SW          = 0,
    AUDIO_TONE_MODE_HW_ONESHOT  = 1,
    AUDIO_TONE_MODE_HW_LOOP     = 2,
    AUDIO_16M_MASK              = 0x4000,
    AUDIO_ADPCM_MASK            = 0x8000,

    AUDIO_LOOP_ADDR             = 0x002,

    AUDIO_PAN_VOL               = 0x003,
    AUDIO_PAN_VOL_MASK          = 0x7f7f,
    AUDIO_VOLUME_MASK           = 0x007f,
    AUDIO_PAN_MASK              = 0x7f00,
    AUDIO_PAN_SHIFT             = 8,

    AUDIO_ENVELOPE0             = 0x004,
    AUDIO_ENVELOPE_INC_MASK     = 0x007f,
    AUDIO_ENVELOPE_SIGN_MASK    = 0x0080,
    AUDIO_ENVELOPE_TARGET_MASK  = 0x7f00,
    AUDIO_ENVELOPE_TARGET_SHIFT = 8,
    AUDIO_ENVELOPE_REPEAT_PERIOD_MASK = 0x8000,

    AUDIO_ENVELOPE_DATA         = 0x005,
    AUDIO_ENVELOPE_DATA_MASK    = 0xff7f,
    AUDIO_EDD_MASK              = 0x007f,
    AUDIO_ENVELOPE_COUNT_MASK   = 0xff00,
    AUDIO_ENVELOPE_COUNT_SHIFT  = 8,

    AUDIO_ENVELOPE1             = 0x006,
    AUDIO_ENVELOPE_LOAD_MASK    = 0x00ff,
    AUDIO_ENVELOPE_RPT_MASK     = 0x0100,
    AUDIO_ENVELOPE_RPCNT_MASK   = 0xfe00,
    AUDIO_ENVELOPE_RPCNT_SHIFT  = 9,

    AUDIO_ENVELOPE_ADDR_HIGH    = 0x007,
    AUDIO_EADDR_HIGH_MASK       = 0x003f,

    AUDIO_ENVELOPE_ADDR         = 0x008,
    AUDIO_WAVE_DATA_PREV        = 0x009,

    AUDIO_ENVELOPE_LOOP_CTRL    = 0x00a,
    AUDIO_EAOFFSET_MASK         = 0x01ff,
    AUDIO_RAMPDOWN_OFFSET_MASK  = 0xfe00,
    AUDIO_RAMPDOWN_OFFSET_SHIFT = 9,

    AUDIO_WAVE_DATA             = 0x00b,

    AUDIO_ADPCM_SEL             = 0x00d,
    AUDIO_ADPCM_SEL_MASK        = 0xfe00,
    AUDIO_ADPCM36_MASK          = 0x8000,
};

// Per-channel phase/pitch registers (audio_phase_regs, offset 0x200 in real spg2xx)
enum {
    AUDIO_PHASE_HIGH            = 0x000,
    AUDIO_PHASE_HIGH_MASK       = 0x0007,

    AUDIO_PHASE_ACCUM_HIGH      = 0x001,
    AUDIO_PHASE_ACCUM_HIGH_MASK = 0x0007,

    AUDIO_TARGET_PHASE_HIGH     = 0x002,
    AUDIO_TARGET_PHASE_HIGH_MASK= 0x0007,

    AUDIO_RAMP_DOWN_CLOCK       = 0x003,
    AUDIO_RAMP_DOWN_CLOCK_MASK  = 0x0007,

    AUDIO_PHASE                 = 0x004,
    AUDIO_PHASE_ACCUM           = 0x005,
    AUDIO_TARGET_PHASE          = 0x006,

    AUDIO_PHASE_CTRL            = 0x007,

    AUDIO_CHAN_OFFSET_MASK      = 0xf0f,
};

enum {
    AUDIO_CHANNEL_ENABLE            = 0x000,
    AUDIO_MAIN_VOLUME               = 0x001,
    AUDIO_MAIN_VOLUME_MASK          = 0x007f,
    AUDIO_CHANNEL_FIQ_ENABLE        = 0x002,
    AUDIO_CHANNEL_FIQ_STATUS        = 0x003,
    AUDIO_CHANNEL_FIQ_STATUS_MASK   = 0xffff,
    AUDIO_BEAT_BASE_COUNT           = 0x004,
    AUDIO_BEAT_BASE_COUNT_MASK      = 0x07ff,
    AUDIO_BEAT_COUNT                = 0x005,
    AUDIO_BEAT_COUNT_MASK           = 0x3fff,
    AUDIO_BIS_MASK                  = 0x4000,
    AUDIO_BIE_MASK                  = 0x8000,
    AUDIO_ENVCLK0                   = 0x006,
    AUDIO_ENVCLK0_HIGH              = 0x007,
    AUDIO_ENVCLK1                   = 0x008,
    AUDIO_ENVCLK1_HIGH              = 0x009,
    AUDIO_ENV_RAMP_DOWN             = 0x00a,
    AUDIO_ENV_RAMP_DOWN_MASK        = 0xffff,
    AUDIO_CHANNEL_STOP              = 0x00b,
    AUDIO_CHANNEL_ZERO_CROSS        = 0x00c,
    AUDIO_CHANNEL_ZERO_CROSS_MASK   = 0xffff,
    AUDIO_CONTROL                   = 0x00d,
    AUDIO_CONTROL_MASK              = 0x9fe8,
    AUDIO_CONTROL_NOINT_MASK        = 0x0200,
    AUDIO_CONTROL_VOLSEL_MASK       = 0x00c0,
    AUDIO_CONTROL_VOLSEL_SHIFT      = 6,
    AUDIO_COMPRESS_CTRL             = 0x00e,
    AUDIO_CHANNEL_STATUS            = 0x00f,
    AUDIO_WAVE_IN_L                 = 0x010,
    AUDIO_WAVE_IN_R                 = 0x011,
    AUDIO_WAVE_OUT_L                = 0x012,
    AUDIO_WAVE_OUT_R                = 0x013,
    AUDIO_CHANNEL_REPEAT            = 0x014,
    AUDIO_CHANNEL_REPEAT_MASK       = 0xffff,
    AUDIO_CHANNEL_ENV_MODE          = 0x015,
    AUDIO_CHANNEL_ENV_MODE_MASK     = 0xffff,
    AUDIO_CHANNEL_TONE_RELEASE      = 0x016,
    AUDIO_CHANNEL_TONE_RELEASE_MASK = 0xffff,
    AUDIO_CHANNEL_ENV_IRQ           = 0x017,
    AUDIO_CHANNEL_ENV_IRQ_MASK      = 0xffff,
    AUDIO_CHANNEL_PITCH_BEND        = 0x018,
    AUDIO_CHANNEL_PITCH_BEND_MASK   = 0xffff,
    AUDIO_SOFT_PHASE                = 0x019,
    AUDIO_ATTACK_RELEASE            = 0x01a,
    AUDIO_EQ_CUTOFF10               = 0x01b,
    AUDIO_EQ_CUTOFF10_MASK          = 0x7f7f,
    AUDIO_EQ_CUTOFF32               = 0x01c,
    AUDIO_EQ_CUTOFF32_MASK          = 0x7f7f,
    AUDIO_EQ_GAIN10                 = 0x01d,
    AUDIO_EQ_GAIN10_MASK            = 0x7f7f,
    AUDIO_EQ_GAIN32                 = 0x01e,
    AUDIO_EQ_GAIN32_MASK            = 0x7f7f,
};

inline bool audio_channel_status(int ch) { return audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & (1 << ch); }

inline uint16_t audio_vol_sel() { return (audio_ctrl_regs[AUDIO_CONTROL] & AUDIO_CONTROL_VOLSEL_MASK) >> AUDIO_CONTROL_VOLSEL_SHIFT; }

inline uint16_t audio_wave_addr_high(int ch) { return audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_WADDR_HIGH_MASK; }

inline uint16_t audio_loop_addr_high(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_LADDR_HIGH_MASK) >> AUDIO_LADDR_HIGH_SHIFT; }

inline uint16_t audio_tone_mode(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_TONE_MODE_MASK) >> AUDIO_TONE_MODE_SHIFT; }

inline uint16_t audio_16bit_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_16M_MASK) ? 1 : 0; }

inline uint16_t audio_adpcm_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_ADPCM_MASK) ? 1 : 0; }

inline uint16_t audio_volume(int ch) { return audio_regs[(ch << 4) | AUDIO_PAN_VOL] & AUDIO_VOLUME_MASK; }

inline uint16_t audio_pan(int ch) { return (audio_regs[(ch << 4) | AUDIO_PAN_VOL] & AUDIO_PAN_MASK) >> AUDIO_PAN_SHIFT; }

inline uint16_t audio_envelope_inc(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_INC_MASK; }

inline uint16_t audio_envelope_sign(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_SIGN_MASK) ? 1 : 0; }

inline uint16_t audio_envelope_target(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_TARGET_MASK) >> AUDIO_ENVELOPE_TARGET_SHIFT; }

inline uint16_t audio_edd(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] & AUDIO_EDD_MASK; }

inline uint16_t audio_envelope_count(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] & AUDIO_ENVELOPE_COUNT_MASK) >> AUDIO_ENVELOPE_COUNT_SHIFT; }

inline void audio_set_edd(int ch, uint8_t edd) { audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] = (audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] & ~AUDIO_EDD_MASK) | edd; }

inline void audio_set_envelope_count(int ch, uint16_t count) { audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] = audio_edd(ch) | (count << AUDIO_ENVELOPE_COUNT_SHIFT); }

inline uint16_t audio_envelope_load(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_LOAD_MASK; }

inline uint16_t audio_envelope_repeat_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_RPT_MASK) ? 1 : 0; }

inline uint16_t audio_envelope_repeat_count(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_RPCNT_MASK) >> AUDIO_ENVELOPE_RPCNT_SHIFT; }

inline void audio_set_envelope_repeat_count(int ch, uint16_t count) { audio_regs[(ch << 4) | AUDIO_ENVELOPE1] = (audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & ~AUDIO_ENVELOPE_RPCNT_MASK) | ((count << AUDIO_ENVELOPE_RPCNT_SHIFT) & AUDIO_ENVELOPE_RPCNT_MASK); }

inline uint16_t audio_envelope_addr_high(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE_ADDR_HIGH] & AUDIO_EADDR_HIGH_MASK; }

inline uint16_t audio_eaoffset(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE_LOOP_CTRL] & AUDIO_EAOFFSET_MASK; }

inline uint16_t audio_rampdown_offset(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE_LOOP_CTRL] & AUDIO_RAMPDOWN_OFFSET_MASK) >> AUDIO_RAMPDOWN_OFFSET_SHIFT; }

inline uint16_t audio_adpcm36_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_ADPCM_SEL] & AUDIO_ADPCM36_MASK) ? 1 : 0; }

inline uint16_t audio_phase_high(int ch) { return audio_phase_regs[(ch << 4) | AUDIO_PHASE_HIGH] & AUDIO_PHASE_HIGH_MASK; }

inline uint32_t audio_phase(int ch) { return ((uint32_t)audio_phase_high(ch) << 16) | audio_phase_regs[(ch << 4) | AUDIO_PHASE]; }

inline uint16_t audio_rampdown_clock(int ch) { return audio_phase_regs[(ch << 4) | AUDIO_RAMP_DOWN_CLOCK] & AUDIO_RAMP_DOWN_CLOCK_MASK; }

inline uint32_t audio_wave_addr(int ch) { return ((uint32_t)audio_wave_addr_high(ch) << 16) | audio_regs[(ch << 4) | AUDIO_WAVE_ADDR]; }

inline uint32_t audio_loop_addr(int ch) { return ((uint32_t)audio_loop_addr_high(ch) << 16) | audio_regs[(ch << 4) | AUDIO_LOOP_ADDR]; }

inline uint32_t audio_envelope_addr(int ch) { return ((uint32_t)audio_envelope_addr_high(ch) << 16) | audio_regs[(ch << 4) | AUDIO_ENVELOPE_ADDR]; }

inline void audio_set_wave_addr(int ch, uint32_t addr) {
    audio_regs[(ch << 4) | AUDIO_MODE] &= ~AUDIO_WADDR_HIGH_MASK;
    audio_regs[(ch << 4) | AUDIO_MODE] |= (addr >> 16) & AUDIO_WADDR_HIGH_MASK;
    audio_regs[(ch << 4) | AUDIO_WAVE_ADDR] = addr & 0xffff;
}

inline void audio_inc_wave_addr(int ch) { audio_set_wave_addr(ch, audio_wave_addr(ch) + 1); }

static const uint32_t s_rampdown_frame_counts[8] = {
    13*4, 13*16, 13*64, 13*256, 13*1024, 13*4096, 13*8192, 13*8192
};

static const uint32_t s_envclk_frame_counts[16] = {
    4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 8192, 8192, 8192, 8192
};

inline uint32_t audio_get_rampdown_frame_count(int ch) { return s_rampdown_frame_counts[audio_rampdown_clock(ch)]; }

inline uint32_t audio_get_envelope_clock(int ch) {
    if (ch < 4) return (audio_ctrl_regs[AUDIO_ENVCLK0] >> (ch << 2)) & 0xf;
    else if (ch < 8) return (audio_ctrl_regs[AUDIO_ENVCLK0_HIGH] >> ((ch - 4) << 2)) & 0xf;
    else if (ch < 12) return (audio_ctrl_regs[AUDIO_ENVCLK1] >> ((ch - 8) << 2)) & 0xf;
    else return (audio_ctrl_regs[AUDIO_ENVCLK1_HIGH] >> ((ch - 12) << 2)) & 0xf;
}

inline uint32_t audio_get_envclk_frame_count(int ch) { return s_envclk_frame_counts[audio_get_envelope_clock(ch)]; }

// Standard IMA ADPCM (mame/src/devices/sound/imaadpcm.cpp) - self-contained,
// no MAME dependencies.
static int32_t s_ima_diff_lookup[89 * 16];
static bool s_ima_tables_computed = false;

void audio_ima_compute_tables() {
    if (s_ima_tables_computed) return;
    s_ima_tables_computed = true;
    static const int8_t nbl2bit[16][4] = {
        { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
        { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
        {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
    };
    for (int step = -8; step <= 80; step++) {
        double stepval_d = floor(16.0 * pow(11.0 / 10.0, (double)step));
        int stepval = (int)(stepval_d < 32767.0 ? stepval_d : 32767.0);
        if (step == -5 || step == -4) stepval++;
        for (int nib = 0; nib < 16; nib++) {
            s_ima_diff_lookup[(step + 8) * 16 + nib] = nbl2bit[nib][0] *
                (stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2] + stepval / 4 * nbl2bit[nib][3] + stepval / 8);
        }
    }
}

static const int8_t s_ima_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

int16_t audio_ima_clock(int ch, uint8_t nibble) {
    audio_ima_signal[ch] += s_ima_diff_lookup[audio_ima_step[ch] * 16 + (nibble & 15)];
    if (audio_ima_signal[ch] > 32767) audio_ima_signal[ch] = 32767;
    else if (audio_ima_signal[ch] < -32768) audio_ima_signal[ch] = -32768;
    audio_ima_step[ch] += s_ima_index_shift[nibble & 7];
    if (audio_ima_step[ch] > 88) audio_ima_step[ch] = 88;
    else if (audio_ima_step[ch] < 0) audio_ima_step[ch] = 0;
    return (int16_t)audio_ima_signal[ch];
}

// Custom "ADPCM36" variant: 2-tap predictive filter keyed by a per-block
// header nibble.
uint16_t audio_decode_adpcm36_nybble(int ch, uint8_t data) {
    int32_t shift = audio_adpcm36_header[ch] & 0xf;
    int16_t filter = (audio_adpcm36_header[ch] & 0x3f0) >> 4;
    int16_t f0 = filter | ((filter & 0x20) ? ~0x3f : 0);
    int32_t f1 = 0;
    int16_t sdata = data << 12;
    sdata = (sdata >> shift) + (((audio_adpcm36_prevsamp[ch][0] * f0) + (audio_adpcm36_prevsamp[ch][1] * f1) + 32) >> 12);
    audio_adpcm36_prevsamp[ch][1] = audio_adpcm36_prevsamp[ch][0];
    audio_adpcm36_prevsamp[ch][0] = sdata;
    return (uint16_t)sdata ^ 0x8000;
}

void audio_stop_channel(int ch) {
    audio_ctrl_regs[AUDIO_CHANNEL_STATUS] &= ~(1 << ch);
    audio_regs[(ch << 4) | AUDIO_MODE] &= ~AUDIO_ADPCM_MASK;
    audio_ctrl_regs[AUDIO_CHANNEL_TONE_RELEASE] &= ~(1 << ch);
    audio_ctrl_regs[AUDIO_ENV_RAMP_DOWN] &= ~(1 << ch);
}

void audio_start_channel(int ch) {
    audio_ctrl_regs[AUDIO_CHANNEL_STATUS] |= (1 << ch);
    audio_envelope_addr_rt[ch] = audio_envelope_addr(ch);
    audio_set_envelope_count(ch, audio_envelope_load(ch));

    audio_ima_signal[ch] = 0;
    audio_ima_step[ch] = 0;
    audio_sample_shift[ch] = 0;
    audio_sample_count[ch] = 0;

    if (audio_adpcm36_bit(ch)) {
        audio_adpcm36_remaining[ch] = 0;
        audio_adpcm36_header[ch] = 0;
        audio_adpcm36_prevsamp[ch][0] = 0;
        audio_adpcm36_prevsamp[ch][1] = 0;
    }
}

void audio_loop_channel(int ch) {
    audio_set_wave_addr(ch, audio_loop_addr(ch));
    audio_sample_shift[ch] = 0;
}

bool audio_fetch_sample(int ch) {
    const uint32_t channel_mask = ch << 4;
    audio_regs[channel_mask | AUDIO_WAVE_DATA_PREV] = audio_regs[channel_mask | AUDIO_WAVE_DATA];

    const uint32_t wave_data_reg = channel_mask | AUDIO_WAVE_DATA;
    const uint16_t tone_mode = audio_tone_mode(ch);

    if (audio_adpcm36_bit(ch) && tone_mode != 0 && audio_adpcm36_remaining[ch] == 0) {
        audio_adpcm36_header[ch] = memory_read16(audio_wave_addr(ch));
        audio_adpcm36_remaining[ch] = 8;
        audio_inc_wave_addr(ch);
    }

    uint16_t raw_sample = tone_mode ? memory_read16(audio_wave_addr(ch)) : audio_regs[wave_data_reg];

    if (audio_adpcm_bit(ch) || audio_adpcm36_bit(ch)) {
        if (tone_mode != 0 && raw_sample == 0xffff) {
            if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT) {
                audio_sample_count[ch] = 0;
                audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
                audio_stop_channel(ch);
                return false;
            } else {
                audio_sample_count[ch] = 0;
                audio_loop_channel(ch);
                audio_regs[(ch << 4) | AUDIO_MODE] &= ~AUDIO_ADPCM_MASK;
            }
        } else {
            audio_regs[wave_data_reg] = raw_sample;
            audio_regs[wave_data_reg] >>= audio_sample_shift[ch];
            const uint8_t adpcm_sample = (uint8_t)(audio_regs[wave_data_reg] & 0x000f);
            if (audio_adpcm36_bit(ch))
                audio_regs[wave_data_reg] = audio_decode_adpcm36_nybble(ch, adpcm_sample);
            else
                audio_regs[wave_data_reg] = (uint16_t)(audio_ima_clock(ch, adpcm_sample)) ^ 0x8000;
        }
        audio_sample_count[ch]++;
    }
    else if (audio_16bit_bit(ch)) {
        if (tone_mode != 0 && raw_sample == 0xffff) {
            if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT) {
                audio_sample_count[ch] = 0;
                audio_stop_channel(ch);
                return false;
            } else {
                audio_sample_count[ch] = 0;
                audio_loop_channel(ch);
            }
        } else {
            audio_regs[wave_data_reg] = raw_sample;
        }
        audio_sample_count[ch]++;
    }
    else {
        // 8-bit mode
        if (tone_mode != 0) {
            if (audio_sample_shift[ch])
                raw_sample &= 0xff00;
            else
                raw_sample <<= 8;
            raw_sample |= raw_sample >> 8;

            if (raw_sample == 0xffff) {
                if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT) {
                    audio_sample_count[ch] = 0;
                    audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
                    audio_stop_channel(ch);
                    return false;
                } else {
                    audio_sample_count[ch] = 0;
                    audio_loop_channel(ch);
                }
            } else {
                audio_regs[wave_data_reg] = raw_sample;
            }
        }
        audio_sample_count[ch]++;
    }

    return true;
}

bool audio_advance_channel(int ch) {
    audio_channel_rate_accum[ch] += audio_channel_rate[ch];
    uint32_t samples_to_advance = 0;
    while (audio_channel_rate_accum[ch] >= 70312.5) {
        audio_channel_rate_accum[ch] -= 70312.5;
        samples_to_advance++;
    }

    if (!samples_to_advance) return true;

    bool playing = true;
    for (uint32_t s = 0; s < samples_to_advance; s++) {
        playing = audio_fetch_sample(ch);
        if (!playing) break;

        if (audio_adpcm_bit(ch) || audio_adpcm36_bit(ch)) {
            audio_sample_shift[ch] += 4;
            if (audio_sample_shift[ch] >= 16) {
                audio_sample_shift[ch] = 0;
                audio_inc_wave_addr(ch);
                if (audio_adpcm36_bit(ch)) audio_adpcm36_remaining[ch]--;
            }
        } else if (audio_16bit_bit(ch)) {
            audio_inc_wave_addr(ch);
        } else {
            audio_sample_shift[ch] += 8;
            if (audio_sample_shift[ch] >= 16) {
                audio_sample_shift[ch] = 0;
                audio_inc_wave_addr(ch);
            }
        }
    }
    return playing;
}

void audio_rampdown_tick(int ch) {
    const uint8_t old_edd = (uint8_t)audio_edd(ch);
    uint8_t new_edd = old_edd - (uint8_t)audio_rampdown_offset(ch);
    if (new_edd > old_edd) new_edd = 0;

    if (new_edd) {
        audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] &= ~AUDIO_EDD_MASK;
        audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] |= new_edd & AUDIO_EDD_MASK;
        audio_rampdown_frame[ch] = audio_get_rampdown_frame_count(ch);
    } else {
        audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
        audio_stop_channel(ch);
    }
}

bool audio_envelope_tick(int ch) {
    const uint16_t channel_mask = ch << 4;
    uint16_t new_count = audio_envelope_count(ch);
    const uint16_t curr_edd = audio_edd(ch);
    bool edd_changed = false;
    if (new_count > 0) {
        new_count--;
        audio_set_envelope_count(ch, new_count);
    }

    if (new_count == 0) {
        const uint16_t target = audio_envelope_target(ch);
        uint16_t new_edd = curr_edd;
        const uint16_t inc = audio_envelope_inc(ch);

        if (new_edd != target) {
            if (audio_envelope_sign(ch)) {
                new_edd -= inc;
                if (new_edd > curr_edd) new_edd = 0;
                else if (new_edd < target) new_edd = target;

                if (new_edd == 0) {
                    audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
                    audio_stop_channel(ch);
                    return true;
                }
            } else {
                new_edd += inc;
                if (new_edd >= target) new_edd = target;
            }
        }

        if (new_edd == target) {
            new_edd = target;
            if (audio_envelope_repeat_bit(ch)) {
                const uint16_t repeat_count = audio_envelope_repeat_count(ch) - 1;
                if (repeat_count == 0) {
                    audio_regs[channel_mask | AUDIO_ENVELOPE0] = memory_read16(audio_envelope_addr_rt[ch]);
                    audio_regs[channel_mask | AUDIO_ENVELOPE1] = memory_read16(audio_envelope_addr_rt[ch] + 1);
                    audio_regs[channel_mask | AUDIO_ENVELOPE_LOOP_CTRL] = memory_read16(audio_envelope_addr_rt[ch] + 2);
                    audio_envelope_addr_rt[ch] = audio_envelope_addr(ch) + audio_eaoffset(ch);
                } else {
                    audio_set_envelope_repeat_count(ch, repeat_count);
                }
            } else {
                audio_regs[channel_mask | AUDIO_ENVELOPE0] = memory_read16(audio_envelope_addr_rt[ch]);
                audio_regs[channel_mask | AUDIO_ENVELOPE1] = memory_read16(audio_envelope_addr_rt[ch] + 1);
                audio_envelope_addr_rt[ch] += 2;
            }
            new_count = audio_envelope_load(ch);
            audio_set_envelope_count(ch, new_count);
        } else {
            new_count = audio_envelope_load(ch);
            audio_set_envelope_count(ch, new_count);
        }

        audio_set_edd(ch, (uint8_t)new_edd);
        edd_changed = true;
    }
    return edd_changed;
}

void check_audio_irq() {
    if ((audio_ctrl_regs[AUDIO_BEAT_COUNT] & (AUDIO_BIS_MASK | AUDIO_BIE_MASK)) == (AUDIO_BIS_MASK | AUDIO_BIE_MASK)) {
        cpu_ptr->execute_set_input(UNSP_IRQ4_LINE, 1);
    } else {
        cpu_ptr->execute_set_input(UNSP_IRQ4_LINE, 0);
    }
}

// Beat IRQ ticks at the same native 70312.5Hz rate as sample processing, so
// it's ticked once per generated sample below instead of via a separate timer.
void audio_beat_tick() {
    if (audio_curr_beat_base_count > 0) audio_curr_beat_base_count--;

    if (audio_curr_beat_base_count == 0) {
        audio_curr_beat_base_count = audio_ctrl_regs[AUDIO_BEAT_BASE_COUNT];

        uint16_t beat_count = audio_ctrl_regs[AUDIO_BEAT_COUNT] & AUDIO_BEAT_COUNT_MASK;
        if (beat_count > 0) {
            beat_count--;
            audio_ctrl_regs[AUDIO_BEAT_COUNT] = (audio_ctrl_regs[AUDIO_BEAT_COUNT] & ~AUDIO_BEAT_COUNT_MASK) | beat_count;
        }
        if (beat_count == 0 && (audio_ctrl_regs[AUDIO_BEAT_COUNT] & AUDIO_BIE_MASK)) {
            audio_ctrl_regs[AUDIO_BEAT_COUNT] |= AUDIO_BIS_MASK;
            check_audio_irq();
        }
    }
}

// Ported from sound_stream_update: the per-sample mixer. Writes interleaved
// stereo int16_t pairs into out[0..num_frames*2).
void generate_audio_frame(int16_t* out, int num_frames) {
    for (int i = 0; i < num_frames; i++) {
        int32_t left_total = 0, right_total = 0;

        for (int ch = 0; ch < 16; ch++) {
            if (!audio_channel_status(ch)) continue;

            bool playing = audio_advance_channel(ch);
            if (playing) {
                int32_t sample = (int16_t)(audio_regs[(ch << 4) | AUDIO_WAVE_DATA] ^ 0x8000);
                if (!(audio_ctrl_regs[AUDIO_CONTROL] & AUDIO_CONTROL_NOINT_MASK)) {
                    int32_t prev_sample = (int16_t)(audio_regs[(ch << 4) | AUDIO_WAVE_DATA_PREV] ^ 0x8000);
                    int16_t lerp_factor = (int16_t)((audio_channel_rate_accum[ch] / 70312.5) * 256.0);
                    prev_sample = (prev_sample * (0x100 - lerp_factor)) >> 8;
                    sample = (sample * lerp_factor) >> 8;
                    sample += prev_sample;
                }

                sample = (sample * (int32_t)audio_edd(ch)) >> 7;

                int32_t vol = audio_volume(ch);
                int32_t pan = audio_pan(ch);

                int32_t pan_left, pan_right;
                if (pan < 0x40) {
                    pan_left = 0x7f * vol;
                    pan_right = pan * 2 * vol;
                } else {
                    pan_left = (0x7f - pan) * 2 * vol;
                    pan_right = 0x7f * vol;
                }

                left_total += ((int16_t)sample * (int16_t)pan_left) >> 14;
                right_total += ((int16_t)sample * (int16_t)pan_right) >> 14;

                const uint16_t mask = (1 << ch);
                if (audio_ctrl_regs[AUDIO_ENV_RAMP_DOWN] & mask) {
                    if (audio_rampdown_frame[ch] > 0) audio_rampdown_frame[ch]--;
                    if (audio_rampdown_frame[ch] == 0) audio_rampdown_tick(ch);
                } else if (!(audio_ctrl_regs[AUDIO_CHANNEL_ENV_MODE] & mask)) {
                    if (audio_envclk_frame[ch] > 0) audio_envclk_frame[ch]--;
                    if (audio_envclk_frame[ch] == 0) {
                        audio_envelope_tick(ch);
                        audio_envclk_frame[ch] = audio_get_envclk_frame_count(ch);
                    }
                }
            }
        }

        if (audio_ctrl_regs[AUDIO_WAVE_IN_L]) left_total += (int32_t)(audio_ctrl_regs[AUDIO_WAVE_IN_L] - 0x8000);
        if (audio_ctrl_regs[AUDIO_WAVE_IN_R]) right_total += (int32_t)(audio_ctrl_regs[AUDIO_WAVE_IN_R] - 0x8000);

        switch (audio_vol_sel()) {
            case 0: left_total >>= 4; right_total >>= 4; break;
            default: left_total >>= 2; right_total >>= 2; break;
        }

        int32_t left_final = (int16_t)((left_total * (int16_t)audio_ctrl_regs[AUDIO_MAIN_VOLUME]) >> 7);
        int32_t right_final = (int16_t)((right_total * (int16_t)audio_ctrl_regs[AUDIO_MAIN_VOLUME]) >> 7);

        out[i * 2 + 0] = (int16_t)left_final;
        out[i * 2 + 1] = (int16_t)right_final;

        audio_beat_tick();
    }
}

uint16_t audio_r(uint32_t offset) { return audio_regs[offset]; }

void audio_w(uint32_t offset, uint16_t data) {
    switch (offset & AUDIO_CHAN_OFFSET_MASK) {
        case AUDIO_PAN_VOL: audio_regs[offset] = data & AUDIO_PAN_VOL_MASK; break;
        case AUDIO_ENVELOPE_DATA: audio_regs[offset] = data & AUDIO_ENVELOPE_DATA_MASK; break;
        case AUDIO_ADPCM_SEL: audio_regs[offset] = data & AUDIO_ADPCM_SEL_MASK; break;
        default: audio_regs[offset] = data; break;
    }
}

uint16_t audio_phase_r(uint32_t offset) { return audio_phase_regs[offset]; }

void audio_phase_w(uint32_t offset, uint16_t data) {
    // Runtime-state arrays are sized 16 (the real channel count); the raw
    // register arrays match real hardware's 0x200-word span (up to 32
    // channel-slots), so this index is defensively clamped to stay in
    // bounds even if the game ever addresses a phantom upper channel.
    const uint16_t channel = ((offset & 0x01f0) >> 4) & 0xF;

    // Ported from spg2xx_audio_device::audio_phase_w (mame/src/devices/
    // machine/spg2xx_audio.cpp): every offset actually stores, not just
    // PHASE_HIGH - some ROM code writes a phase word and then busy-spins
    // reading it back until the readback matches, which hangs forever if
    // the write silently did nothing. PHASE_HIGH/PHASE writes also derive
    // the channel's real playback rate from the phase value - the one place
    // audio_channel_rate[] actually gets set, so this was also why nothing
    // played.
    switch (offset & AUDIO_CHAN_OFFSET_MASK) {
        case AUDIO_PHASE_HIGH:
            audio_phase_regs[offset] = data & AUDIO_PHASE_HIGH_MASK;
            audio_channel_rate[channel] = ((double)audio_phase(channel) * 140625.0 * 2.0) / (double)(1 << 19);
            audio_channel_rate_accum[channel] = 0.0;
            break;
        case AUDIO_PHASE_ACCUM_HIGH:
            audio_phase_regs[offset] = data & AUDIO_PHASE_ACCUM_HIGH_MASK;
            break;
        case AUDIO_TARGET_PHASE_HIGH:
            audio_phase_regs[offset] = data & AUDIO_TARGET_PHASE_HIGH_MASK;
            break;
        case AUDIO_RAMP_DOWN_CLOCK:
            audio_phase_regs[offset] = data & AUDIO_RAMP_DOWN_CLOCK_MASK;
            break;
        case AUDIO_PHASE:
            audio_phase_regs[offset] = data;
            audio_channel_rate[channel] = ((double)audio_phase(channel) * 140625.0 * 2.0) / (double)(1 << 19);
            audio_channel_rate_accum[channel] = 0.0;
            break;
        default:
            audio_phase_regs[offset] = data;
            break;
    }
}

void audio_reset() {
    memset(audio_regs, 0, sizeof(audio_regs));
    memset(audio_phase_regs, 0, sizeof(audio_phase_regs));
    memset(audio_ctrl_regs, 0, sizeof(audio_ctrl_regs));
    memset(audio_channel_rate, 0, sizeof(audio_channel_rate));
    memset(audio_channel_rate_accum, 0, sizeof(audio_channel_rate_accum));
    memset(audio_sample_shift, 0, sizeof(audio_sample_shift));
    memset(audio_sample_count, 0, sizeof(audio_sample_count));
    memset(audio_rampdown_frame, 0, sizeof(audio_rampdown_frame));
    memset(audio_envclk_frame, 0, sizeof(audio_envclk_frame));
    memset(audio_envelope_addr_rt, 0, sizeof(audio_envelope_addr_rt));
    memset(audio_ima_signal, 0, sizeof(audio_ima_signal));
    memset(audio_ima_step, 0, sizeof(audio_ima_step));
    memset(audio_adpcm36_remaining, 0, sizeof(audio_adpcm36_remaining));
    memset(audio_adpcm36_header, 0, sizeof(audio_adpcm36_header));
    memset(audio_adpcm36_prevsamp, 0, sizeof(audio_adpcm36_prevsamp));
    audio_curr_beat_base_count = 0;
}

// Ported from spg2xx_audio_device::audio_ctrl_w (mame/src/devices/machine/spg2xx_audio.cpp):
// channel-enable writes actually start/stop channels, and BEAT_COUNT is a
// write-1-to-clear IRQ-status register - a plain passthrough write to
// audio_ctrl_regs (like the other, simpler registers) would silently break
// both of those. Per-register masking (main volume, FIQ enable) kept too.
void audio_ctrl_w(uint32_t offset, uint16_t data) {
    switch (offset) {
        case AUDIO_CHANNEL_ENABLE: {
            uint16_t old = audio_ctrl_regs[offset];
            audio_ctrl_regs[offset] = data;
            uint16_t changed = old ^ data;
            for (int ch = 0; ch < 16; ch++) {
                uint16_t mask = 1 << ch;
                if (!(changed & mask)) continue;
                if (data & mask) {
                    if (audio_ctrl_regs[AUDIO_CHANNEL_STOP] & mask) continue;
                    if (!(audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask)) audio_start_channel(ch);
                } else {
                    if (audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask) audio_stop_channel(ch);
                }
            }
            break;
        }
        case AUDIO_MAIN_VOLUME:
            audio_ctrl_regs[offset] = data & AUDIO_MAIN_VOLUME_MASK;
            break;
        case AUDIO_CHANNEL_FIQ_STATUS:
            audio_ctrl_regs[offset] &= ~(data & AUDIO_CHANNEL_FIQ_STATUS_MASK);
            break;
        case AUDIO_BEAT_BASE_COUNT:
            audio_ctrl_regs[offset] = data & AUDIO_BEAT_BASE_COUNT_MASK;
            audio_curr_beat_base_count = audio_ctrl_regs[offset];
            break;
        case AUDIO_BEAT_COUNT: {
            uint16_t old_bis = audio_ctrl_regs[offset] & AUDIO_BIS_MASK;
            audio_ctrl_regs[offset] &= ~(data & AUDIO_BIS_MASK); // write-1-to-clear BIS
            audio_ctrl_regs[offset] = (audio_ctrl_regs[offset] & AUDIO_BIS_MASK) | (data & ~AUDIO_BIS_MASK);
            (void)old_bis;
            check_audio_irq();
            break;
        }
        case AUDIO_CHANNEL_STOP: {
            // ROOT CAUSE of total silence: this register is write-1-to-CLEAR
            // (real hardware/MAME's audio_ctrl_w), not a plain overwrite.
            // Several places in our own code latch a channel's stop bit when
            // its one-shot sample finishes (audio_fetch_sample et al); with
            // no case here, this fell into the generic `default:` plain
            // overwrite below, so a stop bit could never actually be
            // cleared once set - CHANNEL_ENABLE's own handler explicitly
            // skips restarting a channel while its stop bit is set, so
            // every channel permanently died the first time it ever
            // finished a sample, until nothing could play at all.
            uint16_t old = audio_ctrl_regs[offset];
            audio_ctrl_regs[offset] &= ~data;
            uint16_t changed = old ^ audio_ctrl_regs[offset];
            for (int ch = 0; ch < 16; ch++) {
                uint16_t mask = 1 << ch;
                if (!(changed & mask)) continue;
                if (!(audio_ctrl_regs[AUDIO_CHANNEL_ENABLE] & mask)) continue;
                if (!(audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask)) audio_start_channel(ch);
            }
            break;
        }
        default:
            audio_ctrl_regs[offset] = data;
            break;
    }
}

// Producer/consumer ring buffer between generate_audio_frame() (called once
// per simulation tick, in whatever batch size the tick's elapsed time
// demands) and raylib's AudioStream (which wants fixed-size chunks, only
// when it has actually finished the previous one).
static const int AUDIO_QUEUE_CAPACITY_FRAMES = 1 << 16; // stereo frames
static int16_t g_audio_queue[AUDIO_QUEUE_CAPACITY_FRAMES * 2];
static int g_audio_queue_head = 0; // next write position (frames)
static int g_audio_queue_tail = 0; // next read position (frames)
static int g_audio_queue_count = 0; // frames currently buffered

// Real hardware's audio DAC clock is derived from the same 27MHz master
// clock as the CPU, 1 sample per 384 cycles (27000000/70312.5 = 384 exactly)
// - and critically, ticks INTERLEAVED with CPU execution, cycle by cycle,
// not in a single lump sum after a whole frame's worth of instructions have
// already run (see the cpu.step() loop, where this actually drives
// generation - a prior wall-clock-paced batch approach here was the real
// root cause of a ~10% slow-tempo bug).
double audio_cycle_debt = 0.0;
AudioStream audio_stream;

void audio_queue_push(const int16_t* buf, int num_frames) {
    if (g_selftest) {
        for (int i = 0; i < num_frames * 2; i++) {
            g_selftest_audio_hash = (g_selftest_audio_hash ^ (uint16_t)buf[i]) * 16777619u;
        }
    }
    for (int i = 0; i < num_frames; i++) {
        if (g_audio_queue_count >= AUDIO_QUEUE_CAPACITY_FRAMES) break; // drop on overflow
        g_audio_queue[g_audio_queue_head * 2 + 0] = buf[i * 2 + 0];
        g_audio_queue[g_audio_queue_head * 2 + 1] = buf[i * 2 + 1];
        g_audio_queue_head = (g_audio_queue_head + 1) % AUDIO_QUEUE_CAPACITY_FRAMES;
        g_audio_queue_count++;
    }
}

void audio_queue_feed_stream(AudioStream stream) {
    // ROOT CAUSE of choppy audio: raylib's internal stream buffer is sized
    // via SetAudioStreamBufferSizeDefault(AUDIO_STREAM_CHUNK=4096), and
    // IsAudioStreamProcessed() only reports true once a full buffer of that
    // size has finished playing. Handing over only 1024 frames per "ready"
    // signal was refilling a quarter of what raylib actually needed each
    // time, so the real output device kept running dry between top-ups even
    // though our own software queue was sitting permanently full (confirmed
    // via trace: queue_count pinned at max capacity the whole session).
    // Matching this to AUDIO_STREAM_CHUNK fixes the mismatch.
    const int CHUNK = AUDIO_STREAM_CHUNK; // frames per UpdateAudioStream call
    while (g_audio_queue_count >= CHUNK && IsAudioStreamProcessed(stream)) {
        static int16_t chunk[CHUNK * 2];
        for (int i = 0; i < CHUNK; i++) {
            chunk[i * 2 + 0] = g_audio_queue[g_audio_queue_tail * 2 + 0];
            chunk[i * 2 + 1] = g_audio_queue[g_audio_queue_tail * 2 + 1];
            g_audio_queue_tail = (g_audio_queue_tail + 1) % AUDIO_QUEUE_CAPACITY_FRAMES;
        }
        g_audio_queue_count -= CHUNK;
        UpdateAudioStream(stream, chunk, CHUNK);
    }
}
