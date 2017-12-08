#include "defaults.h"
#include "ram.h"

#ifndef ARDUINO
static uint8_t RAM[64*1024]={0};         // Definition of the emulated RAM

uint8_t ram_read(uint16_t address) {
	return(RAM[address]);
}

void ram_write(uint16_t address, uint8_t value) {
	RAM[address] = value;
}
#endif

void ram_init() {
	ram_fill(0,EMULATOR_RAM_SIZE*1024,0);
}

void ram_write16(uint16_t address, uint16_t value) {
	// Z80 is a "little indian" (8 bit era joke)
	ram_write(address, value & 0xff);
	ram_write(address + 1, (value >> 8) & 0xff);
}

uint16_t ram_read16(uint16_t address) {
	return (((uint16_t)(ram_read((address & 0xffff) + 1)) << 8) | ram_read(address & 0xffff));
}

void ram_fill(uint16_t address, int size, uint8_t value) {
	while (size--) {
		ram_write(address++, value);
	}
}

void ram_copy(uint16_t source, int size, uint16_t destination) {
	while (size--) {
		ram_write(destination++, ram_read(source++));
	}
}
