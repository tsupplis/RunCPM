#include "defaults.h"
#include "globals.h"
#include "pal.h"
#include "ram.h"
#include "cpm.h"

#define DELAY 100

void setup(void) {
    pal_console_init();
    pal_puts("Coming up....\r\n");
    if(!pal_init()) {
        pal_puts("Unable to initialize the system. CPU halted.\r\n");
        return;
    }
    #ifdef DEBUG_LOG
        pal_delete_file((uint8_t*)DEBUG_LOG_PATH);
    #endif
    ram_init();
    cpm_banner();
	cpm_loop();
    pal_console_reset();
}

void loop(void) {
	digitalWrite(EMULATOR_LED, HIGH);
	delay(DELAY);
	digitalWrite(EMULATOR_LED, LOW);
	delay(DELAY);
}
