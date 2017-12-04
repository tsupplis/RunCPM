#include "defaults.h"
#include "ram.h"

#ifndef RAM_SPI
static uint8_t RAM[RAM_SIZE*1024]={0};			// Definition of the emulated RAM
#endif

#ifndef RAM_SPI
uint8_t _ram_read(uint16_t address) {
	return(RAM[address]);
}

void _ram_write(uint16_t address, uint8_t value) {
	RAM[address] = value;
}
#endif

void _ram_init() {
   _ram_fill(0,RAM_SIZE*1024,0);
}

void _ram_write16(uint16_t address, uint16_t value) {
	// Z80 is a "little indian" (8 bit era joke)
	_ram_write(address, value & 0xff);
	_ram_write(address + 1, (value >> 8) & 0xff);
}

uint16_t _ram_read16(uint16_t address) {
 return (((uint16_t)(_ram_read((address & 0xffff) + 1)) << 8) | _ram_read(address & 0xffff));
}

void _ram_fill(uint16_t address, int size, uint8_t value) {
	while (size--)
		_ram_write(address++, value);
}

void _ram_copy(uint16_t source, int size, uint16_t destination) {
	while (size--)
		_ram_write(destination++, _ram_read(source++));
}
