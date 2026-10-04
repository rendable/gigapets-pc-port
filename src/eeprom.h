// Emulated 93C66 serial EEPROM (bit-banged over GPIO port B) plus save-file persistence.
#pragma once

#include "base.h"
#include "machine.h"

enum EepromState { EE_RESET, EE_WAIT_START, EE_WAIT_CMD, EE_READING, EE_WAIT_DATA, EE_WAIT_COMPLETE };

extern bool eeprom_locked;
extern uint16_t eeprom_data[256];
extern EepromState eeprom_state;
extern bool eeprom_cs, eeprom_clk, eeprom_di;
extern uint32_t eeprom_cmd_addr_accum;
extern int eeprom_bits_accum;
extern uint32_t eeprom_shift;
extern int eeprom_address;

void eeprom_load();
void eeprom_save();
void eeprom_cs_write(bool state);
void eeprom_di_write(bool state);
void eeprom_clk_write(bool state);
bool eeprom_do_read();
