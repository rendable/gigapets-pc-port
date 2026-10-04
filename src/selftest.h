// Deterministic regression harness (GIGAPETS_SELFTEST=1). See tools/selftest.ps1.
#pragma once

#include "base.h"

extern bool g_selftest;
extern uint32_t g_selftest_audio_hash;

uint16_t selftest_buttons(long frame);
void selftest_apply_script(long frame);
bool selftest_checkpoint(long frame, const Color* framebuffer);
