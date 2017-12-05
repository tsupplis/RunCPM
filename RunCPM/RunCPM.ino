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

#define DELAY 100


void setup(void) {
	pinMode(EMULATOR_LED, OUTPUT);
	digitalWrite(EMULATOR_LED, LOW);
#ifdef DEBUG_LOG
	_sys_deletefile((uint8_t *)DEBUG_LOG_PATH);
#endif

    _ram_init();
    _console_init();
    _cpm_banner();

	if (SD.begin(SD_SPI_CS)) {
		if (SD.exists(GLB_CCP_NAME)) {
			while (1) {
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
