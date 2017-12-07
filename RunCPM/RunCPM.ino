#include "defaults.h"
#include "globals.h"
#include "cpm.h"

#include <SPI.h>
#include <SD.h>

#define DELAY 100


void setup(void) {
    if(cpm_init()) {
	    cpm_loop();
    }
}

void loop(void) {
	digitalWrite(EMULATOR_LED, HIGH);
	delay(DELAY);
	digitalWrite(EMULATOR_LED, LOW);
	delay(DELAY);
}
