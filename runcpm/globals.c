#include "globals.h"

/* Definition of global variables */
uint8_t glb_file_name[17];      // Current filename in host filesystem format
uint8_t glb_new_name[17];       // New filename in host filesystem format
uint8_t glb_fcb_name[13];       // Current filename in CP/M format
uint8_t glb_pattern[13];        // File matching pattern in CP/M format
uint16_t glb_dma_addr = 0x0080; // Current dmaAddr
uint8_t glb_o_drive = 0;            // Old selected drive
uint8_t glb_c_drive = 0;            // Currently selected drive
uint8_t glb_user_code = 0;      // Current user code
uint16_t glb_ro_vector = 0;
uint16_t glb_login_vector = 0;
