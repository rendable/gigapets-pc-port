// Emulated 93C66 serial EEPROM (bit-banged over GPIO port B) plus save-file persistence.

#include "common.h"

bool eeprom_locked = true; // moved up from its EEPROM-state block below so call_rom_function can see it

// EEPROM (93C66, 16-bit words, 256 cells, 8-bit address) bit-banged over
// Port B: bit0=CS, bit1=CLK, bit2=DI (writes), bit3=DO (reads at 0x3D06).
// Protocol per the real 93Cxx serial EEPROM state machine: CS high arms it,
// then a start bit (DI=1) + 2-bit opcode + 8-bit address are clocked in MSB
// first; opcode 1=WRITE (clock in 16 data bits), 2=READ (clock out 16 bits),
// 0+top-2-address-bits 3=UNLOCK (erase/write enable), 0 0=LOCK.
uint16_t eeprom_data[256];
EepromState eeprom_state = EE_RESET;
bool eeprom_cs = false, eeprom_clk = false, eeprom_di = false;
uint32_t eeprom_cmd_addr_accum = 0;
int eeprom_bits_accum = 0;
uint32_t eeprom_shift = 0;
int eeprom_address = 0;

void eeprom_load() {
    // A never-written 93C66 reads back as all 1s (0xFFFF per word), the erased state. The ROM's
    // "valid save, or first-time setup?" check keys off that blank signature, so a missing save
    // file must start as 0xFF, not zero (zero looks like a corrupt existing save).
    memset(eeprom_data, 0xFF, sizeof(eeprom_data));
    FILE* f = fopen(app_path("resources/data/gigapets_save.eep").c_str(), "rb");
    if (f) { fread(eeprom_data, sizeof(uint16_t), 256, f); fclose(f); }
}

void eeprom_save() {
    // Write to a temp file then rename over the real one, so a crash or kill
    // mid-write can't leave a truncated/empty save (which loads as a fresh game).
    std::string path = app_path("resources/data/gigapets_save.eep");
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    size_t written = fwrite(eeprom_data, sizeof(uint16_t), 256, f);
    fclose(f);
    if (written != 256) { std::remove(tmp.c_str()); return; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::remove(tmp.c_str());
}

void eeprom_cs_write(bool state) {
    if (state == eeprom_cs) return;
    bool rising = state;
    bool falling = !state;
    eeprom_cs = state;
    if (eeprom_state == EE_RESET) {
        if (rising) eeprom_state = EE_WAIT_START;
    } else if (falling) {
        eeprom_state = EE_RESET;
    }
}

void eeprom_di_write(bool state) {
    eeprom_di = state;
}

void eeprom_clk_write(bool state) {
    if (state == eeprom_clk) return;
    bool rising = state;
    eeprom_clk = state;
    if (!rising) return;

    switch (eeprom_state) {
        case EE_WAIT_START:
            if (eeprom_di) {
                eeprom_cmd_addr_accum = 0;
                eeprom_bits_accum = 0;
                eeprom_state = EE_WAIT_CMD;
            }
            break;
        case EE_WAIT_CMD: {
            eeprom_cmd_addr_accum = (eeprom_cmd_addr_accum << 1) | (eeprom_di ? 1 : 0);
            eeprom_bits_accum++;
            if (eeprom_bits_accum == 10) { // 2-bit opcode + 8-bit address
                int opcode = (eeprom_cmd_addr_accum >> 8) & 3;
                eeprom_address = eeprom_cmd_addr_accum & 0xFF;
                eeprom_bits_accum = 0;
                if (opcode == 1) { // WRITE
                    eeprom_shift = 0;
                    eeprom_state = EE_WAIT_DATA;
                } else if (opcode == 2) { // READ
                    eeprom_shift = 0;
                    eeprom_state = EE_READING;
                } else if (opcode == 0) { // LOCK/UNLOCK/WRITEALL/ERASEALL
                    int sub = (eeprom_address >> 6) & 3;
                    if (sub == 0) eeprom_locked = true;       // LOCK
                    else if (sub == 3) eeprom_locked = false; // UNLOCK (EWEN)
                    eeprom_state = EE_RESET;
                } else { // ERASE - not needed for this game, treat as a no-op completion
                    eeprom_state = EE_WAIT_COMPLETE;
                }
            }
            break;
        }
        case EE_READING:
            if (eeprom_bits_accum % 16 == 0) {
                // Sequential read: while the host keeps clocking past a word's 16 bits with CS held, the
                // real chip auto-increments its address and streams the next word, with no new
                // start bit/opcode/address. Without advancing here, a multi-word read kept returning
                // the first word and later fields came out as garbage.
                if (eeprom_bits_accum != 0) eeprom_address = (eeprom_address + 1) & 0xFF;
                uint16_t val = (eeprom_address < 256) ? eeprom_data[eeprom_address] : 0xFFFF;
                eeprom_shift = ((uint32_t)val) << 16;
            } else {
                eeprom_shift = (eeprom_shift << 1) | 1;
            }
            eeprom_bits_accum++;
            break;
        case EE_WAIT_DATA:
            eeprom_shift = (eeprom_shift << 1) | (eeprom_di ? 1 : 0);
            eeprom_bits_accum++;
            if (eeprom_bits_accum == 16) {
                if (!eeprom_locked && eeprom_address < 256) {
                    eeprom_data[eeprom_address] = (uint16_t)(eeprom_shift & 0xFFFF);
                    eeprom_save();
                }
                eeprom_state = EE_WAIT_COMPLETE;
            }
            break;
        default:
            break;
    }
}

bool eeprom_do_read() {
    if (eeprom_state == EE_READING) return (eeprom_shift & 0x80000000) != 0;
    return true; // pulled up / ready in every other state
}
