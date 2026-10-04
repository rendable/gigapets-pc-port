// SPG2xx audio: 16-channel PCM/ADPCM synth, envelopes, beat timer, and the sample ring
// buffer feeding raylib. Ported from MAME (src/devices/machine/spg2xx_audio.cpp,
// BSD-3-Clause, Ryan Holtz / Jonathan Gevaryahu; src/devices/sound/imaadpcm.cpp,
// BSD-3-Clause, Andrew Gardner / Aaron Giles).
#pragma once

#include "base.h"
#include "machine.h"

extern uint16_t audio_regs[0x200];
extern uint16_t audio_phase_regs[0x200];
extern uint16_t audio_ctrl_regs[0x20];
extern double audio_channel_rate[16];
extern double audio_channel_rate_accum[16];
extern uint8_t audio_sample_shift[16];
extern uint32_t audio_sample_count[16];
extern uint32_t audio_rampdown_frame[16];
extern uint32_t audio_envclk_frame[16];
extern uint32_t audio_envelope_addr_rt[16];
extern int32_t audio_ima_signal[16];
extern int32_t audio_ima_step[16];
extern uint16_t audio_adpcm36_remaining[16];
extern uint16_t audio_adpcm36_header[16];
extern uint16_t audio_curr_beat_base_count;
extern int32_t audio_adpcm36_prevsamp[16][2];
extern double audio_cycle_debt;
extern AudioStream audio_stream;

void audio_ima_compute_tables();
int16_t audio_ima_clock(int ch, uint8_t nibble);
uint16_t audio_decode_adpcm36_nybble(int ch, uint8_t data);
void audio_stop_channel(int ch);
void audio_start_channel(int ch);
void audio_loop_channel(int ch);
bool audio_fetch_sample(int ch);
bool audio_advance_channel(int ch);
void audio_rampdown_tick(int ch);
bool audio_envelope_tick(int ch);
void check_audio_irq();
void audio_beat_tick();
void generate_audio_frame(int16_t* out, int num_frames);
uint16_t audio_r(uint32_t offset);
void audio_w(uint32_t offset, uint16_t data);
uint16_t audio_phase_r(uint32_t offset);
void audio_phase_w(uint32_t offset, uint16_t data);
void audio_reset();
void audio_ctrl_w(uint32_t offset, uint16_t data);
void audio_queue_push(const int16_t* buf, int num_frames);
void audio_queue_feed_stream(AudioStream stream);
