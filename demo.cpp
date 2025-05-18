#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/dma.h"

#include "serial-ram-emu.h"

#include "build/serial-ram-emu.pio.h"

#include "tt_pins.h"

#include "common.h"
#include "anemonegrafx8.h"

void init_console() {
	printf("entering init_console\r\n");

	// Initialize memory to all ones -- causes copper to halt immediately
	// ==================================================================
	memset((void *)emu_ram, -1, sizeof(emu_ram));

	// Start RAM emulator
	// ==================
	SerialRamEmu ram_emu;
	printf("calling serial_ram_emu_init\r\n");
	serial_ram_emu_init(&ram_emu, pio0, ADDRESS_PIN_BASE, DATA_PIN_BASE, emu_ram, NULL, 0);
	printf("calling serial_ram_emu_start\r\n");
	serial_ram_emu_start(&ram_emu);
	//start_clock_loop();

	// Start clock
	// ===========
	printf("calling start_clock\r\n");
	start_clock();
	printf("init done\r\n");
}

int min(int x, int y) { return x < y ? x : y; }
int max(int x, int y) { return x > y ? x : y; }

const int CMP_Y0 = 512 - 2*240;

void checkerboard_init_tiles(int tile_p_addr, int plane_addr, int row0, int num_rows, int blank_lines) {
	const int center_level = 2;

	// Intialize tiles
	for (int index = 0; index < 64*num_rows; index++) {
		for (int j = 0; j < 8; j++) {
			int y = 8*(index >> 6) + j;
			//y -= 128;
			int data = 0;
			for (int i = 7; i >= 0; i--) {
				if (y <= blank_lines) continue;
				int x = 8*(index & 63) + i - 256;

				/*
				int dist = (x*x + y*y) >> 7;
				int bits = dist & 3;
				*/

				int inv_z = y+1;
				int n = 16;
				int bx = (x << (1+n))/inv_z;
				int dx = (1 << (1+n))/inv_z;
				int by = (1<<(9+n))/inv_z;
				int dy = (1 << (9+n))/inv_z - (1 << (9+n))/(inv_z + 1);

				int bx2 = bx - by;
				int by2 = bx + by;

				int bits = ((bx2 ^ by2)>>n) & 1 ? 3 : 0;

				int d = dx > dy ? dx : dy;
				d >>= 1;


				const int c = 1 << (n-1);
				const int mask = ((1 << n)-1);
				int diff1 = abs(((bx2 + c) & mask) - c);
				int diff2 = abs(((by2 + c) & mask) - c);
				int diff = diff1 < diff2 ? diff1 : diff2;
				int pull = (2*d - diff)/d;
				if (pull > 0) {
					if (bits == 0) {
						bits += pull;
						if (bits > center_level) bits = center_level;
					} else {
						bits -= pull;
						if (bits < center_level) bits = center_level;
					}
				}

				data = (data << 2) | (bits & 3);
			}
			write_tile_row(tile_p_addr, index, y, data);
		}
	}

	// Initialize plane0 map
	for (int y = 0; y < num_rows; y++) {
		for (int x = 0; x < 64; x++) {
			//int pal = 4*(((x>>1)+y)&3);
			int pal = 0;
			write_map(plane_addr, x, y + row0, pack_map(x + ((y&31) << 6), pal, false));
		}
	}
}

void show_checkerboard() {
	const int plane0_addr      = 0x0000;
	const int tile_p_addr      = 0x4000;
	const int copper_list_addr = 0xf000;

	ram_uint16 *const copper_list = emu_ram_ptr(copper_list_addr);

	ram_uint16 *copper_dest = copper_list;

	copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_PLANE0);
	copper_write_map_base(copper_dest, plane0_addr, 0);
	copper_write_tile_base(copper_dest, tile_p_addr, 0);
	copper_write(copper_dest, REG_ADDR_SCROLL_X0, -118+(512-320)/2);
	copper_write(copper_dest, REG_ADDR_SCROLL_Y0, -16);

	//copper_write_pal333(copper_dest, 15, 7, 0, 7); // set color 15 to magenta
	for (int i = 0; i < 4; i++) copper_write_pal333(copper_dest, i,  i*2, i*2, i*2+1);
	for (int i = 4; i < 16; i++) copper_write_pal333(copper_dest, i,  i>>1, (i>>2)+4*(i&1), i>>1);

/*
	// scan line timed copper commands
	for (int j=0; j < 8; j++) {
		int fade = 7-j;

		if (j > 0) {
			int cmp_y = CMP_Y0 + 64 + 8*j;
			copper_write(copper_dest, REG_ADDR_CMP_Y, cmp_y);
		}

		// Update checkerboard palette
		for (int i = 0; i < 4; i++) {
			int r = min(7, i + fade);
			int g = min(7, i*2 + fade);
			int b = min(7, i*2 + 1 + fade);
			copper_write_pal(copper_dest, i, pack_rgb333(r, g, b));
		}
	}
*/
	ram_uint16 *copper_timed = copper_dest;

	copper_stop(copper_dest);
	enable_copper(copper_list_addr);
	// The copper list continues with many -1, so we can fill in more entries later without worrying that we have time to put in a -1

	const int row0 = 15;
	const int num_rows = 30-row0;

	checkerboard_init_tiles(tile_p_addr, plane0_addr, row0, num_rows, 0);

	int t = 0;
	while (true) {
		wait_vsync();
		copper_dest = copper_timed;

		//copper_write(copper_dest, REG_ADDR_SCROLL_X0, -128+(512-320)/2 + t);

		int pal_i = 0;
		int next_pal_y = 0;
		for (int i = 0; i < 8*num_rows; i++) {
			int sy = 8*row0 + i;
			if (i > 0) copper_wait_y(copper_dest, CMP_Y0 + 2*sy);

			// scroll checker board
			int y = i + 1;
			int dx = (((t&127) - 64)*y) >> 8;
			copper_write(copper_dest, REG_ADDR_SCROLL_X0, -128+(512-320)/2 + dx); // not centered?

			if (i >= next_pal_y) {
				int fade = 7 - pal_i;
				// Update checkerboard palette
				for (int i = 0; i < 4; i++) {
					int r = min(7, i + fade);
					int g = min(7, i*2 + fade);
					int b = min(7, i*2 + fade);
					copper_write_pal(copper_dest, i, pack_rgb333(r, g, b));
				}
				pal_i++;
				if (pal_i < 8) next_pal_y = 32 + 4*pal_i;
				else next_pal_y = 512;
			}

		}

		t++;
	}
}

#include "cloud_data_generated.cpp"

// hex digits: (layer, source row) in front and then behind
int cloud_rows[] = {
	0x00ff,
	0x01ff,
	0x02ff,
	0x0315,
	0x0416,
	0x17ff,
	0x1829,
	0x2a3b,
	0xff3c
};
const int num_cloud_rows = sizeof(cloud_rows)/sizeof(*cloud_rows);


// Overwrites the whole planes
void clouds_init_tiles(int tile_p_addr, int plane0_addr, int plane1_addr, int tile_index_offset) {
	// write pixel data
	for (int j=0; j < cloud_tiles_y; j++) {
		for (int i=0; i < 8; i++) {
			write_tile_row(tile_p_addr, j + tile_index_offset, i, cloud_tiles[j*8 + i]);
		}
	}

	// write tile map data
	//memset((void *)emu_ram_ptr(plane0_addr), 0, 64*64*sizeof(uint16_t));
	//memset((void *)emu_ram_ptr(plane1_addr), 0, 64*64*sizeof(uint16_t));

	for (int i=0; i < 64*64; i++) {
		// uses palette 0; the cloud palette
		*emu_ram_ptr(plane0_addr + i) = tile_index_offset;
		*emu_ram_ptr(plane1_addr + i) = tile_index_offset;
	}

	for (int j=0; j < num_cloud_rows; j++) {
		for (int plane=0; plane < 2; plane++) {
			int cfg = (cloud_rows[j] >> (plane == 0 ? 8 : 0)) & 255;
			//int layer = cfg >> 4;
			int src_row = cfg & 15;

			if (src_row == 15) continue;

			int plane_addr = plane == 0 ? plane0_addr : plane1_addr;
			for (int i=0; i < cloud_map_x; i++) {
				write_map(plane_addr, i, j, pack_map(cloud_map[src_row*cloud_map_x + i] + tile_index_offset, 0));
			}
		}
	}
}


void show_clouds() {
	const int plane0_addr      = 0x0000;
	const int plane1_addr      = 0x1000;
	const int tile_p_addr      = 0x4000;
	const int copper_list_addr = 0xf000;

	ram_uint16 *const copper_list = emu_ram_ptr(copper_list_addr);

	ram_uint16 *copper_dest = copper_list;

	copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_PLANE0 | DM_PLANE1);
	//copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_PLANE1);
	copper_write_map_base(copper_dest, plane0_addr, plane1_addr);
	copper_write_tile_base(copper_dest, tile_p_addr, 0);

	ram_uint16 *const copper_scroll = copper_dest;
	copper_write(copper_dest, REG_ADDR_SCROLL_X0, -118); // Seems to put the left edge of the tile map at the left edge of the screen
	copper_write(copper_dest, REG_ADDR_SCROLL_X1, -118); // Seems to put the left edge of the tile map at the left edge of the screen
	copper_write(copper_dest, REG_ADDR_SCROLL_Y0, -16);
	copper_write(copper_dest, REG_ADDR_SCROLL_Y1, -16);

	for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i,  pack_rgb333(cloud_pal[i*3], cloud_pal[i*3+1], cloud_pal[i*3+2]));
	//for (int i = 4; i < 16; i++) copper_write_pal333(copper_dest, i,  i>>1, (i>>2)+4*(i&1), i>>1);

	ram_uint16 *copper_timed = copper_dest;

	copper_stop(copper_dest);
	enable_copper(copper_list_addr);

	clouds_init_tiles(tile_p_addr, plane0_addr, plane1_addr, 0);

	int t = 0;
	while (true) {
		wait_vsync();
/*
		copper_dest = copper_scroll;
		copper_write(copper_dest, REG_ADDR_SCROLL_X0, t&511);
		copper_write(copper_dest, REG_ADDR_SCROLL_X1, (t>>1)&511);
*/

		copper_dest = copper_timed;
		for (int row = 0; row < num_cloud_rows; row++) {

			if (row > 0) copper_wait_y(copper_dest, CMP_Y0 + 16*row);

			for (int plane = 0; plane <= 1; plane++) {
				int cfg = (cloud_rows[row] >> (plane == 0 ? 8 : 0)) & 255;
				int layer = cfg >> 4;
				int speed = 4-layer;
				int dx = (speed*t) >> 1;

				copper_write(copper_dest, REG_ADDR_SCROLL_X0 + 2*plane, dx&511);
			}
		}


		t += 1;
	}
}

/*
enum {NUM_SPRITES=64};

inline int clamp07(int x) { if (x <= 0) return 0; else if (x >= 7) return 7; else return x; }

void sprite_test() {
	memset((void *)emu_ram, -1, sizeof(emu_ram));

	ram_uint16 *p_tiles = emu_ram + 0xc000;

	const int tile_bits = 11;


	// Fill in the tiles
	// -----------------
	for (int k=0; k < 2048; k++) {
		for (int j=0; j < 8; j++) {
			p_tiles[j + 8*k] = rand();
		}
	}

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

	ram_uint16 *sorted_list = emu_ram;
	ram_uint16 *oam = emu_ram + 0x80;
	ram_uint16 *tiles = emu_ram + 0x8000;
	const int copper_list_offset = 0x4000;
	ram_uint16 *copper_list = emu_ram + copper_list_offset;
	ram_uint16 *copper_start = emu_ram + 0xfffe;


	ram_uint16 *sorted_list2 = emu_ram + 0x100;
	ram_uint16 *oam2 = emu_ram + 0x180;


	for (int i = 0; i < NUM_SPRITES; i++) {
		// xs needs to be increasing, or we need to re-sort the list
		//xs[i] = i*128/NUM_SPRITES + 32 + 512 - 160;
		xs[i] = ((i*(320-8)/(NUM_SPRITES-1) + 128) & 511);
		ys[i] = i*240/NUM_SPRITES + 256 - 240;
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

	// Not adding copper stop, relying on prefilled -1:s
	enable_copper(copper_list_offset);



	int pal_settings[] = {0, 0, 0,  2, 0, -4,  -3, 0, -4,  -4, -3, 0};


	for (int i = 0; i < NUM_SPRITES; i++) {


		sorted_list2[i] = (ys[i] ^ 255) | (i << 8); // {id, y}
		oam2[2*i] = ((ys[i] ^ 255) & 7) | ((i&7) << 4); // attr_y
		oam2[2*i+1] = (xs[i]>>1) + 128; // attr_x // TODO: update to new attribute format
	}

	ram_uint16 *copper_dest = copper_list;

	// *(copper_dest++) = reg_addr_tile_base | ((0xb << 1) << copper_addr_bits);
	copper_write_tile_base(copper_dest, 0xc000, 0x8000);

	for (int j=0; j < 4; j++) {
		for (int i=0; i < 4; i++) {
			int rgb[3];
			for (int k=0; k < 3; k++) rgb[k] = clamp07(pal_settings[3*j + k] + 2*i + 1);

			int r = rgb[0];
			int g = rgb[1];
			int b = rgb[2];

			int index = i + 4*j;
			//if (index == (t&15)) { r = 7; g = b = 0; }
			//if (index == ((t>>4)&15)) r = clamp07(r + 4);

			int pal_bits = ((r << 5) | (g << 2) | (b >> 1)) << 1;
			*(copper_dest++) = (reg_addr_pal + index) | (pal_bits << copper_addr_bits);
		}
	}

	copper_write_sorted_base(copper_dest, 0x100);
	copper_write_oam_base(copper_dest, 0x180);

	*(copper_dest++) = -1;
}
*/

#include "logo3_data_generated.cpp"
#include "logo_b_data_generated.cpp"

const int logo2_id_offset = 64;
const int max_sprites = 64;
const int sprite_y_offset = -6;
const int sprite_b_y_offset = 160-3;

const int num_letters = logo_map_x/4 + 2;
const int num_letters_b = logo_b_map_x/4;

const int y_sprite_split = 160-8;


static Sprite sprites[max_sprites];
static Sprite sprites_b[max_sprites];
static int sprite_l[max_sprites];
static int sprite_l_b[max_sprites];

static int num_sprites, num_sprites_b=0;

void logo_init_sprites(int tile_s_addr, int base_pal, int depth1b, int depth2) {
	// write pixel data
	for (int j=0; j < logo_tiles_y; j++) {
		for (int i=0; i < 8; i++) {
			write_sprite_tile_row(tile_s_addr, j, i, logo_tiles[j*8 + i]);
		}
	}
	for (int j=0; j < logo_b_tiles_y; j++) {
		for (int i=0; i < 8; i++) {
			write_sprite_tile_row(tile_s_addr, j + logo2_id_offset, i, logo_b_tiles[j*8 + i]);
		}
	}

	memset(sprites, 0, sizeof(sprites));
	memset(sprites_b, 0, sizeof(sprites_b));

	int sprite_offsets[] = {0,2,0,-2,0,-2,-5};
	int sprite_offsets_b[] = {0,2,2,2,2};

	// create sprites
	for (int logo=1; logo >= 0; logo--) {
		num_sprites = 0;
		//for (int l=0; l < num_letters; l++) {
		for (int l0 = (logo ? num_letters_b : num_letters)-1; l0 >= 0; l0--) {
			int l = logo ? num_letters_b-1-l0 : l0;
			for (int j=0; j < logo_map_y; j++) {
				for (int i=0; i < 4; i++) {
					// 0123412
					// ANEMOne
					int ls = l;
					if (ls >= 5) ls -= 4;

					int tile_index = (logo ? logo_b_map : logo_map)[j*logo_map_x + i + ls*4];
					if (tile_index == 0 || num_sprites >= max_sprites) continue;

					Sprite &s = (logo ? sprites_b : sprites)[num_sprites];
					(logo ? sprite_l_b : sprite_l)[num_sprites] = l;
					num_sprites++;

					//int slant = 4;
					int slant = (logo == 0 && l == 0) ? 8 : 4;
					int y_offset = logo ? (num_letters_b-1-l)*10 : l*15;

					int x_offset = 0;
					if (logo == 1) {
						if ((l == 2 && j == 3) || (l == 3 && j != 2) || (l == 4 && j == 2)) x_offset = 4;
						else if ((l == 4 && j == 1)) x_offset = 8;
					}

					s.x = 128 + 48 + l*28 + i*16 + slant*(logo_map_y-1-j) + (logo ? sprite_offsets_b : sprite_offsets)[l]*4 + x_offset;
					s.y = 16 + j*8 + y_offset + (l==6 ? 8 : 0) + (logo ? sprite_b_y_offset : sprite_y_offset);
					s.pal = 4*logo + base_pal;
					s.tile_index = tile_index + logo*logo2_id_offset;
					s.depth = logo ? depth2 : (s.y >= 10*8 ? depth1b : 0);
					s.flags = SPRITE_FLAG_ON;
					//s.flags |= (l&1) + 1;
				}
			}
		}
		if (logo == 1) num_sprites_b = num_sprites;
	}
}

void show_logo() {

	const int num_alt_buffers = 2;
	const int alt_offset = 0x400;
	const int sub_offset = 0x100;

	const int sorted_base      = 0x2000; // Must be in first half of memory
	const int oam_base         = 0x3000;
	const int tile_s_addr      = 0x8000;
	const int copper_list_addr = 0xf000;

	for (int alt=0; alt < num_alt_buffers; alt++) {
		ram_uint16 *const copper_list = emu_ram_ptr(copper_list_addr + alt*alt_offset);

		ram_uint16 *copper_dest = copper_list;

		copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_SPRITES);
		//copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_ALL);

		copper_write_tile_base(copper_dest, 0, tile_s_addr);
		copper_write_sorted_base(copper_dest, sorted_base + alt*alt_offset);
		copper_write_oam_base(copper_dest, oam_base + alt*alt_offset);

		for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i,   pack_rgb333(logo_pal[i*3], logo_pal[i*3+1], logo_pal[i*3+2]));
		for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i+4, pack_rgb333(logo_b_pal[i*3], logo_b_pal[i*3+1], logo_b_pal[i*3+2]));
		//for (int i = 4; i < 16; i++) copper_write_pal333(copper_dest, i,  i>>1, (i>>2)+4*(i&1), i>>1);

		//ram_uint16 *copper_timed = copper_dest;

		copper_wait_y(copper_dest, CMP_Y0 + y_sprite_split*2);
		copper_write_sorted_base(copper_dest, sorted_base + alt*alt_offset + sub_offset);
		copper_write_oam_base(copper_dest, oam_base + alt*alt_offset + sub_offset);

		copper_stop(copper_dest);
	}
	enable_copper(copper_list_addr);

	logo_init_sprites(tile_s_addr, 0, 0, 0);

	// set up sprite lists
	update_oam(oam_base, sprites, num_sprites);
	update_sorted(sorted_base, sprites, num_sprites);

	for (int alt = 0; alt < num_alt_buffers; alt++) {
		update_oam(oam_base + alt*alt_offset + sub_offset, sprites_b, num_sprites_b);
		update_sorted(sorted_base + alt*alt_offset + sub_offset, sprites_b, num_sprites_b);
	}

	printf("num_sprites = %d\r\n", num_sprites);

	static Sprite sprites2[max_sprites];
	static Sprite sprites_b2[max_sprites];

	static int letter_offs_x[num_letters];
	static int letter_offs_y[num_letters];

	int t = 0;
	int alt = 0;
	while (true) {
		wait_vsync();

		// Prepare next frame
		// ------------------
		//copper_dest = copper_timed;

		for (int l=0; l < num_letters; l++) {
			float phi = t*0.15 + (6.28/16)*l;
			float phi2 = t*0.1 + (6.28/16)*l;

			letter_offs_x[l] = int(32*cosf(phi2));
			letter_offs_y[l] = int(12*sinf(phi));
		}

		memcpy(sprites2, sprites, sizeof(sprites2));
		for (int i = 0; i < num_sprites; i++) {
			Sprite &s = sprites2[i];

/*
			float phi = t*0.15 + (6.28/16)*sprite_l[i];
			float phi2 = t*0.1 + (6.28/16)*sprite_l[i];

			s.y += int(12*sinf(phi));
			s.x += int(32*cosf(phi2));
*/
			s.x += letter_offs_x[sprite_l[i]];
			s.y += letter_offs_y[sprite_l[i]];
		}
		update_oam(oam_base + alt*alt_offset, sprites2, num_sprites);
		update_sorted(sorted_base + alt*alt_offset, sprites2, num_sprites);

		memcpy(sprites_b2, sprites_b, sizeof(sprites_b2));
		for (int i = 0; i < num_sprites_b; i++) {
			Sprite &s = sprites_b2[i];
/*
			float phi = t*0.15 + (6.28/16)*sprite_l_b[i];
			float phi2 = t*0.1 + (6.28/16)*sprite_l_b[i];

			s.y -= int(6*sinf(phi));
			s.x += int(32*cosf(phi2));
*/
			s.x += letter_offs_x[sprite_l_b[i]];
			s.y -= letter_offs_y[sprite_l_b[i]] >> 1;
		}
		update_oam(oam_base + alt*alt_offset + sub_offset, sprites_b2, num_sprites_b);
		update_sorted(sorted_base + alt*alt_offset + sub_offset, sprites_b2, num_sprites_b);

		// Switch to next frame
		// --------------------
		enable_copper(copper_list_addr + alt*alt_offset);

		t += 1;
		alt++;
		if (alt >= num_alt_buffers) alt = 0;
	}
}

#include "font_data_generated.cpp"

char upper(char ch) { return ('a' <= ch && ch <= 'z') ? ch + 'A' - 'a' : ch; }

char take_letter(const char *&str) {
	char ch = *str;
	if (ch == 0) ch = ' ';
	else if (upper(ch) == 'L' && upper(str[1]) == 'T' && upper(str[2]) == 'A') { ch = '@'; str += 3; } // ligatures
	else if (upper(ch) == 'T' && upper(str[1]) == 'A' && upper(str[2]) == 'T') { ch = '#'; str += 3; }
	else if (upper(ch) == 'T' && upper(str[1]) == 'A') { ch = '{'; str += 2; }
	else if (upper(ch) == 'A' && upper(str[1]) == 'T') { ch = '}'; str += 2; }
	else if (upper(ch) == 'P' && upper(str[1]) == ',') { ch = '`'; str += 2; }
	else if (upper(ch) == 'T' && upper(str[1]) == 'O') { ch = '['; str += 2; }
	else if (upper(ch) == 'S' && upper(str[1]) == 'O') { ch = ']'; str += 2; }
	else if (upper(ch) == 'P' && upper(str[1]) == 'A') { ch = '&'; str += 2; }
	else if (upper(ch) == 'S' && upper(str[1]) == 'A') { ch = '|'; str += 2; }
	else if (upper(ch) == 'L' && upper(str[1]) == 'T') { ch = '^'; str += 2; }
	else if (upper(ch) == 'A' && upper(str[1]) == 'V') { ch = '%'; str += 2; }
	else if (upper(ch) == 'S' && upper(str[1]) == ',') { ch = '~'; str += 2; }
	else str++;

	return ch;
}

static int font_tile_index_offset = 0;

int get_letter_width(int ch) { return font_w[upper(ch)&127]; }
int put_letter(int plane_addr, int x, int y, char ch, int pal) {
	//if ('a' <= ch && ch <= 'z') ch += 'A' - 'a';
	ch = upper(ch);

	int pos = font_pos[ch&127];
	int w   = font_w[  ch&127];

	for (int j=0; j < font_map_y; j++) {
		for (int i=0; i < w; i++) {
			write_map(plane_addr, x+i, y+j, pack_map(font_map[font_map_x*j + i + pos] + font_tile_index_offset, pal));
		}
	}
	return w;
}

void show_scroller() {
	const int plane0_addr      = 0x0000;
	const int tile_p_addr      = 0x4000;
	const int copper_list_addr = 0xf000;

	ram_uint16 *const copper_list = emu_ram_ptr(copper_list_addr);

	ram_uint16 *copper_dest = copper_list;

	copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_PLANE0);
	copper_write_map_base(copper_dest, plane0_addr, 0);
	copper_write_tile_base(copper_dest, tile_p_addr, 0);

	ram_uint16 *const copper_scroll = copper_dest;
	copper_write(copper_dest, REG_ADDR_SCROLL_X0, -118); // Seems to put the left edge of the tile map at the left edge of the screen
	copper_write(copper_dest, REG_ADDR_SCROLL_Y0, -16);

	for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i,  pack_rgb333(font_pal[i*3], font_pal[i*3+1], font_pal[i*3+2]));
	//for (int i = 4; i < 16; i++) copper_write_pal333(copper_dest, i,  i>>1, (i>>2)+4*(i&1), i>>1);

	ram_uint16 *copper_timed = copper_dest;

	copper_stop(copper_dest);
	enable_copper(copper_list_addr);

	// write pixel data
	for (int j=0; j < font_tiles_y; j++) {
		for (int i=0; i < 8; i++) {
			write_tile_row(tile_p_addr, j, i, font_tiles[j*8 + i]);
		}
	}

	const char *message = "Hi, welcome to the first demo created for Anemonegrafx-8! I designed this retro console on a chip, and got it taped out (made into silicon) thanks to Tiny Tapeout!";

	// write tile map data
	memset((void *)emu_ram_ptr(plane0_addr), 0, 64*64*sizeof(uint16_t));

	const char *str = message;
	int char_x = 40;
	int char_y = 15;
	int char_pal = 0;

	int t = 0;
	int scroll = 0;
	while (true) {
		wait_vsync();

		while (((char_x*8 - scroll)&511) < 48*8) {
			char ch = take_letter(str);
			char_x += put_letter(plane0_addr, char_x, char_y, ch, char_pal);
		}
/*
		while (*str != 0) {
			if (((char_x*8 - scroll)&511) >= 48*8) break;
			char_x += put_letter(plane0_addr, char_x, char_y, *str, char_pal);
			str++;
			//if (char_x >= 64-3) break;
		}
*/

		copper_dest = copper_scroll;
		copper_write(copper_dest, REG_ADDR_SCROLL_X0, (scroll-118)&511);

		t += 1;
		scroll += 1;
		if (scroll >= 512*5) {
			scroll = 0;
			char_x = 40;
			str = message;
		}
	}
}


uint8_t logo_b2_pal[] = {
2,	3,	6,
0,	0,	0,
4,	5,	4,
2,	3,	2
};

#define SPACES "    "
#define SPACES2 "        "
#define SPACES3 "                "

void show_demo(bool still) {
	const char *message = 
		"                                                            "
		SPACES3 "Hi, welcome to the first demo created for AnemoneGrafx-8! "
		"I designed this retro console on a chip, and got it made into silicon thanks to Tiny Tapeout! "
		SPACES3 "Features: "
			SPACES2 "* 320x240 60 fps output (scaled to 640x480 VGA) "
			SPACES2 "* 2 tile planes with 8x8 pixel tiles "
			SPACES2 "64+ simultaneous 16x8 pixel sprites, "
				"up to 4 overlapping and 7 on the same scan line "
			SPACES2 "* 16 colors on the same scan line, from a 256 color palette "
			SPACES2 "* per sprite/tile, choose subpalette: 15 for 4 color mode, one for 16 color mode (gives half tile x resolution/sprite width) "
			SPACES2 "* Scan line tricks using simple copper function "
		SPACES3 "There is also an analog inspired 4 voice synth, with different waveforms and a second order resonant filter, "
		"but I haven't tried to get it working yet. "
		"For more about features/implementation/use, see https://github.com/toivoh/tt06-retro-console (link below) "
		SPACES3 "Thanks for watching!";

	int text_width = 0;
	for (const char *str = message; *str != 0; str++) text_width += get_letter_width(*str);
	int max_char_scroll = 8*((text_width&~63)+64);


	const int num_alt_buffers = 2;
	const int alt_offset = 0x400;
	const int sub_offset = 0x100;


	const int plane0_addr      = 0x0000;
	const int plane1_addr      = 0x1000;
	const int sorted_base      = 0x2000; // Must be in first half of memory
	const int oam_base         = 0x3000;
	const int tile_p_addr      = 0x4000;
	const int tile_s_addr      = 0x8000;
	const int copper_list_addr = 0xf000;

	ram_uint16 *copper_timed;

	for (int alt=0; alt < num_alt_buffers; alt++) {
		ram_uint16 *const copper_list = emu_ram_ptr(copper_list_addr + alt*alt_offset);

		ram_uint16 *copper_dest = copper_list;

		//copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_PLANE0 | DM_PLANE1);
		copper_write(copper_dest, REG_ADDR_DISPLAY_MASK, DM_ALL);
		copper_write_map_base(copper_dest, plane0_addr, plane1_addr);
		copper_write_tile_base(copper_dest, tile_p_addr, tile_s_addr);
		copper_write_sorted_base(copper_dest, sorted_base + alt*alt_offset);
		copper_write_oam_base(copper_dest, oam_base + alt*alt_offset);

		ram_uint16 *const copper_scroll = copper_dest;
		copper_write(copper_dest, REG_ADDR_SCROLL_X0, -118); // Seems to put the left edge of the tile map at the left edge of the screen
		copper_write(copper_dest, REG_ADDR_SCROLL_X1, -118); // Seems to put the left edge of the tile map at the left edge of the screen
		copper_write(copper_dest, REG_ADDR_SCROLL_Y0, -16);
		copper_write(copper_dest, REG_ADDR_SCROLL_Y1, -16);

		for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i,  pack_rgb333(cloud_pal[i*3], cloud_pal[i*3+1], cloud_pal[i*3+2]));
		//for (int i = 4; i < 16; i++) copper_write_pal333(copper_dest, i,  i>>1, (i>>2)+4*(i&1), i>>1);

		for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i+8,  pack_rgb333(logo_pal[i*3], logo_pal[i*3+1], logo_pal[i*3+2]));
		for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i+12, pack_rgb333(logo_b2_pal[i*3], logo_b2_pal[i*3+1], logo_b2_pal[i*3+2]));

		for (int i = 0; i < 4; i++) copper_write_pal(copper_dest, i+4,  pack_rgb333(font_pal[i*3], font_pal[i*3+1], font_pal[i*3+2]));

		if (alt == 0) copper_timed = copper_dest;
		copper_stop(copper_dest);
	}

	enable_copper(copper_list_addr);


	const int checker_row0 = 15;
	const int checker_num_rows = 30 - checker_row0;
	const int checker_num_tiles = 64*checker_num_rows;

	const int cloud_tile_index_offset = checker_num_tiles;
	const int cloud_num_tiles = cloud_tiles_y;

	font_tile_index_offset = cloud_tile_index_offset + cloud_num_tiles;

	clouds_init_tiles(tile_p_addr, plane0_addr, plane1_addr, cloud_tile_index_offset);
	checkerboard_init_tiles(tile_p_addr, plane1_addr, checker_row0, checker_num_rows, 32);

	logo_init_sprites(tile_s_addr, 8, still ? 1 : 0, still ? 1 : 2);

	// Font: write pixel data
	for (int j=0; j < font_tiles_y; j++) {
		for (int i=0; i < 8; i++) {
			write_tile_row(tile_p_addr, j + font_tile_index_offset, i, font_tiles[j*8 + i]);
		}
	}



	static Sprite sprites2[max_sprites];
	static Sprite sprites_b2[max_sprites];
	static int letter_offs_x[num_letters];
	static int letter_offs_y[num_letters];

	const char *str = message;
	int char_x = 40;
	int char_y = 15;
	int char_pal = 4;
	int char_scroll = 0;

	if (still) {
		char_x = 0;
		char_y = 10;
		const char *str = "  I designed this retro\n    console on a chip\n  and got it made into\n    silicon thanks to\n        Tiny Tapeout";
		while (*str) {
			char ch = take_letter(str);
			if (ch == '\n' || char_x + get_letter_width(ch) > 40) {
				char_x = 0;
				char_y += 4;
			}
			if (ch != '\n') char_x += put_letter(plane0_addr, char_x, char_y, ch, char_pal);
		}
		char_x = 40;
	}


	int alt = 0;
	int t = 0;
	while (true) {
		wait_vsync();

		//copper_dest = copper_timed;
		ram_uint16 *copper_dest = copper_timed + alt*alt_offset;

		// Clouds
		// ------
		for (int row = 0; row < num_cloud_rows; row++) {

			if (row > 0) copper_wait_y(copper_dest, CMP_Y0 + 16*row);

			for (int plane = 0; plane <= 1; plane++) {
				int cfg = (cloud_rows[row] >> (plane == 0 ? 8 : 0)) & 255;
				int layer = cfg >> 4;

				//int speed = 4-layer; int dx = (speed*t) >> 1;
				int speed = 7-layer; int dx = (speed*t) >> 3;

				copper_write(copper_dest, REG_ADDR_SCROLL_X0 + 2*plane, dx&511);
			}
		}

		// Scoller scroll
		copper_wait_y(copper_dest, CMP_Y0 + 16*num_cloud_rows);
		copper_write(copper_dest, REG_ADDR_SCROLL_X0, (char_scroll-118)&511);

		// Checker board
		// -------------
		//int pal_i = 0;
		int pal_i = -8;
		int next_pal_y = 0;
		for (int i = 0; i < 8*checker_num_rows; i++) {
			int sy = 8*checker_row0 + i;
			if (sy > 0) copper_wait_y(copper_dest, CMP_Y0 + 2*sy);

			// scroll checker board
			int y = i + 1;
			//int dx = t;
			int dx = -2*t;
			dx = (((dx&127) - 64)*y) >> 8;
			copper_write(copper_dest, REG_ADDR_SCROLL_X1, -128+(512-320)/2 + dx); // not centered?

			if (i >= next_pal_y) {
				if (pal_i < 0) {
					int fade = max(pal_i + 8 - 2, 0);
					int r = min(7, cloud_pal[0] + fade);
					int g = min(7, cloud_pal[1] + fade);
					int b = min(7, cloud_pal[2] + fade);
					copper_write_pal(copper_dest, 0, pack_rgb333(r, g, b));
				} else {
					// Update checkerboard palette
					for (int i = 0; i < 4; i++) {
						int fade = 7 - pal_i;
						//if (i == 0 && fade == 0) fade = 1;
						int r = min(7, i + fade);
						int g = min(7, i*2 + fade);
						int b = min(7, i*2 + fade);
						copper_write_pal(copper_dest, i, pack_rgb333(r, g, b));
					}
					if (!still) {
						// Update logo2 palette
						for (int i = 0; i < 4; i++) {
							int fade = 7 - pal_i;
							int r = min(7, logo_b2_pal[i*3] + fade);
							int g = min(7, logo_b2_pal[i*3+1] + fade);
							int b = min(7, logo_b2_pal[i*3+2] + fade);
							copper_write_pal(copper_dest, i+12, pack_rgb333(r, g, b));
						}
					}
				}

				pal_i++;
				if (pal_i < 8) next_pal_y = 32 + 4*pal_i;
				else next_pal_y = 512;
			}

			if (sy == y_sprite_split) {
				copper_write_sorted_base(copper_dest, sorted_base + alt*alt_offset + sub_offset);
				copper_write_oam_base(copper_dest, oam_base + alt*alt_offset + sub_offset);
			}
		}

		// Logo
		// ----
		for (int l=0; l < num_letters; l++) {
			float phi = t*.5*0.15 + (6.28/16)*l;
			float phi2 = t*.5*0.1 + (6.28/16)*l;

			letter_offs_x[l] = still ? 0 : int(32*cosf(phi2));
			letter_offs_y[l] = still ? 0 : int(12*sinf(phi));
		}

		memcpy(sprites2, sprites, sizeof(sprites2));
		for (int i = 0; i < num_sprites; i++) {
			Sprite &s = sprites2[i];
			s.x += letter_offs_x[sprite_l[i]];
			s.y += letter_offs_y[sprite_l[i]];
		}
		update_oam(oam_base + alt*alt_offset, sprites2, num_sprites);
		update_sorted(sorted_base + alt*alt_offset, sprites2, num_sprites);

		memcpy(sprites_b2, sprites_b, sizeof(sprites_b2));
		for (int i = 0; i < num_sprites_b; i++) {
			Sprite &s = sprites_b2[i];
			s.x += letter_offs_x[sprite_l_b[i]];
			s.y -= letter_offs_y[sprite_l_b[i]] >> 1;
		}
		update_oam(oam_base + alt*alt_offset + sub_offset, sprites_b2, num_sprites_b);
		update_sorted(sorted_base + alt*alt_offset + sub_offset, sprites_b2, num_sprites_b);

		// Scroller
		// --------
		while (((char_x*8 - char_scroll)&511) < 48*8) {
			char ch = take_letter(str);
			char_x += put_letter(plane0_addr, char_x, char_y, ch, char_pal);
		}

		// Switch to next frame
		// --------------------
		enable_copper(copper_list_addr + alt*alt_offset);


		t += 1;

		char_scroll += 1;
		if (char_scroll >= max_char_scroll) {
			char_scroll = 0;
			char_x = 40;
			str = message;
		}

		alt++;
		if (alt >= num_alt_buffers) alt = 0;

		if (still) break;
	}
}


void serial_subordinate() {

	uint64_t data = 0;
	int data_counter = 0;

	while (true) {
		int c = getchar_timeout_us(1000);
		if (c != PICO_ERROR_TIMEOUT) {
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
	}
}


void demo_main() {
	init_console();
	//show_checkerboard();
	//show_clouds();
	//show_logo();
	//sprite_test();
	//show_scroller();
	show_demo(false);
	//show_demo(true); // still image
	//while (true) {}
	serial_subordinate();
}
