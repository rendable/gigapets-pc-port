// Per-instruction hooks keyed on the emulated PC, plus the hidden test-menu unlock state.

#include "common.h"

// Opt-in hidden test-menu auto-unlock (F9). Off by default - only runs when
// the player presses the hotkey, which forces a real reset (same path as
// the watchdog reset above) then drives the exact real button sequence
// during the resulting boot, instead of requiring precise manual timing.
// See the full_pc()==0x03C9E8 RAM-arm hook for the ROM-side half of this.
bool g_test_menu_seq_active = false;
int g_test_menu_seq_frame = 0;

// Set by the PlaySoundEffect(0x59) hook once the real chime actually fires
// (confirmed happens ~frame 182, not a fixed guess) - timing below is
// relative to this instead of a fixed hold duration, since releasing
// Left+Select before the real chime resets the test-mode flag to 0.
int g_test_menu_chime_frame = -1;

// Real "Exit Test Mode" calls PowerDownHardware() then spins in a genuine
// infinite do-nothing loop (byte-perfect ROM match, not our bug) - real
// hardware just turns off. It also runs TestRomChecksum, a real EEPROM
// diagnostic that deliberately erases the whole chip as part of its test
// (Eeprom_EraseRange) - real hardware behavior, but not something a PC
// player who found this via a cheat should lose their save over. Snapshot
// EEPROM before entering test mode and restore+reboot on the way out.
uint16_t g_test_mode_eeprom_backup[256];
bool g_test_mode_backup_valid = false;

// Hooks that run before each emulated instruction, keyed on the PC about to execute.
// Returns true if the machine was just reset and the caller should stop stepping this frame.
bool rom_hooks_before_step(unsp_20_device& cpu) {
    // GROUND TRUTH (captured via the call-site tracer below,
    // frame-by-frame while spawned): the real culprit is
    // HandleMiniPetRoomInputAndStoryLogic's call at ROM
    // 0x0130c8, which fires UNCONDITIONALLY every tick once
    // tracking is active (its only guard is
    // IsMiniPetTrackingEnabled(), not the link-cable flag - the
    // 0x1b19/MiniPet_HandleOverworldInput theory was a
    // different, uninvolved function). It always pushes
    // args (1,0), which inside FeedOrAdvanceMiniPet's
    // (paramFar,paramClose) convention takes the "no valid
    // target" branch and force-writes the tracking flag to 4
    // (despawn) - confirmed byte-for-byte at ROM 0x046235.
    // FeedOrAdvanceMiniPet's own top-of-function gate
    // (`if (DAT_001a37 != 0x7b) return;`, the read at ROM
    // 0x04617d) is what's supposed to make this a no-op once a
    // real minipet is idle/parked, but our synthetic Follow
    // state legitimately needs anim0 holding a live walk-pose
    // frame ID (see PlayMiniPetAnimForRoom), so it never reads
    // as the sentinel and this fires every tick instead.
    //
    // First attempt forced ram[MINIPET_ANIM_STRUCT_ADDR]=0x7B
    // directly - that "fixed" the despawn but broke Follow
    // itself: it overwrites the SAME memory every real minipet
    // reader (rendering, IsAnimationBusy, AdvanceSpriteAnimFrame)
    // also uses, so the sprite got stuck permanently repeating
    // the zap-ring frame instead of ever showing a walk pose -
    // that's what looked like "spamming the zap animation" and
    // the color drift (the ring anim uses a palette baked into
    // that same struct's other fields, restored to a stale
    // value every tick).
    //
    // Fix: leave memory alone and fake only the CPU register
    // this one comparison reads into. r1=[0x1a37] executes at
    // 0x04617d; on the step right after (PC now at 0x04617f,
    // the `cmp r1,0x7b`), override r1 back to 0x7b before that
    // compare runs. Nothing else that reads the real struct
    // value is touched.
    static bool force_r1_sentinel = false;
    if (force_r1_sentinel) {
        cpu.set_r(unsp_12_device::REG_R1, 0x7B);
        force_r1_sentinel = false;
    }
    if (g_minipet_spawned && full_pc() == ROM_MINIPET_ANIM_LOAD) {
        force_r1_sentinel = true;
    }

    // KNOWN ISSUE - Mystery Island travel: the dock trigger only
    // works if a minipet was already tracked when the room last
    // loaded (g_activeStoryObjectId, ram[0x1AA5], must read 0x68
    // there). Spawning a minipet from the Mod Menu mid-visit does
    // not retroactively update it, so the player has to leave and
    // re-enter the area (or go in and out of a building) first.
    // A real fix needs LoadRoom's per-area argument semantics
    // worked out (3 args beyond room id + position are not yet
    // understood).

    // Real hidden test-menu unlock, found by a user on real
    // hardware/MAME (not something we're bypassing - this is a
    // genuine dev-debug gate baked into the ROM). Real MAME
    // recipe: `bp 3c9e8,1,{maincpu.pb@1a4e=1;g}` - a breakpoint
    // at ROM 0x3C9E8 that force-writes RAM 0x1a4e=1 then resumes.
    // 0x3C9E8 is already bank<<16|offset (bank 3, offset 0xC9E8),
    // same convention as full_pc() elsewhere in this file, and
    // maincpu.pb is the same word-addressed RAM space used
    // throughout this port - no unit conversion needed for
    // either side. Still requires the real input combo on top
    // (hold Left+Select before the Hasbro screen for the chime,
    // release, then Up, Down, Menu, Cancel in that exact order)
    // - that part is genuine ROM logic, unaffected by this hook.
    if (full_pc() == ROM_TEST_MENU_GATE) {
        ram[TEST_MENU_UNLOCK_FLAG_ADDR] = 1;
    }
    // PlaySoundEffect's real prologue (verified via disassembly):
    // push bp,sp; sp-=2; bp=sp+1; r1=bp+5; r2=[bp+5]. At
    // 0x04D856 (right after that last instruction executes),
    // r2 holds the sound id argument (p0). 0x59 is the chime -
    // used to time the release relative to the real threshold
    // instead of a guessed fixed duration.
    if (full_pc() == ROM_PLAY_SOUND_EFFECT_ARG_READY) {
        uint16_t sound_id = cpu.get_r(unsp_12_device::REG_R2);
        if (sound_id == 0x59 && g_test_menu_seq_active && g_test_menu_chime_frame < 0) {
            g_test_menu_chime_frame = g_test_menu_seq_frame;
        }
    }
    // Real "Exit Test Mode" (PowerDownHardware, ROM 0x04F658)
    // then spins forever - real hardware just turns off. Restore
    // the pre-test-mode EEPROM snapshot (undoing TestRomChecksum's
    // real erase-everything side effect) and reboot instead of
    // hanging, so the player gets their save back and lands on
    // the main menu like turning the device back on would.
    if (full_pc() == ROM_POWER_DOWN_HARDWARE && g_test_mode_backup_valid) {
        memcpy(eeprom_data, g_test_mode_eeprom_backup, sizeof(eeprom_data));
        eeprom_save();
        g_test_mode_backup_valid = false;
        memset(ram, 0, sizeof(ram));
        memset(io, 0, sizeof(io));
        memset(video_regs, 0, sizeof(video_regs));
        audio_reset();
        cpu.device_reset();
        return true;
    }
    return false;
}

// Hooks that run after each emulated instruction: they override a register right after the
// instruction that loaded it, so the very next compare sees the patched value.
void rom_hooks_after_step(unsp_20_device& cpu) {
    // Neuter the real hardware's auto-power-off idle timer at its
    // actual source, instead of reactively detecting the hang it
    // used to cause. WaitForNextTick (ROM 0x01d2b9) computes idle
    // time via CheckIdleTimeElapsed, compares it against a
    // 480-second (8 minute) threshold with FloatCompare, and
    // calls IdleTimeoutScreen (0x01bf0e) when it's exceeded -
    // which on real hardware halts forever in a genuine self-jump
    // at 0x01bf49, waiting for a physical power button. Real
    // hardware needs that; a PC app the user closes with a normal
    // window control does not - there's no separate "off" state
    // to preserve. FloatCompare's result lands in r1 (raw
    // disassembly: "call 0x04f85c; sp+=4; cmp r1,0x1; jg
    // 0x01d320 /*IdleTimeoutScreen*/"), so forcing r1<=1 right
    // before that compare - same register-fake technique as the
    // minipet despawn fix, not a persistent memory write -
    // makes the jg never fire. IdleTimeoutScreen's own body,
    // the self-jump inside it, and the eeprom-safe full-reset
    // recovery this replaced are now all permanently
    // unreachable, so removed rather than left as dead code.
    if (full_pc() == ROM_IDLE_TIMEOUT_COMPARE) {
        cpu.set_r(unsp_12_device::REG_R1, 0);
    }

    // Quit-freeze fix (screen frozen, music still playing,
    // reported after hitting Quit). Ground-truthed via a PC
    // sampling trace: execution gets permanently stuck spinning
    // inside WaitForNextTick's vblank-tick wait (ROM
    // 0x01d2ce-0x01d2e0), which busy-waits on ram[0x13]/[0x14]
    // advancing - a counter only IrqHandlerVideo increments,
    // gated on video IRQ enable (MMIO 0x2862 bit0, see
    // check_video_irq() above). WaitForNextTick itself
    // conditionally calls Link_UpdateAndSync (0x046d9b) first,
    // which under 3 GPIO-pin conditions on 0x3D01 (bits
    // 0x400/0x100/0x200 - link-cable-detect pins, distinct from
    // the low 7 button bits this port emulates on the same
    // address) proceeds into Cart_WriteBytes. That function
    // calls Cart_SuspendAudioForTiming (0x0469fb, saves+zeroes
    // 0x2862 among others) but - confirmed via full disassembly
    // of its single exit path at 0x046d0d - never calls the
    // matching Cart_RestoreAudioAfterTiming (0x046a29): video
    // IRQs stay disabled forever once this runs, so the very
    // next WaitForNextTick call hangs permanently. On real
    // hardware with no link cable physically connected, those
    // GPIO pins float/pull to a state that fails the checks and
    // bails out before Suspend is ever called; this port's GPIO
    // emulation for that address apparently doesn't reproduce
    // that idle state, so the ROM incorrectly believes a link
    // partner is present. Rather than rework link-cable GPIO
    // emulation (this port doesn't implement real inter-device
    // linking anyway), force the final gate check to always
    // fail closed - same register-fake technique as the
    // idle-timeout fix above: 0x046c3f is "cmp r1,0" testing
    // the 0x200 bit already masked into r1 by the preceding
    // instruction, so zeroing r1 here guarantees the safe
    // no-link-partner bailout every time, matching real
    // hardware's default (unplugged) behavior without touching
    // button input or any other GPIO bit on this address.
    if (full_pc() == ROM_LINK_PARTNER_GATE) {
        cpu.set_r(unsp_12_device::REG_R1, 0);
    }

    // GeneratePaletteBlendTable (0x01cdb7, called from
    // WaitForNextTick while a fade is active) waits for the
    // vblank tick counter (ram 0x13/0x14) to change before each
    // of 256 colors - correct on real hardware, where a vblank
    // IRQ can preempt at any instruction boundary mid-spin. This
    // port only advances that tick once per fully-exhausted
    // per-frame cycle budget, so a wait that re-snapshots and
    // re-enters within the same budget window could never see
    // it change (confirmed empirically: 7400+ spins with zero
    // progress in one captured frame). Forcing the loop's own
    // "did it change" comparison (0x01ce31, "cmp r1,r3") to read
    // as unequal lets the ROM's own completion logic drive the
    // outcome exactly as if a tick had arrived - real, verified
    // fix for this specific architectural mismatch, independent
    // of the Quit-freeze below.
    if (full_pc() == ROM_PALETTE_BLEND_TICK_WAIT) {
        cpu.set_r(unsp_12_device::REG_R1, cpu.get_r(unsp_12_device::REG_R3) + 1);
    }
}

// Advances the F9 test-menu unlock sequence by one 1/60s tick.
void test_menu_tick() {
    if (g_test_menu_seq_active) {
        g_test_menu_seq_frame++;
        // Safety cap in case the chime never fires for some reason -
        // the normal end-of-sequence path is rel>=64 in the GPIO
        // override above.
        if (g_test_menu_seq_frame >= 900) g_test_menu_seq_active = false;
    }
}

// Opt-in hidden test-menu auto-unlock. Forces a real reset (same
// path the watchdog uses) so the sequence below runs against a
// guaranteed-fresh boot, then drives the exact real Left+Select
// hold / Up,Down,Menu,Back sequence automatically - see the
// g_test_menu_seq_active GPIO override and the full_pc()==0x3C9E8
// RAM-arm hook for the two halves of this.
void test_menu_unlock_begin() {
    memcpy(g_test_mode_eeprom_backup, eeprom_data, sizeof(eeprom_data));
    g_test_mode_backup_valid = true;
    memset(ram, 0, sizeof(ram));
    memset(io, 0, sizeof(io));
    memset(video_regs, 0, sizeof(video_regs));
    audio_reset();
    cpu_ptr->device_reset();
    g_test_menu_seq_active = true;
    g_test_menu_seq_frame = 0;
    g_test_menu_chime_frame = -1;
}
