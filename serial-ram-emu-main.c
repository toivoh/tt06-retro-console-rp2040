/*
 * MIT License
 *
 * Copyright (c) 2023 tinyVision.ai
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
//#include "pico/stdio.h"
#include <stdio.h>
#include <string.h>

//#include "hardware/irq.h"
//#include "hardware/gpio.h"
#include "hardware/uart.h"
//#include "ice_usb.h"
//#include "ice_fpga.h"
//#include "ice_led.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
//#include "ice_led.h"
#include "hardware/dma.h"
//#include "hardware/structs/bus_ctrl.h"

#include "serial-ram-emu.h"

#include "build/serial-ram-emu.pio.h"

//#include <tusb.h>
#include "tt_pins.h"
#include "common.h"


#define HALF_FREQ
//#define THIRD_FREQ
//#define QUARTER_FREQ

//#define RAM_ENABLE_PIN 14
//#define HSYNC_PIN 15



#if TT_CLOCK_PINT != CLK
#error "TT_CLOCK_PINT (from pio file) != CLK (from tt_pins.h)"
#endif



const int emu_ram_elements = EMU_RAM_ELEMENTS;


void tud_task(void) {} // TODO: remove


void init(volatile uint16_t *capture_buffer, int capture_buffer_size) {
	printf("entering init\r\n");
/*
	// Initialize PLL, USB, ...
	// ========================
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

	tusb_init();
	stdio_init_all();

	ice_usb_init();

	// Set up FPGA clock -- start before RAM emulator
	// ==============================================
	// TODO: Don't use RAM_ENABLE pin?
	PIO pio = pio0;
	gpio_set_dir(ICE_FPGA_CLOCK_PIN, true);
	gpio_set_dir(RAM_ENABLE_PIN, true);
	uint fpga_frame_clock_offset = pio_add_program(pio, &fpga_frame_clock_program);
	uint fpga_frame_clock_sm = pio_claim_unused_sm(pio, true);
	fpga_frame_clock_program_init(pio, fpga_frame_clock_sm, fpga_frame_clock_offset, ICE_FPGA_CLOCK_PIN, RAM_ENABLE_PIN);
	// The clock doesn't start until it gets the loop count
*/

	// Start RAM emulator
	// ==================
	SerialRamEmu ram_emu;
	printf("calling serial_ram_emu_init\r\n");
	serial_ram_emu_init(&ram_emu, pio0, ADDRESS_PIN_BASE, DATA_PIN_BASE, emu_ram, capture_buffer, capture_buffer_size);
	printf("calling serial_ram_emu_start\r\n");
	serial_ram_emu_start(&ram_emu);
	//start_clock_loop();

/*
	// Start the FPGA
	// ==============
	//clock_gpio_init(ICE_FPGA_CLOCK_PIN, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 2);
	ice_fpga_start();
	// Provide loop count, starts the FPGA clock
	pio_sm_put(pio, fpga_frame_clock_sm, 2*800*525 - fpga_frame_clock_NONLOOP_CYCLES); // Provide loop count
*/
	printf("calling start_clock\r\n");
	start_clock();
	printf("init done\r\n");
}

int capture_test(void) {
	const int aligned_buffer_size = (1 << 17);

	// Fill in aligned buffer
	// ----------------------
	/*
	memset((void *)emu_ram, 0, aligned_buffer_size);
	for (int i=0; i < aligned_buffer_size/2; i++) emu_ram[i] = i;
	*/
	for (int j=0; j < 164; j++) {
		for (int i=0; i < 400; i++) {
			int index = i + 400*j;
			if (index*sizeof(uint16_t) >= aligned_buffer_size) break;

			int c = 0;
			if (i == 0 || i == 319 || j == 0) c = 0xfff;

			int di = i-160, dj = j - 100;
			int d2 = di*di + dj*dj;
			
			if (d2 <= 8191) c += (8191 - d2) >> 9;
			if (d2 <= 2047) c += ((2047 - d2) >> 7) << 4;
			if (d2 <= 1023) c += ((1023 - d2) >> 6) << 8;

			//if (d2 < 30*30) c = 0xf00;
			//else if (d2 < 50*50) c = 0x0f0;
			//else if (d2 < 70*70) c = 0x00f;

			c = (c & 0xfff) | ((index & 0xf) << 12);

			emu_ram[index] = c;
		}
	}

	// Allocate capture buffer
	// -----------------------
	const int capture_buffer_size = 32768;
	volatile uint16_t *capture_buffer = (uint16_t *)malloc(capture_buffer_size * sizeof(uint16_t));
	// Initialize buffer once at startup -- hard to do in sync
	for (int i = 0; i < capture_buffer_size; i++) capture_buffer[i] = -1;

	init(capture_buffer, capture_buffer_size);

	uint64_t last_time = 0;
	while (true) {
		tud_task();

		uint64_t time = time_us_64();
		bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));

		if (step) {
			//printf("Testing...\r\n");
			printf("emu_ram = 0x%x\r\n", (int)emu_ram);

#ifdef ALTERNATING_ADRESS_TEST
			printf("capture_buffer = [");
			for (int i = 0; i < 16; i++) {
				printf("0x%x, ", capture_buffer[i]);
			}
			printf("]\r\n");
#else
			int num_failed = 0;
			printf("unexpected = [");
			for (int i = 0; i < capture_buffer_size-1; i++) {
				tud_task();

				uint16_t data = capture_buffer[i];
				uint16_t expected = emu_ram[i];

				if (data != expected) {
					num_failed++;
					if (num_failed < 20) {
						printf("[0x%x, 0x%x, 0x%x],", i, data, expected);
					}
				}
			}
			printf("]\r\n");

			if (num_failed == 0) printf("All addresses ok\r\n");
			else printf("%d checks FAILED\r\n", num_failed);
#endif

			printf("\r\n");
		}
	}

	return 0;
}

int fb_test(void) {
	// Fill in frame buffer
	// --------------------
	for (int j=0; j < 200; j++) {
		for (int i=0; i < 320; i++) {
			int index = i + 320*j;
			if (index >= emu_ram_elements) break;

			int c = 0;

			int di = i-160, dj = j - 100;
			int d2 = di*di + dj*dj + 16384/16;

			if (d2 < 16384) c += (16384 - d2) >> 10;
			if (d2 < 8192) c += ((8192 - d2) >> 9) << 4;
			if (d2 < 4096) c += ((4096 - d2) >> 8) << 8;

			if (i == 0 || i == 319 || j == 0 || j == 199) c = 0xfff;

			c = (c & 0xfff) | ((index & 0xf) << 12);

			emu_ram[index] = c;
		}
	}

	init(NULL, 0);

	uint64_t last_time = 0;
	while (true) {
		tud_task();
		uint64_t time = time_us_64();
		bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));
		if (step) printf("FB test...\r\n");
	}

	return 0;
}

int tilemap_test0(void) {
	memset((void *)emu_ram, -1, sizeof(emu_ram));
	// Fill in tile buffer and pixels
	// ------------------------------
	for (int j=0; j < 30; j++) {
		for (int i=0; i < 40; i++) {
			int index = i + 40*j;
			// Tile buffer
			emu_ram[index] = index + 0x8000;
			// Pixels
			for (int k = 0; k < 8; k++) {
				int r = i & 15;
				int g = (i&j&((k << 1)+1)) & 15;
				int b = j & 15;
				emu_ram[index + (k<<12) + 0x8000] = (r << 8) | (g << 4) | b;
			}
		}
	}

	init(NULL, 0);

	uint64_t last_time = 0;
	while (true) {
		tud_task();
		uint64_t time = time_us_64();
		bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));
		if (step) printf("Tile map test...\r\n");
	}

	return 0;
}

void tilemap_test_setup(void);


int clamp07(int x) { if (x <= 0) return 0; else if (x >= 7) return 7; else return x; }


enum {NUM_SPRITES=64};

int sprite_test(bool loop) {
	//memset((void *)emu_ram, -1, sizeof(emu_ram));
	tilemap_test_setup();

	const int copper_addr_bits = 7;

	const int reg_addr_pal    = 0;
	const int reg_addr_scroll = 16;
	const int reg_addr_cmp_x  = 20;
	const int reg_addr_cmp_y  = 21;
	const int reg_addr_jump1_lsb = 22;
	const int reg_addr_jump2_msb = 23;
	const int reg_addr_sorted_base = 24;
	const int reg_addr_oam_base = 25;
	const int reg_addr_map_base = 26;
	const int reg_addr_tile_base = 27;


	int xs[NUM_SPRITES];
	int ys[NUM_SPRITES];

	volatile uint16_t *sorted_list = emu_ram;
	volatile uint16_t *oam = emu_ram + 0x80;
	volatile uint16_t *tiles = emu_ram + 0x8000;
	const int copper_list_offset = 0x4000;
	volatile uint16_t *copper_list = emu_ram + copper_list_offset;
	volatile uint16_t *copper_start = emu_ram + 0xfffe;


	volatile uint16_t *sorted_list2 = emu_ram + 0x100;
	volatile uint16_t *oam2 = emu_ram + 0x180;


	for (int i = 0; i < NUM_SPRITES; i++) {
		// xs needs to be increasing, or we need to re-sort the list
		xs[i] = i*128/NUM_SPRITES + 32 + 512 - 160;
		ys[i] = i*240/NUM_SPRITES + 256 - 240;

		sorted_list[i] = ys[i] | (i << 8); // {id, y}

		oam[2*i] = (ys[i] & 7) | ((i&7) << 4); // attr_y
		oam[2*i+1] = xs[i]; // attr_x
	}

	for (int i = 0; i < 16; i++) {
		for (int y = 0; y < 8; y++) {
			int bits = 0;
			for (int x = 0; x < 4; x++) {
				int c;
				if (y == 0) c = 15 - ((x + i*4) & 15);
				else if (x == 0) c = 15 - y;
				else c = i + ((x ^ y) & 1)*7;

				bits |= (c & 15) << (4*x);
			}
			tiles[y + 8*i] = bits;
		}
	}

	// Jump to the copper list
	copper_start[0] = reg_addr_jump1_lsb | ((copper_list_offset & 0xff) << 8);
	copper_start[1] = reg_addr_jump2_msb | (copper_list_offset & 0xff00);

	// Add a lot of stop markers for the copper
	for (int i=0; i < 1024; i++) copper_list[i] = -1;

	int scroll_x0 = 0;
	int scroll_y0 = 0;
	int scroll_x1 = 4;
	int scroll_y1 = 4 + 256;


	init(NULL, 0);


	int pal_settings[] = {0, 0, 0,  2, 0, -4,  -3, 0, -4,  -4, -3, 0};


	uint64_t last_time = 0;
	int t = 0;
	while (true) {
		tud_task();
		uint64_t time = time_us_64();
		bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));
		//if (step) printf("delta_t = %d\r\n", delta_t);
		//if (step) printf("GPIO = 0x%x\r\n", (int)(sio_hw->gpio_in));

		/* TODO: remove/bring back?
		if (tud_cdc_available()) {
			char c = getchar();
			printf("%d ", c);
		}
		*/


		last_time = time;

		int w = t & 255;
		if (w >= 128) w = 255-w;
		if (step) {
			for (int i = 0; i < NUM_SPRITES; i++) {
/*
				// xs needs to be increasing, or we need to re-sort the list
				//xs[i] = i*w/NUM_SPRITES + 32 + 512 - 160;
				//xs[i] = i*9 + 32 + 512 - 160;
				xs[i] = i*9 + 128;

				int d = (i*32 + t*2) & 127;
				if (d >= 64) d = 127 - d;
				xs[i] -= d >> 3;

				//int y = (i*240/NUM_SPRITES + t) & 511;
				//if (y >= 256) y = 511 - y;
				int y = t % 171;
				if (y > 85) y = 170 - y;
				y = y*i*10/85;
				//if (i > 2) y = 240;
				ys[i] = y + 16;
*/

				int d = t & 31;
				if (d >= 16) d = 32 - d;
				d -= 8;

				//xs[i] = ((i*(320-8)/(NUM_SPRITES-1) + 128 + d*(i&7)/8) & 511) | (i&1)*512;

				//xs[i] = ((i*(320-8)/(NUM_SPRITES-1) + 128 + d*(i&7)/8) & 511) | (512 | 1024)*((i>>8)&1) | ((i&3) << 11);
				xs[i] = ((i*(320-8)/(NUM_SPRITES-1) + 128 + d*(i&7)/8) & 511);

				//ys[i] = i*(240-8)/(NUM_SPRITES-1) + 16;
				ys[i] = i*(240-8)/(NUM_SPRITES-1)/2 + 16;

/*
				int d = t & 63;
				if (d >= 32) d = 64 - d;
				d -= 16;

				xs[i] = 128 + i*(5*8 + d)/8;
				ys[i] =  16 + 120 + i/4;
*/
				sorted_list[i] = ys[i] | (i << 8); // {id, y}

				oam[2*i] = (ys[i] & 7) | ((i&7) << 4); // attr_y
				//oam[2*i+1] = xs[i]; // attr_x
				oam[2*i+1] = xs[i] | (((i>>8)&1) ? (i&3) << 14 : 15 << 12); // attr_x


				sorted_list2[i] = (ys[i] ^ 255) | (i << 8); // {id, y}
				oam2[2*i] = ((ys[i] ^ 255) & 7) | ((i&7) << 4); // attr_y
				oam2[2*i+1] = (xs[i]>>1) + 128; // attr_x // TODO: update to new attribute format
			}

			if ((t&1) == 0) scroll_x0 += 1;
			if ((t&3) == 0) scroll_x1 -= 1;
			if ((t&7) == 0) scroll_y1 += 1;
			if ((t&15) == 0) scroll_y0 -= 1;

			volatile uint16_t *copper_dest = copper_list;

			*(copper_dest++) = reg_addr_sorted_base | (0 << copper_addr_bits);
			*(copper_dest++) = reg_addr_oam_base | (1 << copper_addr_bits);
			*(copper_dest++) = reg_addr_map_base | ((0x21 << 1) << copper_addr_bits);
			*(copper_dest++) = reg_addr_tile_base | ((0xb << 1) << copper_addr_bits);

			*(copper_dest++) = (reg_addr_scroll + 0) | (scroll_x0 << copper_addr_bits);
			*(copper_dest++) = (reg_addr_scroll + 1) | (scroll_y0 << copper_addr_bits);
			*(copper_dest++) = (reg_addr_scroll + 2) | (scroll_x1 << copper_addr_bits);
			*(copper_dest++) = (reg_addr_scroll + 3) | (scroll_y1 << copper_addr_bits);

			/* 
			for (int i=0; i < 16; i++) {
				int r = 0; //i & 7;
				int g = 0; //i & 7;
				int b = i & 7;
				int pal_bits = ((r << 5) | (g << 2) | (b >> 1)) << 1;
				copper_list[4+i] = (reg_addr_pal + i) | (pal_bits << copper_addr_bits);
			}
			*/

			//*(copper_dest++) = (reg_addr_scroll + 0) | (scroll_x0 << copper_addr_bits);

			for (int j=0; j < 4; j++) {
				for (int i=0; i < 4; i++) {
					int rgb[3];
					for (int k=0; k < 3; k++) rgb[k] = clamp07(pal_settings[3*j + k] + 2*i + 1);

					int r = rgb[0];
					int g = rgb[1];
					int b = rgb[2];

					int index = i + 4*j;
					//if (index == (t&15)) { r = 7; g = b = 0; }
					if (index == ((t>>4)&15)) r = clamp07(r + 4);

					int pal_bits = ((r << 5) | (g << 2) | (b >> 1)) << 1;
					//copper_list[4+index] = (reg_addr_pal + index) | (pal_bits << copper_addr_bits);
					*(copper_dest++) = (reg_addr_pal + index) | (pal_bits << copper_addr_bits);
				}
			}

/*
			*(copper_dest++) = (reg_addr_scroll + 2) | (scroll_x0 << copper_addr_bits);
			*(copper_dest++) = (reg_addr_scroll + 1) | (scroll_x0 << copper_addr_bits);
*/

			*(copper_dest++) = reg_addr_cmp_y | (32 << copper_addr_bits); // Wait
			*(copper_dest++) = (reg_addr_scroll + 0) | ((scroll_x0 + 4) << copper_addr_bits);

			*(copper_dest++) = reg_addr_cmp_y | (136 << copper_addr_bits); // Wait
			*(copper_dest++) = (reg_addr_pal + 0) | (2 << copper_addr_bits);

			*(copper_dest++) = reg_addr_sorted_base | (4 << copper_addr_bits);
			*(copper_dest++) = reg_addr_oam_base | (3 << copper_addr_bits);


/*
			for (int i=0; i < 128; i++) {
				*(copper_dest++) = reg_addr_cmp_y | ((152+i) << copper_addr_bits); // Wait y
				*(copper_dest++) = (reg_addr_scroll + 0) | ((scroll_x0 + 128+3) << copper_addr_bits);
//				*(copper_dest++) = reg_addr_cmp_x | ((128+160+i) << copper_addr_bits); // Wait x
				*(copper_dest++) = reg_addr_cmp_x | ((128+160) << copper_addr_bits); // Wait x
				*(copper_dest++) = (reg_addr_scroll + 0) | (scroll_x0 << copper_addr_bits);
			}
*/
/*
			for (int i=0; i < 16; i++) {
				*(copper_dest++) = reg_addr_cmp_y | ((184+i) << copper_addr_bits); // Wait y
				*(copper_dest++) = (reg_addr_pal + 0) | (4 << copper_addr_bits);
				*(copper_dest++) = reg_addr_cmp_x | ((128+160+i) << copper_addr_bits); // Wait x
				*(copper_dest++) = (reg_addr_pal + 0) | (2 << copper_addr_bits);
			}
*/
			*(copper_dest++) = -1;

			t++;
		}
		if (!loop) break;
	}

	return 0;
}

void tilemap_test_setup(void) {
	printf("entering tilemap_test_setup\r\n");
	memset((void *)emu_ram, 0, sizeof(emu_ram));
	printf("memset done\r\n");

	volatile uint16_t *tilemap0 = emu_ram + 0x1000;
	volatile uint16_t *tilemap1 = emu_ram + 0x2000;
	volatile uint16_t *tiles = emu_ram + 0xc000;

	const int map_x = 64;
	const int map_y = 64;

	const int tile_bits = 11;

	// Fill in the tiles
	// -----------------
	for (int k=0; k < 32; k++) {
		int dx = (k&1)*2 - 1;
		int dy = ((k>>1)&1)*2 - 1;
		int dark = (k>>2)&3;
		bool use_2bpp = k >= 16;

		for (int j=0; j < 8; j++) {
			int line = 0;
			if (use_2bpp) {
				for (int i = 0; i < 8; i++) {
					int d = dx*(2*i - 7) + dy*(2*j - 7) + 1;
					d = (d >> (dark)) + 1;
					if (d < 0) d = 0;
					int color = d;

					line |= (color & 3) << 2*i;
				}
			} else {
				for (int i = 0; i < 4; i++) {
					int d = dx*(4*i - 6) + dy*(2*j - 7) + 1;
					d = (d >> dark) + 1;
					if (d < 0) d = 0;
					int color = d;

					line |= (color & 15) << 4*i;
				}
			}
			tiles[j + 8*k] = line;
		}
	}

	// Fill in the tile map
	// --------------------
//	for (int k=0; k < 2; k++) {
	for (int k=0; k < 2; k++) {
		volatile uint16_t *tilemap = k == 0 ? tilemap0 : tilemap1;

		for (int j=0; j < map_y; j++) {
			for (int i=0; i < map_x; i++) {
				int index = ((i*j & 127) + (map_x+map_y-2-i-j)) >> 4;
				if (index >= 8) index = 32;

				if (i == 0) index = 1;
				if (j == 0) index = 2;
				if (j == 32) index = 3;
				if (i == 0 && j == 0) index = 3;

				//tilemap[i + map_x*j] = index + 4*k;
				//int use_2bpp = k;
				int use_2bpp = 1;
				int pal = (i - j) & 3;

				//if (k==1) { index = 32; pal = 0; use_2bpp = 1; } // empty the second layer

				//tilemap[i + map_x*j] = (index + 4*k + 16*use_2bpp) | ((((pal&3) << 1) | use_2bpp) << tile_bits);
				tilemap[i + map_x*j] = (index + 4*k + 16*use_2bpp) | ((((use_2bpp ? ((pal&3) << 2) : 15)) << 1) << tile_bits);
			}
		}
	}
}

int tilemap_test(void) {
	printf("entering tilemap_test\r\n");
	tilemap_test_setup();

	init(NULL, 0);

	uint64_t last_time = 0;
	while (true) {
		tud_task();
		uint64_t time = time_us_64();
		bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));
		//if (step) printf("Tile map test...\r\n");
	}

	return 0;
}

int serial_slave(void) {
	//memset((void *)emu_ram, 0, sizeof(emu_ram));
	//init(NULL, 0);
	sprite_test(false);

	uint64_t data = 0;
	int data_counter = 0;

	uint64_t last_time = 0;
	while (true) {
		tud_task();
		//uint64_t time = time_us_64();
		//bool step = (last_time & ~((1 << 16) - 1)) != (time & ~((1 << 16) - 1));
		//if (step) printf("Tile map test...\r\n");

		/* TODO: remove / bring back?
		while (tud_cdc_available()) {
			char c = getchar();
			tud_task();
			if (c&128) { data_counter = 0; data = 0; }
			data = (data << 7) | (c & 127);
			data_counter++;

			//printf("0x%x: data = 0x%x\n", c, (int)data);

			if (data_counter == 5) {
				int cmd = data >> 32;

				int addr = (data >> 16) & 0xffff;
				uint16_t data16 = data  & 0xffff;
				emu_ram[addr] = data16;
				//printf("Cmd = %d, Write ram[0x%x] = 0x%x\n", cmd, addr, data16);

				//data_counter = 0; data = 0;
			}
		}
		*/
	}

	return 0;
}


void simple_test(void) {
	printf("entering simple_test\r\n");
	memset((void *)emu_ram, -1, sizeof(emu_ram));
	//memset((void *)emu_ram, 0x77, sizeof(emu_ram));
/*
	for (int i=0; i < 65536; i++) {
		//int r = rand(); r &= 0x7777;
		int r = rand() & 16384 ? -1 : 0;
		emu_ram[i] = r;
	}
*/
	init(NULL, 0);
	//while (true) {}
	printf("entering memset 0x11*i loop\r\n");
	int counter = 0;
	while (true) {
		/*
		//for (int i = 0; i < 16; i++) {
		for (int i = 7; i < 16; i += 8) {
			if (i == 0b1101) continue;
			memset((void *)emu_ram, i*0x11, sizeof(emu_ram));
		}
		*/
		for (int i=0; i < 65536; i++) {
			int r = ((rand() >> 14) ^ i ^ counter) & 1 ? 0x7777 : 0xffff;
			emu_ram[i] = r;
		}
		counter++;
	}
}

// Tuning process for adjusting address and data delays:
// Try to stay close to the original address and data delays, probably they haven't changed by so many cycles.
// This test pattern is built to be useful even when the data delays are wrong, and not trigger any bad copper writes,
// that is avoid writng to PPU registers 28-31 even if the delays are wrong.
// It relies on looking at how the palette varies for different tiles across the display.
// The palette is chosen based on bits 12-15 of every word in the tile map.
// Each nibble can have a value of either 10 or 11, choosing between two palettes.
// Some tile data will be 0b1010, som will be 0b1011, using only one to two of the colors from each palette (colors 2 and 3).

// Start with this test _without masking_. (data &= ...)
// Make sure that you get palette variations in the x and y directions -- otherwise the data delay is too short?
// The console alternates between reads for the tile planes and reads for other things (sprites/copper)
// -- for most parts of the screen, those reads will probably stay at a fixed address, so if the data delay is too short,
//    the pal bits in the tile map (bits 12-15) will come from the result of a stationary read instead.

// Adjust the address delay until stable image and
// If delay for addr_out is right (and data delay reasonable), should get
// - x direction: 8 tiles with one palette, then 8 of the other, repeating (plus som coarser variations)
// - y direction: alternating palette for each row of tiles (plus som coarser variations)
//
// Blinking indicates that the address delay should change between even and odd.
// Changing the delay by 2 should shift by one nibble.

// Then try to mask more of the nibbles, and adjust the data delay so that the pattern stays.
// When you can use data &= 0xfaaa; and still get a pattern (and it's the right one as described above), tuning should be complete.

void address_test(void) {
	//memset((void *)emu_ram, -1, sizeof(emu_ram));
	for (int i = 0; i < 65536; i++) {
		// Use different address bit from each nibble, to show the effect of address delay
		int a0 = (i >> 3) & 1;
		int a1 = (i >> 6) & 1;
		int a2 = (i >> 9) & 1;
		int a3 = (i >> 12) & 1;
		int x = a0 + a1 + a2 + a3;
		//int nibble = 8 + (x&3);
		// Setting bit 3 of every nibble ensures always_opaque ==> see just one tile layer.
		// These nibble patterns should never set PPU registers 28-31, which could break things.
		int nibble = 8 + 2 + (x&1);
		int data = 0x1111 * nibble;

		// Turn on successively more masking to tune the data delay, should still get palette variations.
		// Turn off initially!
		//data &= 0xffaa;
		//data &= 0xfafa;
		data &= 0xfaaa; // mask everything but the top nibble, which should control the palette

		emu_ram[i] = data;
	}
	init(NULL, 0);
	while (true) {}
}


void test_pattern(void) {
	//memset((void *)emu_ram, -1, sizeof(emu_ram));
	for (int i = 0; i < 65536; i++) {
		int sum = 0;
		int temp = i;
		for (int j=0; j < 4; j++) {
			sum += temp & 15;
			temp >>= 4;
		}
		//int nibble = 8 + ((sum&1)<<1);
		int nibble = 8 + (sum&3);
		//int data = 0x1111 * nibble;
		int data = 0x1000 * nibble;

		emu_ram[i] = data;
	}
	init(NULL, 0);
	while (true) {}
}


void serial_ram_emu_main(void) {
	//capture_test();
	//fb_test();
	//tilemap_test0();
	//tilemap_test();
	sprite_test(true);
	//serial_slave();

	//simple_test();
	//test_pattern();
	//address_test();

/*
	printf("calling start_clock\r\n");
	start_clock();
*/
	while (true) {}
}
