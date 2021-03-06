#ifndef _GLOBALS_H
#define _GLOBALS_H

#include <stdint.h>

#define GLB_STR_HELPER(x) #x
#define GLB_STR(x) GLB_STR_HELPER(x)

#if defined(EMULATOR_OS_DOS) || defined(EMULATOR_OS_WIN32)
#define GLB_FOLDER_SEP '\\'
#else
#define GLB_FOLDER_SEP '/'
#endif

//// Size of the allocated pages (Minimum size = 1 page = 256 bytes)
#define GLB_BIOS_PAGE           (EMULATOR_RAM_SIZE*1024 - 256)
#define GLB_BIOS_JUMP_PAGE      (GLB_BIOS_PAGE - 256)
#define GLB_BDOS_PAGE           (GLB_BIOS_JUMP_PAGE - 256)
#define GLB_BDOS_JUMP_PAGE      (GLB_BDOS_PAGE - 256)

#define GLB_DPB_ADDR (GLB_BIOS_PAGE + 64)   // Address of the Disk Parameters Block (Hardcoded in BIOS)

#define GLB_SCB_ADDR (GLB_BDOS_PAGE + 16)   // Address of the System Control Block
#define GLB_TMP_FCB_ADDR  (GLB_BDOS_PAGE + 64)  // Address of the temporary FCB

#ifdef EMULATOR_CCP_DR
#define GLB_CCP_FILE
#define GLB_CCP_NAME        "CCP-DR." GLB_STR(EMULATOR_RAM_SIZE) "K"
#define GLB_CCP_VERSION 0x00                    // Version to be used by INFO.COM
#define GLB_BATCH_FCB_ADDR  (GLB_CCP_ADDR + 0x7AC)      // Position of the $$$.SUB fcb on this CCP
#define GLB_CCP_ADDR        (GLB_BDOS_JUMP_PAGE-0x0800) // CCP memory address
#endif
//
#ifdef EMULATOR_CCP_CCPZ
#define GLB_CCP_FILE
#define GLB_CCP_NAME        "CCP-CCPZ." GLB_STR(EMULATOR_RAM_SIZE) "K"
#define GLB_CCP_VERSION 0x01
#define GLB_BATCH_FCB_ADDR  (GLB_CCP_ADDR + 0x7A)       // Position of the $$$.SUB fcb on this CCP
#define GLB_CCP_ADDR        (GLB_BDOS_JUMP_PAGE-0x0800)
#endif
//
#ifdef EMULATOR_CCP_ZCPR2
#define GLB_CCP_FILE
#define GLB_CCP_NAME        "CCP-ZCP2." GLB_STR(EMULATOR_RAM_SIZE) "K"
#define GLB_CCP_VERSION 0x02
#define GLB_BATCH_FCB_ADDR  (GLB_CCP_ADDR + 0x5E)       // Position of the $$$.SUB fcb on this CCP
#define GLB_CCP_ADDR        (GLB_BDOS_JUMP_PAGE-0x0800)
#endif
//
#ifdef EMULATOR_CCP_ZCPR3
#define GLB_CCP_FILE
#define GLB_CCP_NAME        "CCP-ZCP3." GLB_STR(EMULATOR_RAM_SIZE) "K"
#define GLB_CCP_VERSION 0x03
#define GLB_BATCH_FCB_ADDR  (GLB_CCP_ADDR + 0x5E)       // Position of the $$$.SUB fcb on this CCP
#define GLB_CCP_ADDR        (GLB_BDOS_JUMP_PAGE-0x1000)
#endif
//
#ifdef EMULATOR_CCP_Z80
#define GLB_CCP_FILE
#define GLB_CCP_NAME        "CCP-Z80." GLB_STR(EMULATOR_RAM_SIZE) "K"
#define GLB_CCP_VERSION 0x04
#define GLB_BATCH_FCB_ADDR  (GLB_CCP_ADDR + 0x79E)      // Position of the $$$.SUB fcb on this CCP
#define GLB_CCP_ADDR        (GLB_BDOS_JUMP_PAGE-0x0800)
#endif
//
#ifdef EMULATOR_CCP_EMULATED
#define GLB_CCP_NAME		"Emulated v1.3"			// Will use the CCP from ccp.h
#define GLB_CCP_VERSION	0x13
#define GLB_BATCH_FCB_ADDR	(GLB_TMP_FCB_ADDR + 36)
#define GLB_CCP_ADDR		(GLB_BDOS_JUMP_PAGE-0x0800)
#endif

#ifdef EMULATOR_CCP_INTERNAL
#ifdef RAM_SIZE
#undef RAM_SIZE
#define RAM_SIZE 64
#endif

#define GLB_CCP_MEM
#define GLB_CCP_NAME        "CCP-ZCP3." GLB_STR(EMULATOR_RAM_SIZE) "K(I)"
#define GLB_CCP_VERSION 0x03
#define GLB_BATCH_FCB_ADDR  (GLB_CCP_ADDR + 0x5E)       // Position of the $$$.SUB fcb on this CCP
#define GLB_CCP_ADDR        (GLB_BDOS_JUMP_PAGE-0x1000)
#define GLB_CCP_BUFFER
#endif
//
#define GLB_CCP_BANNER      "\r\nRunCPM Version " EMULATOR_VERSION " (CP/M 2.2 " GLB_STR(EMULATOR_RAM_SIZE) "K)\r\n"

/* Definition of global variables */
extern uint8_t glb_file_name[17];       // Current filename in host filesystem format
extern uint8_t glb_new_name[17];        // New filename in host filesystem format
extern uint8_t glb_fcb_name[13];        // Current filename in CP/M format
extern uint8_t glb_pattern[13];         // File matching pattern in CP/M format
extern uint16_t glb_dma_addr;   // Current dmaAddr
extern uint8_t glb_c_drive;             // Old selected drive
extern uint8_t glb_o_drive;             // Currently selected drive
extern uint8_t glb_user_code;       // Current user code
extern uint16_t glb_ro_vector;
extern uint16_t glb_login_vector;

#endif
