#include "utils.h"
#include "defaults.h"
#include "pal.h"
#include "ram.h"
#include "globals.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

uint8_t _pal_file_match(uint8_t *fcbname, uint8_t *pattern) {
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

uint8_t _chready(void)		// Checks if there's a character ready for input
{
	return(_kbhit() ? 0xff : 0x00);
}

uint8_t _getchNB(void)		// Gets a character, non-blocking, no echo
{
	return(_kbhit() ? _getch() : 0x00);
}

void _putcon(uint8_t ch)		// Puts a character
{
	_putch(ch & 0x7f);
}

void _pal_puts(const char *str)	// Puts a \0 terminated string
{
	while (*str)
		_putcon(*(str++));
}

void _puthex8(uint8_t c)		// Puts a HH hex string
{
	uint8_t h;
	h = c >> 4;
	_putcon(tohex(h));
	h = c & 0x0f;
	_putcon(tohex(h));
}

void _puthex16(uint16_t w)	// puts a HHHH hex string
{
	_puthex8(w >> 8);
	_puthex8(w & 0x00ff);
}
