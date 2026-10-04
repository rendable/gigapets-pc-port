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
