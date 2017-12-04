#ifndef ARDUINO

/*
		RunCPM - Execute 8bit CP/M applications on modern computers
		Copyright (c) 2016 - Marcelo Dantas

		Extensive debugging/testing by Tom L. Burnett
		Debugging/testing and new features by Krzysztof Klis
		DOS and Posix ports added by Krzysztof Klis

		Best operating system ever by Gary Kildall, 40 years ago
		I was 9 at that time and had no idea what a computer was
*/

#include "utils.h"
#include "defaults.h"
#include "globals.h"
#include "pal.h"
#include "ram.h"		// ram.h - Implements the RAM
#include "disk.h"		// disk.h - Defines all the disk access abstraction functions
#include "cpm.h"		// cpm.h - Defines the CPM structures and calls
#include "cpu.h"		// cpm.h - Defines the CPM structures and calls

int main(int argc, char *argv[]) {

#ifdef DEBUG_LOG
	_sys_deletefile((uint8_t*)DEBUG_LOG_PATH);
#endif
    _ram_init();
	_console_init();
	_clrscr();
	_pal_puts("CP/M 2.2 Emulator v" EMULATOR_VERSION " by Marcelo Dantas\r\n");
	_pal_puts("      Build " __DATE__ " - " __TIME__ "\r\n");
	_pal_puts("-----------------------------------------\r\n");
	_pal_puts("CCP: " GLB_CCP_NAME "  CCP Address: 0x");
	_puthex16(GLB_CCP_ADDR);
	_pal_puts("\r\n");

	while (1) {
		if(!_sys_exists((uint8_t*)GLB_CCP_NAME)) {
			_pal_puts("\r\nCan't open CCP!\r\n");
			break;
		} else {
			_pal_puts(GLB_CCP_BANNER);
			_pal_ram_load((uint8_t*)GLB_CCP_NAME, GLB_CCP_ADDR);	// Loads the CCP binary file into memory
			_cpm_patch();	// Patches the CP/M entry points and other things in


			_cpu_reset();			// Resets the Z80 CPU
			CPU_REG_SET_LOW(_cpu_regs.bc, _ram_read(0x0004));	// Sets C to the current drive/user
			_cpu_regs.pc = GLB_CCP_ADDR;		// Sets CP/M application jump point
			_cpu_run();			// Starts simulation
			if (_cpu_status == 1)	// This is set by a call to BIOS 0 - ends CP/M
				break;
		}
	}

	_console_reset();
	_pal_puts("\r\n");
	return(0);
}

#endif
