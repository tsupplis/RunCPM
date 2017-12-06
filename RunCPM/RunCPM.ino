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
  pal_delete_file((uint8_t *)DEBUG_LOG_PATH);
#endif

  ram_init();
  pal_console_init();
  cpm_banner();

  if (SD.begin(SD_SPI_CS)) {
    if (SD.exists(GLB_CCP_NAME)) {
      while (1) {
        if (!pal_ram_load((uint8_t*)GLB_CCP_NAME, GLB_CCP_ADDR)) {
          cpm_patch();
          cpu_reset();
          CPU_REG_SET_LOW(cpu_regs.bc, ram_read(0x0004));
          cpu_regs.pc = GLB_CCP_ADDR;
          cpu_run();
          if (cpu_status == 1) {
            break;
          }
        } else {
          pal_puts("Unable to load the CCP. CPU halted.\r\n");
          break;
        }
      }
    } else {
      pal_puts("Unable to load CP/M CCP. CPU halted.\r\n");
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
