
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
#include "cpm.h"        


#ifndef ARDUINO

int main(int argc, char *argv[]) {
    if(cpm_init()) {
        cpm_loop();
    } else {
        return -1;
    }
	return 0;
}

#endif
