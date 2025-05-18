#pragma once

#include "common.h"

#define REG_ADDR_PAL            0
#define REG_ADDR_SCROLL_X0     16
#define REG_ADDR_SCROLL_Y0     17
#define REG_ADDR_SCROLL_X1     18
#define REG_ADDR_SCROLL_Y1     19
#define REG_ADDR_CMP_X         20
#define REG_ADDR_CMP_Y         21
#define REG_ADDR_JUMP1_LSB     22
#define REG_ADDR_JUMP2_MSB     23
#define REG_ADDR_SORTED_BASE   24
#define REG_ADDR_OAM_BASE      25
#define REG_ADDR_MAP_BASE      26
#define REG_ADDR_TILE_BASE     27
#define REG_ADDR_DISPLAY_MASK  31


#define COPPER_START_ADDR 		0xfffe

#define COPPER_ADDR_BITS 6
#define COPPER_VALUE_SHL 7


// display_mask bits
#define DM_DISPLAY_PLANE0   1
#define DM_DISPLAY_PLANE1   2
#define DM_DISPLAY_SPRITES  4

#define DM_LOAD_PLANE0      8
#define DM_LOAD_PLANE1     16
#define DM_LOAD_SPRITES    32

#define DM_PLANE0       (1| 8)
#define DM_PLANE1       (2|16)
#define DM_SPRITES      (4|32)

#define DM_ALL 63


#define TILE_INDEX_BITS 11


inline void disable_copper() {
	// Replace the jump with -1 to disable the copper as soon as it starts.
	emu_ram[COPPER_START_ADDR+1] = -1; // Replace the actual jump first, so we don't jump to a bad place.
	emu_ram[COPPER_START_ADDR]   = -1;
}

inline void enable_copper(int copper_list_addr) {
	disable_copper();
	emu_ram[COPPER_START_ADDR]     = REG_ADDR_JUMP1_LSB | ((copper_list_addr & 0xff) << 8);
	// Write the actual jump last to avoid taking it before it has been set up.
	// Tiny risk that the copper has read the wrong lsb and then reads the jump below?
	emu_ram[COPPER_START_ADDR + 1] = REG_ADDR_JUMP2_MSB | (copper_list_addr & 0xff00);
}

inline ram_uint16 *emu_ram_ptr(int addr) { return emu_ram + (addr & 0xffff); }

inline void copper_write(ram_uint16 *&copper_dest, int reg, int value, bool fast_mode = false) { *(copper_dest++) = (reg & ((1 << COPPER_ADDR_BITS)-1)) | (fast_mode << COPPER_ADDR_BITS) | (value << COPPER_VALUE_SHL); }
inline void copper_stop( ram_uint16 *&copper_dest) { *(copper_dest++) = -1; }

inline void copper_write_pal(ram_uint16 *&copper_dest, int index, int value, bool fast_mode = false) { copper_write(copper_dest, REG_ADDR_PAL + (index&15), value << 1, fast_mode); }

// Ignore LSB of b (just like AnemoneGrafx 8)
inline int pack_rgb333(int r, int g, int b) { return (((r&7) << 5) | ((g&7) << 2) | ((b&7) >> 1)); }
inline void copper_write_pal333(ram_uint16 *&copper_dest, int index, int r, int g, int b, bool fast_mode = false) { copper_write_pal(copper_dest, index, pack_rgb333(r, g, b), fast_mode); }


inline void copper_write_map_base( ram_uint16 *&copper_dest, int addr0, int addr1, bool fast_mode = false)   { copper_write(copper_dest, REG_ADDR_MAP_BASE,  ((addr0 >> 12) | ((addr1 >> 12) << 4)) << 1, fast_mode); }
inline void copper_write_tile_base(ram_uint16 *&copper_dest, int addr_p, int addr_s, bool fast_mode = false) { copper_write(copper_dest, REG_ADDR_TILE_BASE, ((addr_p >> 14) | ((addr_s >> 14) << 2)) << 1, fast_mode); }

// Must be in first half of memory
inline void copper_write_sorted_base( ram_uint16 *&copper_dest, int addr, bool fast_mode = false) { copper_write(copper_dest, REG_ADDR_SORTED_BASE,  (addr >> 6), fast_mode); }

inline void copper_write_oam_base(    ram_uint16 *&copper_dest, int addr, bool fast_mode = false) { copper_write(copper_dest, REG_ADDR_OAM_BASE,     (addr >> 7), fast_mode); }


inline int pack_map(int index, int pal, bool opaque=false) { return (index & ((1 << TILE_INDEX_BITS)-1)) | (opaque << TILE_INDEX_BITS) | (pal << 12); }

inline ram_uint16 *map_ptr(int base_addr, int x, int y) { return emu_ram_ptr(base_addr | (x&63) | ((y&63) << 6)); }
inline void write_map(int base_addr, int x, int y, int value) { *map_ptr(base_addr, x, y) = value; }
inline uint16_t read_map(int base_addr, int x, int y) { return *map_ptr(base_addr, x, y); }


inline ram_uint16 *tile_row_ptr(int base_addr, int index, int y) { return emu_ram_ptr(base_addr | ((index & 0x7ff) << 3) | (y & 7)); }
inline void write_tile_row(int base_addr, int index, int y, int value) { *tile_row_ptr(base_addr, index, y) = value; }
inline uint16_t read_tile_row(int base_addr, int index, int y) { return *tile_row_ptr(base_addr, index, y); }

inline void write_sprite_tile_row(int base_addr, int index, int y, int value) {
	*tile_row_ptr(base_addr, index*2, y) = value;
	*tile_row_ptr(base_addr, index*2 + 1, y) = value>>16;
}


// Assumes vertical polarity 1 (vsync active low)
inline void wait_vsync() {
	while (!gpio_get(VSYNC_PIN)) {} // wait for vsync inactive
	while ( gpio_get(VSYNC_PIN)) {} // wait for vsync active
}


inline void copper_wait_y(ram_uint16 *&copper_dest, int cmp_y) { copper_write(copper_dest, REG_ADDR_CMP_Y, cmp_y); }


// {m1, m0} must be the bottom two bits
#define SPRITE_FLAG_MASK0 1
#define SPRITE_FLAG_MASK1 2
#define SPRITE_FLAG_OPAQUE 4
#define SPRITE_FLAG_ON 8


typedef struct {
	int16_t x;
	uint8_t y;
	uint8_t pal;
	int16_t tile_index;
	uint8_t depth;
	uint8_t flags;
} Sprite;

void update_oam(int base_addr, const Sprite *sprites, int n);
// not reentrant
void update_sorted(int base_addr, const Sprite *sprites, int n);
