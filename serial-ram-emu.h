#pragma once

#include "hardware/pio.h"

typedef struct {
	PIO pio;
	int data_pin_base, addr_pin_base;
	volatile uint16_t *aligned_buffer;
	bool capture;
	volatile uint16_t *capture_buffer;
	int capture_buffer_size; // in elements

	uint address_in_offset, data_out_echo_offset;
	uint address_in_sm, data_out_echo_sm;

	int address_channel, address2_channel, data_channel, capture_channel;

	dma_channel_config address_cfg;
	dma_channel_config capture_cfg;

	volatile uint32_t *address_channel_dest;
	int address_channel_transcount;
	volatile uint16_t *capture_channel_dest;
	int capture_channel_transcount;
} SerialRamEmu;

#ifdef __cplusplus
extern "C" {
#endif
	void serial_ram_emu_init(SerialRamEmu *s, PIO pio, int addr_pin_base, int data_pin_base, volatile uint16_t *aligned_buffer, volatile uint16_t *capture_buffer, int capture_buffer_size);
	void serial_ram_emu_start(SerialRamEmu *s);
	void serial_ram_emu_stop(SerialRamEmu *s);
#ifdef __cplusplus
}
#endif
