#pragma once

#include <stdint.h>
#include <stdio.h>

#define DEFINE_DEVICE_TYPE(...)
#define DECLARE_DEVICE_TYPE(...)
#define DECLARE_READ16_MEMBER(...) uint16_t __VA_ARGS__(uint32_t offset, uint16_t mem_mask = ~0)
#define DECLARE_WRITE16_MEMBER(...) void __VA_ARGS__(uint32_t offset, uint16_t data, uint16_t mem_mask = ~0)
#define ALLOW_SAVE_TYPE(...)

// Endianness
#define ENDIANNESS_BIG 1
#define ENDIANNESS_LITTLE 0

// Logging
#define logerror(...) printf(__VA_ARGS__)
#define debugger_instruction_hook(...)

// Memory interface
extern uint16_t memory_read16(uint32_t addr);
extern void memory_write16(uint32_t addr, uint16_t data);

class device_t {
public:
    device_t(const char* tag) {}
    virtual ~device_t() {}
};

class cpu_device : public device_t {
public:
    cpu_device(...) : device_t("cpu") {}
    virtual void execute_run() = 0;
};

// Stub for mconfig and other MAME internals
struct machine_config {};
typedef int device_type;
typedef void* address_map_constructor;

#define UML_EXIT(...)

namespace uml {
    class code_handle {};
}
class drcuml_block {};
class drcuml_state {};
class drc_cache {};
class uml_code_handle {};
class drc_frontend {};
class drc_romentry {};
typedef uint32_t offs_t;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#define DECLARE_WRITE16_MEMBER(...) void __VA_ARGS__(uint32_t offset, uint16_t data, uint16_t mem_mask = ~0)

#define BIT(x, n) (((x) >> (n)) & 1)

#include <string>
#include <memory>
struct device_state_entry {};

#define NAME(x) #x
#define save_item(...) do {} while (0)
#define set_icountptr(...) do {} while (0)

class machine_stub {
public:
    const char* describe_context() { return ""; }
};
inline machine_stub& machine() { static machine_stub m; return m; }

// Dummy log constants
#define LOG_GENERAL 1
#define LOG_UNIMP 2

#define fatalerror(...) do {} while (0)
#define standard_irq_callback(x) 0

#define state_add(...) do {} while (0)
#define standard_irq_callback(...) 0

struct dummy_state {
    template <typename T> dummy_state& formatstr(T) { return *this; }
};
#undef state_add
#define state_add(...) dummy_state()


#define UNSP_11 0
#define UNSP 1
#define UNSP_12 2
#define UNSP_20 3
