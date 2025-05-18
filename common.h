#pragma once

#include "tt_pins.h"

#define ADDRESS_PIN_BASE UIO0
#define DATA_PIN_BASE IN0
#define VSYNC_PIN OUT3
#define HSYNC_PIN OUT7

#define EMU_RAM_ELEMENTS 65536


typedef volatile uint16_t ram_uint16;
//typedef uint16_t ram_uint16;

//extern volatile uint16_t emu_ram[EMU_RAM_ELEMENTS];
extern ram_uint16 emu_ram[EMU_RAM_ELEMENTS];

#ifdef __cplusplus
extern "C" {
#endif
	void start_clock(void);
	void start_clock_loop(void);

	void serial_ram_emu_main(void);
	void demo_main(void);
#ifdef __cplusplus
}
#endif

