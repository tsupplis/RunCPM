
/*
        RunCPM - Execute 8bit CP/M applications on modern computers
        Copyright (c) 2016 - Marcelo Dantas

        Extensive debugging/testing by Tom L. Burnett
        Debugging/testing and new features by Krzysztof Klis
        DOS and Posix ports added by Krzysztof Klis

        Best operating system ever by Gary Kildall, 40 years ago
        I was 9 at that time and had no idea what a computer was
 */

#include "defaults.h"
#include "globals.h"
#include "pal.h"
#include "ram.h"
#include "cpm.h"        


#ifndef ARDUINO

int main(int argc, char *argv[]) {
    pal_console_init();
    pal_puts("Coming up....\r\n");
    if(!pal_init()) {
        pal_puts("Unable to initialize the system. CPU halted.\r\n");
        return -1;
    }
    #ifdef DEBUG_LOG
        pal_delete_file((uint8_t*)DEBUG_LOG_PATH);
    #endif
    ram_init();
    cpm_banner();
    cpm_loop();
    pal_console_reset();
    return 0;
}

#endif
