#include "utils.h"
#include "defaults.h"
#include "globals.h"
#include "ram.h"
#include "cpu.h"
#include "disk.h"
#include "cpm.h"
#include "pal.h"

#include <SPI.h>
#include <SD.h>

#define SDELAY 50
#define DELAY 100


void setup(void) {
	pinMode(EMULATOR_LED, OUTPUT);
	digitalWrite(EMULATOR_LED, LOW);
	Serial.begin(9600);
	while (!Serial) {	// Wait until serial is connected
		digitalWrite(EMULATOR_LED, HIGH);
		delay(SDELAY);
		digitalWrite(EMULATOR_LED, LOW);
		delay(SDELAY);
	}
#ifdef DEBUG_LOG
	_sys_deletefile((uint8 *)LogName);
#endif

	_clrscr();
	_pal_puts("CP/M 2.2 Emulator v" EMULATOR_VERSION " by Marcelo Dantas\r\n");
	_pal_puts("Arduino read/write support by Krzysztof Klis\r\n");
	_pal_puts("      Build " __DATE__ " - " __TIME__ "\r\n");
	_pal_puts("--------------------------------------------\r\n");
	_pal_puts("CCP: " GLB_CCP_NAME "  CCP Address: 0x");
	_puthex16(GLB_CCP_ADDR);
	_pal_puts("\r\n");

	if (SD.begin(SD_SPI_CS)) {
		if (SD.exists(GLB_CCP_NAME)) {
			while (true) {
				_pal_puts(GLB_CCP_BANNER);
				if (!_pal_ram_load((uint8_t*)GLB_CCP_NAME, GLB_CCP_ADDR)) {
					_cpm_patch();
					_cpu_reset();
					CPU_REG_SET_LOW(_cpu_regs.bc, _ram_read(0x0004));
					_cpu_regs.pc = GLB_CCP_ADDR;
					_cpu_run();
					if (_cpu_status == 1)
						break;
				} else {
					_pal_puts("Unable to load the CCP. CPU halted.\r\n");
					break;
				}
			}
		} else {
			_pal_puts("Unable to load CP/M CCP. CPU halted.\r\n");
		}
    }
}

void loop(void) {
	digitalWrite(EMULATOR_LED, HIGH);
	delay(DELAY);
	digitalWrite(EMULATOR_LED, LOW);
	delay(DELAY);
	digitalWrite(EMULATOR_LED, HIGH);
	delay(DELAY);
	digitalWrite(EMULATOR_LED, LOW);
	delay(DELAY * 4);
}
