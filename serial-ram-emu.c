#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/structs/bus_ctrl.h"

#include "build/serial-ram-emu.pio.h"
#include "serial-ram-emu.h"

#include <stdio.h>

void start_clock_loop(void);

void serial_ram_emu_init(SerialRamEmu *s, PIO pio, int addr_pin_base, int data_pin_base, volatile uint16_t *aligned_buffer, volatile uint16_t *capture_buffer, int capture_buffer_size) {
	s->pio = pio;
	s->addr_pin_base = addr_pin_base;
	s->data_pin_base = data_pin_base;
	s->aligned_buffer = aligned_buffer;
	s->capture = capture_buffer != NULL;
	s->capture_buffer = capture_buffer;
	s->capture_buffer_size = capture_buffer_size;


	// PIO
	// ===

	// Address in
	// ----------
	s->address_in_offset = pio_add_program(pio, &address_in_program);
	s->address_in_sm = pio_claim_unused_sm(pio, true);
	//address_in_program_init(pio, s->address_in_sm, s->address_in_offset, s->addr_pin_base);

	// Data out
	// --------
	gpio_set_dir_out_masked(((1 << NUM_SERIAL_PINS) - 1) << s->data_pin_base);
	s->data_out_echo_offset = pio_add_program(pio, &data_out_echo_program);
	s->data_out_echo_sm = pio_claim_unused_sm(pio, true);
	//data_out_echo_program_init(pio, s->data_out_echo_sm, s->data_out_echo_offset, s->data_pin_base);


	// DMA
	// ===

	// Prioritize DMA over CPU cores
	// ----------------------------
	hw_clear_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC0_BITS | BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	hw_set_bits(  &bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS);

	// Allocate DMA channels
	// ---------------------
	s->address_channel = dma_claim_unused_channel(true);
	s->address2_channel = dma_claim_unused_channel(true);
	s->capture_channel = s->capture ? dma_claim_unused_channel(true) : -1;
	s->data_channel = dma_claim_unused_channel(true);

	// Address channel
	// ---------------
	s->address_channel_dest = &(dma_channel_hw_addr(s->data_channel)->al3_read_addr_trig);
	//s->address_channel_dest = (volatile uint32_t *)&(pio->txf[s->data_out_echo_sm]);
	//s->address_channel_transcount = 2*800*480/4; // Just enough for one frame of pixels for now
	s->address_channel_transcount = 1; // A single transfer, then chain to the other address channel

	s->address_cfg = dma_channel_get_default_config(s->address_channel);
	channel_config_set_read_increment(&s->address_cfg, false);
	channel_config_set_write_increment(&s->address_cfg, false);
	channel_config_set_high_priority(&s->address_cfg, true);
	channel_config_set_dreq(&s->address_cfg, pio_get_dreq(pio, s->address_in_sm, false)); // dreq from RX FIFO
	channel_config_set_chain_to(&s->address_cfg, s->address2_channel);

	dma_channel_configure(s->address_channel, &s->address_cfg, s->address_channel_dest, (const volatile uint32_t *)&(pio->rxf[s->address_in_sm]), s->address_channel_transcount, false);

	// Address channel 2
	// ---------------
	// Chain between the two address channels to keep them going

	dma_channel_config address2_cfg = dma_channel_get_default_config(s->address2_channel);
	channel_config_set_read_increment(&address2_cfg, false);
	channel_config_set_write_increment(&address2_cfg, false);
	channel_config_set_high_priority(&address2_cfg, true);
	channel_config_set_dreq(&address2_cfg, pio_get_dreq(pio, s->address_in_sm, false)); // dreq from RX FIFO
	channel_config_set_chain_to(&address2_cfg, s->address_channel);

	dma_channel_configure(s->address2_channel, &address2_cfg, s->address_channel_dest, (const volatile uint32_t *)&(pio->rxf[s->address_in_sm]), s->address_channel_transcount, false);


	// Data channel
	// ------------
	volatile uint32_t *data_channel_dest = (volatile uint32_t *)&(pio->txf[s->data_out_echo_sm]);

	dma_channel_config data_cfg = dma_channel_get_default_config(s->data_channel);
	channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_16);
	channel_config_set_high_priority(&data_cfg, true);
	// Write to TX fifo for data_out_echo pio, read from address what was set through address channel
	dma_channel_configure(s->data_channel, &data_cfg, data_channel_dest, aligned_buffer, 1, false);


	// Capture channel
	// ---------------
	if (s->capture) {
		s->capture_channel_dest = capture_buffer;
		s->capture_channel_transcount = capture_buffer_size;

		s->capture_cfg = dma_channel_get_default_config(s->capture_channel);
		channel_config_set_transfer_data_size(&s->capture_cfg, DMA_SIZE_16);
		channel_config_set_read_increment(&s->capture_cfg, false);
		channel_config_set_write_increment(&s->capture_cfg, true);
		channel_config_set_dreq(&s->capture_cfg, pio_get_dreq(pio, s->data_out_echo_sm, false)); // dreq from RX FIFO

		dma_channel_configure(s->capture_channel, &s->capture_cfg, s->capture_channel_dest, (const volatile uint32_t *)&(pio->rxf[s->data_out_echo_sm]), s->capture_channel_transcount, false);
	}
}

void serial_ram_emu_start(SerialRamEmu *s) {
	// (Re)start address_in PIO program
	// --------------------------------
	// Reinit
	//address_in_program_init(s->pio, s->address_in_sm, s->address_in_offset, s->addr_pin_base);
	address_in_program_init(s->pio, s->address_in_sm, s->address_in_offset, s->addr_pin_base, s->addr_pin_base+0);

	//pio_sm_set_enabled(s->pio, s->address_in_sm, true);
	printf("pio_sm_put\r\n");
	pio_sm_put(s->pio, s->address_in_sm, ((int)s->aligned_buffer)>>17); // Initialize aligned buffer address

	// Start address DMA (waits for data from address_in program)
	//dma_channel_configure(s->address_channel, &s->address_cfg, s->address_channel_dest, (const volatile uint32_t *)&(s->pio->rxf[s->address_in_sm]), s->address_channel_transcount, true);
	printf("dma_channel_start(s->address_channel)\r\n");
	dma_channel_start(s->address_channel);

	// (Re)start data_out_echo PIO program
	// -----------------------------------
	// Reinit
	printf("data_out_echo_program_init\r\n");
	data_out_echo_program_init(s->pio, s->data_out_echo_sm, s->data_out_echo_offset, s->data_pin_base);
	//start_clock_loop();
	//pio_sm_set_enabled(s->pio, s->data_out_echo_sm, true);
	// Initialize delay timer
	//pio_sm_put(s->pio, s->data_out_echo_sm, 2*2); // TODO: put back, together with the code that reads it

	// Start capture DMA (waits for data from data_out_echo program) -- need to restore write address
	//dma_channel_configure(s->capture_channel, &s->capture_cfg, s->capture_channel_dest, (const volatile uint32_t *)&(s->pio->rxf[s->data_out_echo_sm]), s->capture_channel_transcount, true);
	if (s->capture) dma_channel_set_write_addr(s->capture_channel, s->capture_channel_dest, true);
}

void serial_ram_emu_stop(SerialRamEmu *s) {
	// Disable PIO programs
	pio_sm_set_enabled(s->pio, s->address_in_sm, false);
	pio_sm_set_enabled(s->pio, s->data_out_echo_sm, false);

	// Stop the address_in DMAs from chaining to each other by setting transfer count to zero
	dma_channel_set_trans_count(s->address_channel,  0, false);
	dma_channel_set_trans_count(s->address2_channel, 0, false);

	// Stop DMA channels
	dma_channel_abort(s->address_channel);
	dma_channel_abort(s->address2_channel);
	if (s->capture) dma_channel_abort(s->capture_channel);

	// Clear FIFOs
	pio_sm_clear_fifos(s->pio, s->address_in_sm);
	pio_sm_clear_fifos(s->pio, s->data_out_echo_sm);

	// Restore address_in DMAs transfer counts, cleared by stop
	dma_channel_set_trans_count(s->address_channel,  1, false);
	dma_channel_set_trans_count(s->address2_channel, 1, false);
}
