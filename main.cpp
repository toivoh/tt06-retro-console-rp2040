#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <hardware/pwm.h>
#include <pico/multicore.h>

#include <array>
#include <cstdio>

#include "tt_setup.h"
#include "tt_pins.h"

#include "common.h"

#include "build/serial-ram-emu.pio.h"



#define DESIGN_NUM 458

//#define HALF_FREQ
//#define THIRD_FREQ
//#define QUARTER_FREQ

#if TT_CLOCK_PIN != CLK
#error "TT_CLOCK_PIN (from pio file) != CLK (from tt_pins.h)"
#endif


ram_uint16 __attribute__((section(".spi_ram.emu_ram"))) emu_ram[EMU_RAM_ELEMENTS];


void start_clock(void) {
	// Set up TT clock using PWM
	// =========================
	gpio_set_function(CLK, GPIO_FUNC_PWM);
	uint tt_clock_slice_num = pwm_gpio_to_slice_num(CLK);

	// Period 2, one cycle low and one cycle high
	pwm_set_wrap(tt_clock_slice_num, 1);
	pwm_set_chan_level(tt_clock_slice_num, CLK & 1, 1);
	// The clock doesn't start until the pwm is enabled
	// Enable the PWM, starts the TT clock
	pwm_set_enabled(tt_clock_slice_num, true);
}

void start_clock_loop(void) {
	printf("calling start_clock\r\n");
	start_clock();
	while (true) {}
}



int main() {
	//set_sys_clock_khz(133000, true);
#ifdef QUARTER_FREQ
	set_sys_clock_pll(1056 * MHZ, 7, 6); //  25.142857142857142 MHz
#elif defined THIRD_FREQ
	set_sys_clock_pll(1416 * MHZ, 7, 6); //  33.57142857142857 MHz
#elif defined HALF_FREQ
	set_sys_clock_pll(1512 * MHZ, 5, 6); //  50.4 MHz
	//set_sys_clock_pll(840 * MHZ, 7, 3); //  40 MHz !!!
#else
	set_sys_clock_pll(1512 * MHZ, 5, 3); // 100.8 MHz
#endif


	stdio_init_all();

	// Uncomment to pause until the USB is connected before continuing
	//while (!stdio_usb_connected());

	sleep_ms(20);
	printf("Selecting tt06-retro-console\n");
	sleep_ms(10);

	//tt_select_design(1);

	tt_select_design(DESIGN_NUM);
	printf("Selected\n");
	sleep_ms(10);

	// Reset
	tt_set_input_byte(0b00001111); // ui[0] = 1 at reset ==> rx_in[1:0] is connected to ui[5:4], which we set to zero

	// Clock in reset
	for (int i = 0; i < 10; i++) tt_clock_project_once();

	// Take out of reset
	gpio_put(nRST, 1);

//	while (1) {
//		tt_clock_project_once();
//		sleep_ms(100);
//		printf("%d\n", tt_get_output_byte());
//	}

/*
	start_clock();

	//while (true) {}
	while (true) {
		for (int i = 0; i < 15; i++) {
			if (i == 0b1101) continue;
			tt_set_input_byte(i & 15);
		}
	}
*/

	//start_clock();
	//serial_ram_emu_main();
	demo_main();
}
