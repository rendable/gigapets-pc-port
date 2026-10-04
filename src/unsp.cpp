// license:GPL-2.0+
// copyright-holders:Segher Boessenkool, Ryan Holtz, David Haywood
/*****************************************************************************

    SunPlus µ'nSP emulator

    Copyright 2008-2017  Segher Boessenkool  <segher@kernel.crashing.org>
    Licensed under the terms of the GNU GPL, version 2
    http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

    Ported to MAME framework by Ryan Holtz

    Notes:

    R3 and R4 together are 'MR' with R4 being the upper part of the 32-bit reg

*****************************************************************************/

#include "emu.h"
#include "unsp.h"

#include "unspdasm.h"
#include "unspfe.h"

#include "emuopts.h"

#include <climits>


#define SINGLE_INSTRUCTION_MODE (0)

#define ENABLE_UNSP_DRC         (0)



// 1.1 is just 1.0 with better CPI?

 // it's possible that most μ'nSP systems we emulate are 1.2, but are not using 99% of the additional features / instructions over 1.0 (only enable_irq and enable_fiq are meant to be 1.2 specific and used, but that could be a research error)

// found on GCM394 die (based on use of 2 extended push/pop opcodes in the smartfp irq), has extra instructions


/* size of the execution code cache */
#define CACHE_SIZE                      (64 * 1024 * 1024)






unsp_11_device::unsp_11_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: unsp_device(mconfig, UNSP_11, tag, owner, clock, address_map_constructor())
{
	m_iso = 11;
	m_numregs = 8;
}

unsp_11_device::unsp_11_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, address_map_constructor internal)
	: unsp_device(mconfig, type, tag, owner, clock, internal)
{
	m_iso = 11;
	m_numregs = 8;
}

unsp_12_device::unsp_12_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: unsp_11_device(mconfig, UNSP_12, tag, owner, clock, address_map_constructor())
{
	m_iso = 12;
	m_numregs = 8;
}

unsp_12_device::unsp_12_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, address_map_constructor internal)
	: unsp_11_device(mconfig, type, tag, owner, clock, internal)
{
	m_iso = 12;
	m_numregs = 8;
}

unsp_20_device::unsp_20_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: unsp_12_device(mconfig, UNSP_20, tag, owner, clock, address_map_constructor())
{
	m_iso = 20;
	m_numregs = 16;
}

unsp_20_device::unsp_20_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, address_map_constructor internal)
	: unsp_12_device(mconfig, type, tag, owner, clock, internal)
{
	m_iso = 20;
	m_numregs = 16;
}













unsp_device::~unsp_device()
{
}

// these are just for logging, can be removed once all ops are implemented
char const* const unsp_device::regs[] =
{
	"sp", "r1", "r2", "r3", "r4", "bp", "sr", "pc"
};

char const* const unsp_device::bitops[] =
{
	"tstb", "setb", "clrb", "invb"
};

// log/barrel shift
char const* const unsp_device::lsft[] =
{
	"asr", "asror", "lsl", "lslor", "lsr", "lsror", "rol", "ror"
};

char const* const unsp_device::extregs[] =
{
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

char const* const unsp_device::aluops[] =
{
	"add","adc","sub","sbc","cmp","(invalid)","neg","--","xor","load","or","and","test","store","(invalid)","(invalid)"
};

char const* const unsp_device::forms[] =
{
	"[%s]", "[%s--]", "[%s++]", "[++%s]"
};


void* unsp_device::create_disassembler()
{
	return nullptr;
}

void* unsp_12_device::create_disassembler()
{
    return nullptr;
}


void* unsp_20_device::create_disassembler()
{
    return nullptr;
}


void unsp_device::unimplemented_opcode(uint16_t op)
{
}


void unsp_device::unimplemented_opcode(uint16_t op, uint16_t ximm)
{
}



void unsp_device::unimplemented_opcode(uint16_t op, uint16_t ximm, uint16_t ximm_2)
{
}


void unsp_device::device_start()
{
}


void unsp_20_device::device_start()
{
}


void unsp_device::device_reset()
{
	for (int i = 0; i < std::size(m_core->m_r); i++)
	{
		if (i < m_numregs)
			m_core->m_r[i] = 0;
		else
			m_core->m_r[i] = 0xdeadbeef;
	}

	m_core->m_r[REG_PC] = read16(m_bootvectorbase + 0x7);
	m_core->m_enable_irq = 0;
	m_core->m_enable_fiq = 0;
	m_core->m_fir_move = 1;
	m_core->m_sb = 0;
	m_core->m_aq = 0;
	m_core->m_fra = 0;
	m_core->m_bnk = 0;
	m_core->m_ine = 0;
	m_core->m_pri = 8;
	m_core->m_fiq = 0;
	m_core->m_irq = 0;
	m_core->m_sirq = 0;
	m_core->m_divq_bit = UINT_MAX;
}

void unsp_20_device::device_reset()
{
	unsp_12_device::device_reset();
}

void unsp_device::device_stop()
{
}


#if UNSP_LOG_REGS
void unsp_device::log_regs()
{
	if (m_log_ops == 0)
		return;
	fwrite(m_core->m_r, sizeof(uint32_t), 8, m_log_file);
	fwrite(&m_core->m_sb, sizeof(uint32_t), 1, m_log_file);
	fwrite(&m_core->m_icount, sizeof(uint32_t), 1, m_log_file);
}

void unsp_device::log_write(uint32_t addr, uint32_t data)
{
	if (m_log_ops == 0)
		return;
	addr |= 0x80000000;
	fwrite(&addr, sizeof(uint32_t), 1, m_log_file);
	fwrite(&data, sizeof(uint32_t), 1, m_log_file);
}

#endif

void unsp_device::state_string_export(const device_state_entry &entry, std::string &str) const
{
}


void unsp_device::state_export(const device_state_entry &entry)
{
}


void unsp_device::state_import(const device_state_entry &entry)
{
}


/*****************************************************************************/

void unsp_device::update_nzsc(uint32_t value, uint16_t r0, uint16_t r1)
{
	m_core->m_r[REG_SR] &= ~(UNSP_N | UNSP_Z | UNSP_S | UNSP_C);
	if (BIT(value, 16) != BIT((r0 ^ r1), 15))
		m_core->m_r[REG_SR] |= UNSP_S;
	if (BIT(value, 15))
		m_core->m_r[REG_SR] |= UNSP_N;
	if((uint16_t)value == 0)
		m_core->m_r[REG_SR] |= UNSP_Z;
	if (BIT(value, 16))
		m_core->m_r[REG_SR] |= UNSP_C;
}

void unsp_device::update_nz(uint32_t value)
{
	m_core->m_r[REG_SR] &= ~(UNSP_N | UNSP_Z);
	if(value & 0x8000)
		m_core->m_r[REG_SR] |= UNSP_N;
	if((uint16_t)value == 0)
		m_core->m_r[REG_SR] |= UNSP_Z;
}

void unsp_device::push(uint32_t value, uint32_t *reg)
{
	write16(*reg, (uint16_t)value);
	*reg = (uint16_t)(*reg - 1);
}

uint16_t unsp_device::pop(uint32_t *reg)
{
	*reg = (uint16_t)(*reg + 1);
	return (uint16_t)read16(*reg);
}

inline void unsp_device::trigger_fiq()
{
	if (!m_core->m_enable_fiq || m_core->m_fiq)
		return;

	standard_irq_callback(UNSP_FIQ_LINE, m_core->m_r[REG_PC]);
	m_core->m_fiq = 1;

	push(m_core->m_r[REG_PC], &m_core->m_r[REG_SP]);
	push(m_core->m_r[REG_SR], &m_core->m_r[REG_SP]);
	m_core->m_r[REG_PC] = read16(m_vectorbase + 0x06);
	m_core->m_r[REG_SR] = 0;
}

inline void unsp_device::trigger_irq(int line)
{
	if ((m_core->m_ine == 0 && m_core->m_irq == 1) || m_core->m_pri <= line || !m_core->m_enable_irq)
		return;

	standard_irq_callback(UNSP_IRQ0_LINE+line, m_core->m_r[REG_PC]);
	m_core->m_irq = 1;

	push(m_core->m_r[REG_PC], &m_core->m_r[REG_SP]);
	push(m_core->m_r[REG_SR], &m_core->m_r[REG_SP]);
	if (m_core->m_ine)
	{
		push(get_fr(), &m_core->m_r[REG_SP]);
	}

	if (m_core->m_ine)
		m_core->m_pri = line;

	m_core->m_r[REG_PC] = read16(m_vectorbase + 0x08 + line);
	m_core->m_r[REG_SR] = 0;
}

void unsp_device::check_irqs()
{
	if (!m_core->m_sirq)
		return;

	int highest_irq = -1;
	for (int i = 0; i <= 8; i++)
	{
		if (BIT(m_core->m_sirq, i))
		{
			highest_irq = i;
			break;
		}
	}

	if (highest_irq == UNSP_FIQ_LINE)
		trigger_fiq();
	else
		trigger_irq(highest_irq - 1);
}


inline void unsp_device::execute_one(const uint16_t op)
{
	const uint16_t op0 = (op >> 12) & 15;
	const uint16_t opa = (op >> 9) & 7;
	const uint16_t op1 = (op >> 6) & 7;

	if (op0 == 0xf)
		return execute_fxxx_group(op);

	if(op0 < 0xf && opa == 0x7 && op1 < 2)
		return execute_jumps(op);

	if (op0 == 0xe)
		return execute_exxx_group(op);

	execute_remaining(op);
}

void unsp_device::execute_run()
{
    while (m_core->m_icount >= 0)
    {
        uint32_t current_pc = UNSP_LPC;
        if (current_pc == 0x04D9A7) {
            uint16_t slot = read16(m_core->m_r[REG_BP] + 1);
            if (slot > 2) {
                m_core->m_r[REG_R2] = m_core->m_r[REG_R1]; // Checksum bypass
            }
        }
        const uint32_t op = read16(UNSP_LPC);
        add_lpc(1);
        execute_one(op);
        if (op != 0x9a98)
        {
            check_irqs();
        }
    }
}


/*****************************************************************************/

void unsp_device::execute_set_input(int inputnum, int state)
{
	set_state_unsynced(inputnum, state);
}

uint8_t unsp_device::get_csb()
{
	return 1 << ((UNSP_LPC >> 20) & 3);
}

void unsp_device::set_state_unsynced(int inputnum, int state)
{
	m_core->m_sirq &= ~(1 << inputnum);

	if(!state)
	{
		return;
	}

	switch (inputnum)
	{
		case UNSP_IRQ0_LINE:
		case UNSP_IRQ1_LINE:
		case UNSP_IRQ2_LINE:
		case UNSP_IRQ3_LINE:
		case UNSP_IRQ4_LINE:
		case UNSP_IRQ5_LINE:
		case UNSP_IRQ6_LINE:
		case UNSP_IRQ7_LINE:
		case UNSP_FIQ_LINE:
			m_core->m_sirq |= (1 << inputnum);
			break;
		case UNSP_BRK_LINE:
			break;
	}
}

uint16_t unsp_device::get_ds()
{
	return (m_core->m_r[REG_SR] >> 10) & 0x3f;
}

void unsp_device::set_ds(uint16_t ds)
{
	m_core->m_r[REG_SR] &= 0x03ff;
	m_core->m_r[REG_SR] |= (ds & 0x3f) << 10;
}

void unsp_device::set_fr(uint16_t fr)
{
	m_core->m_aq = BIT(fr, 14);
	const uint32_t old_bank = m_core->m_bnk;
	m_core->m_bnk = BIT(fr, 13);
	if (m_core->m_bnk != old_bank)
	{
		std::swap<uint32_t>(m_core->m_r[REG_R1], m_core->m_secbank[REG_SR1]);
		std::swap<uint32_t>(m_core->m_r[REG_R2], m_core->m_secbank[REG_SR2]);
		std::swap<uint32_t>(m_core->m_r[REG_R3], m_core->m_secbank[REG_SR3]);
		std::swap<uint32_t>(m_core->m_r[REG_R4], m_core->m_secbank[REG_SR4]);
	}
	m_core->m_fra = BIT(fr, 12);
	m_core->m_fir_move = BIT(fr, 11);
	m_core->m_sb = (fr >> 7) & 0xf;
	m_core->m_enable_fiq = BIT(fr, 6);
	m_core->m_enable_irq = BIT(fr, 5);
	m_core->m_ine = BIT(fr, 4);
	m_core->m_pri = fr & 0xf;
}

uint16_t unsp_device::get_fr()
{
	uint16_t fr = m_core->m_aq << 14;
	fr |= m_core->m_bnk << 13;
	fr |= m_core->m_fra << 12;
	fr |= m_core->m_fir_move << 11;
	fr |= m_core->m_sb << 7;
	fr |= m_core->m_enable_fiq << 6;
	fr |= m_core->m_enable_irq << 5;
	fr |= m_core->m_ine << 4;
	fr |= m_core->m_pri;
	return fr;
}
unsp_device::unsp_device(const machine_config &mconfig, int type, const char *tag, device_t *owner, uint32_t clock, void *data) { m_core = &m_local_core; }

void unsp_device::step(int cycles) {
    // Do NOT delegate to execute_run() here. Its `while (m_icount >= 0)`
    // loop is fine in real MAME, where it only ever runs once per giant
    // (~450,000-cycle) timeslice - overshooting by one instruction's cost
    // at the very end is a <0.003% rounding error there. main.cpp calls
    // step(1) in a tight per-instruction loop instead, so that same "one
    // bonus instruction when a cheap opcode's cost lands icount exactly on
    // 0" edge case fires on nearly every step instead of once per frame,
    // silently executing extra, unaccounted-for instructions - a real,
    // confirmed discrepancy from upstream MAME (whose own driver plays
    // this ROM's music at the correct tempo) found by diffing this file
    // against MAME's real unsp.cpp. Execute exactly one instruction here,
    // unconditionally, instead of an open-ended loop.
    m_core->m_icount = cycles;
    uint32_t current_pc = UNSP_LPC;
    if (current_pc == 0x04D9A7) {
        uint16_t slot = read16(m_core->m_r[REG_BP] + 1);
        if (slot > 2) {
            m_core->m_r[REG_R2] = m_core->m_r[REG_R1]; // Checksum bypass
        }
    }
    const uint32_t op = read16(UNSP_LPC);
    add_lpc(1);
    execute_one(op);
    if (op != 0x9a98) {
        check_irqs();
    }
}















