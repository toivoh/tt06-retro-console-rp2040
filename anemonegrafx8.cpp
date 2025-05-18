#include <string.h>
#include <stdio.h>
#include "anemonegrafx8.h"


void update_oam(int base_addr, const Sprite *sprites, int n) {
	if (n > 64) n = 64;
	ram_uint16 *oam = emu_ram_ptr(base_addr & 0xff80);

	for (int i=0; i < n; i++) {
		const Sprite &s = sprites[i];
		oam[2*i]   = (s.y&7) | ((s.tile_index&0x3ff) << 4);
		oam[2*i+1] = (s.x&511) | ((s.depth&3) << 9) | (s.flags&SPRITE_FLAG_OPAQUE ? (1 << 11) : 0) | ((s.pal&15) << 12);
	}
}


//#define DEBUG_SORT

void update_sorted(int base_addr, const Sprite *sprites, int n) {
	const int num_buckets = 512;
	static uint8_t buckets[num_buckets];

	if (n > 64) n = 64;

	// Set up buckets as destionation pointers into the list
	memset(buckets, 0, sizeof(buckets));
	for (int i=0; i < n; i++) if (sprites[i].flags & SPRITE_FLAG_ON) buckets[sprites[i].x & (num_buckets-1)]++;

#ifdef DEBUG_SORT
	printf("buckets = [");
	for(int i=0; i < num_buckets; i++) printf("%d, ", buckets[i]);
	printf("]\r\n");
#endif

	int index = 0;
	for (int i=0; i < num_buckets; i++) {
		int size = buckets[i];
		buckets[i] = index;
		index += size;
	}

#ifdef DEBUG_SORT
	printf("buckets = [");
	for(int i=0; i < num_buckets; i++) printf("%d, ", buckets[i]);
	printf("]\r\n");

	printf("sorted = [");
#endif
	// Fill in the sprites in sorted order
	ram_uint16 *sorted = emu_ram_ptr(base_addr & 0xffc0);
	for (int i=0; i < n; i++) {
		const Sprite &s = sprites[i];
		if (!(s.flags & SPRITE_FLAG_ON)) continue;
		int x = s.x & (num_buckets-1);
		int index = buckets[x];
		buckets[x]++;

		sorted[index] = (s.y&255) | (((i&63)<<8)) | ((s.flags&3)<<14);

#ifdef DEBUG_SORT
		printf("(%d, %d, %d, %d), ", index, i, s.x, s.y);
#endif
	}
#ifdef DEBUG_SORT
	printf("]\r\n");

	printf("buckets = [");
	for(int i=0; i < num_buckets; i++) printf("%d, ", buckets[i]);
	printf("]\r\n");

	printf("sorted_list = [");
	for(int i=0; i < n; i++) printf("0x%x, ", sorted[i]);
	printf("]\r\n");

	printf("sorted_x = [");
	for(int i=0; i < n; i++) printf("%d, ", sprites[(sorted[i]>>8)&63].x);
	printf("]\r\n");
#endif

	// Clear out the remaining, unused sprites
	for (int i=buckets[num_buckets-1]; i < 64; i++) sorted[i] = -1;
}
