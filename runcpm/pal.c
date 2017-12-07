#include "defaults.h"
#include "pal.h"
#include "ram.h"
#include "globals.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define TO_HEX(x)   (x < 10 ? x + 48 : x + 87)

uint8_t pal_load_buffer(uint8_t* src, uint32_t len, uint16_t address) {
	int i;

    for(i=0; i<len; i++) {
		ram_write(address+i,src[i]);
	}
	return 0;
}


uint8_t pal_file_match(uint8_t *fcbname, uint8_t *pattern) {
	uint8_t result = 1;
	uint8_t i;

	for (i = 0; i < 12; i++) {
		if (*pattern == '?' || *pattern == *fcbname) {
			pattern++; fcbname++;
			continue;
		} else {
			result = 0;
			break;
		}
	}
	return(result);
}

uint8_t pal_chready(void)       // Checks if there's a character ready for input
{
	return(pal_kbhit() ? 0xff : 0x00);
}

uint8_t pal_getch_nb(void)      // Gets a character, non-blocking, no echo
{
	return(pal_kbhit() ? pal_getch() : 0x00);
}

void pal_put_con(uint8_t ch)        // Puts a character
{
	pal_putch(ch & 0x7f);
}

void pal_puts(const char *str)  // Puts a \0 terminated string
{
	while (*str)
		pal_put_con(*(str++));
}

void pal_put_hex8(uint8_t c)        // Puts a HH hex string
{
	uint8_t h;
	h = c >> 4;
	pal_put_con(TO_HEX(h));
	h = c & 0x0f;
	pal_put_con(TO_HEX(h));
}

void pal_put_hex16(uint16_t w)  // puts a HHHH hex string
{
	pal_put_hex8(w >> 8);
	pal_put_hex8(w & 0x00ff);
}
