#include "defaults.h"
#include "globals.h"
#include "ram.h"
#include "pal.h"
#include "disk.h"
#include "cpu.h"
#include "cpm.h"

#include <ctype.h>

#ifdef EMULATOR_CCP_EMULATED

#define CCP_JP              0xc3
#define CCP_CALL            0xcd
#define CCP_C_READ          1
#define CCP_C_WRITE         2
#define CCP_C_READSTR       10
#define CCP_DRV_ALLRESET    13
#define CCP_DRV_SET         14
#define CCP_F_OPEN          15
#define CCP_F_CLOSE         16
#define CCP_F_DELETE        19
#define CCP_F_READ          20
#define CCP_F_WRITE         21
#define CCP_F_MAKE          22
#define CCP_F_RENAME        23
#define CCP_DRV_GET         25
#define CCP_F_DMAOFF        26
#define CCP_F_USERNUM       32
#define CCP_F_RUNLUA        254

#define CCP_CMD_FCB (GLB_BATCH_FCB_ADDR + 36)       // FCB for use by internal commands
#define CCP_PAR_FCB 0x005C              // FCB for use by line parameters
#define CCP_SEC_FCB 0x006C              // Secondary part of FCB for renaming files
#define CCP_TRAMPOLINE (CCP_CMD_FCB + 36)   // TRAMPOLINE for running external commands

#define CCP_IN_BUFFER   (GLB_BDOS_JUMP_PAGE - 256)  // Input buffer location
#define CMD_LEN 125                 // Maximum size of a command line (sz+rd+cmd+\0)

#define CCP_DEF_DMA 0x0080              // Default DMA address
#define CCP_DEF_LOAD    0x0100              // Default load address

#define CCP_PG_SIZE 24                  // for TYPE

// CCP global variables
static uint8_t ccp_cur_drive;   // 0 -> 15 = A -> P	.. Current drive for the CCP (same as RAM[0x0004]
static uint8_t ccp_par_drive;   // 0 -> 15 = A -> P .. Drive for the first file parameter
static uint8_t ccp_cur_user;    // 0 -> 15			.. Current user aread to access
static uint8_t ccp_s_flag;  //					.. Submit Flag
static uint8_t ccp_prompt[5] = "\r\n >";
static uint16_t ccp_pbuf;
static uint16_t ccp_perr;
static uint8_t ccp_blen;                            // Actual size of the typed command line (size of the buffer)

static const char *ccp_commands[] =
{
	// Standard CP/M commands
	"DIR",
	"ERA",
	"TYPE",
	"SAVE",
	"REN",
	"USER",
	// Extra CCP commands
	"CLS",
	"DEL",
	"EXIT",
	NULL
};

// Used to call BDOS from inside the CCP
static uint16_t ccp_bdos(uint8_t function, uint16_t de) {
	CPU_REG_SET_LOW(cpu_regs.bc, function);
	cpu_regs.de = de;
	cpm_bdos();
	return(cpu_regs.hl & 0xffff);
}

// Compares two strings (Atmel doesn't like strcmp)
static uint8_t ccp_strcmp(char *stra, char *strb) {
	while (*stra && *strb && (*stra == *strb)) {
		stra++; strb++;
	}
	return(*stra == *strb);
}

// Gets the command ID number
uint8_t ccp_cnum(void) {
	uint8_t result = 255;
	uint8_t command[9];
	uint8_t i = 0;

	if (!ram_read(CCP_CMD_FCB)) {   // If a drive was set, then the command is external
		while (i < 8 && ram_read(CCP_CMD_FCB + i + 1) != ' ') {
			command[i] = ram_read(CCP_CMD_FCB + i + 1);
			i++;
		}
		command[i] = 0;

		i = 0;
		while (ccp_commands[i]) {
			if (ccp_strcmp((char*)command, (char*)ccp_commands[i])) {
				result = i;
				ccp_perr = CCP_DEF_DMA + 2;
				break;
			}
			i++;
		}
	}
	return(result);
}

// Returns true if character is a separator
static uint8_t ccp_delim(uint8_t ch) {
	return(ch == 0 || ch == ' ' || ch == '=' || ch == '.' || ch == ':' || ch == ';' || ch == '<' || ch == '>');
}

// Prints the FCB filename
static void ccp_printfcb(uint16_t fcb, uint8_t compact) {
	uint8_t i, ch;

	ch = ram_read(fcb);
	if (ch && compact) {
		ccp_bdos(CCP_C_WRITE, ch + '@');
		ccp_bdos(CCP_C_WRITE, ':');
	}

	for (i = 1; i < 12; i++) {
		ch = ram_read(fcb + i);
		if (ch == ' ' && compact)
			continue;
		if (i == 9)
			ccp_bdos(CCP_C_WRITE, compact ? '.' : ' ');
		ccp_bdos(CCP_C_WRITE, ch);
	}
}

// Initializes the FCB
static void ccp_init_fcb(uint16_t address) {
	uint8_t i;

	for (i = 0; i < 36; i++)
		ram_write(address + i, 0x00);
	for (i = 0; i < 11; i++) {
		ram_write(address + 1 + i, 0x20);
		ram_write(address + 17 + i, 0x20);
	}
}

// Name to FCB
static uint8_t ccp_name_to_fcb(uint16_t fcb) {
	uint8_t pad, plen, ch, n = 0;

	// Checks for a drive and places it on the Command FCB
	if (ram_read(ccp_pbuf + 1) == ':') {
		ch = toupper(ram_read(ccp_pbuf++));
		ram_write(fcb, ch - '@');       // Makes the drive 0x1-0xF for A-P
		ccp_pbuf++;                         // Points ccp_pbuf past the :
		ccp_blen -= 2;
	}

	if (ccp_blen) {
		fcb++;

		plen = 8;
		pad = ' ';
		ch = toupper(ram_read(ccp_pbuf));
		while (ccp_blen && plen) {
			if (ccp_delim(ch)) {
				break;
			}
			ccp_pbuf++; ccp_blen--;
			if (ch == '*')
				pad = '?';
			if (pad == '?' || ch == '?') {
				ch = pad;
				n = n | 0x80;   // Name is not unique
			}
			plen--; n++;
			ram_write(fcb++, ch);
			ch = toupper(ram_read(ccp_pbuf));
		}

		while (plen--)
			ram_write(fcb++, pad);

		plen = 3;
		pad = ' ';
		if (ch == '.') {
			ccp_pbuf++; ccp_blen--;
		}
		while (ccp_blen && plen) {
			ch = toupper(ram_read(ccp_pbuf));
			if (ccp_delim(ch)) {
				break;
			}
			ccp_pbuf++; ccp_blen--;
			if (ch == '*')
				pad = '?';
			if (pad == '?' || ch == '?') {
				ch = pad;
				n = n | 0x80;   // Name is not unique
			}
			plen--; n++;
			ram_write(fcb++, ch);
		}

		while (plen--)
			ram_write(fcb++, pad);
	}

	return(n);
}

// Converts the CCP_PAR_FCB name to a number
static uint16_t ccp_fcb_to_num() {
	uint8_t ch;
	uint16_t n = 0;
	uint8_t pos = CCP_PAR_FCB + 1;
	while (1) {
		ch = ram_read(pos++);
		if (ch<'0' || ch>'9')
			break;
		n = (n * 10) + (ch - 0x30);
	}
	return(n);
}

// DIR command
static void ccp_dir(void) {
	uint8_t i;
	uint8_t dir_head[6] = "A: ";
	uint8_t dir_sep[4] = " : ";
	uint32_t fcount = 0;    // Number of files printed
	uint32_t ccount = 0;    // Number of columns printed

	if (ram_read(CCP_PAR_FCB + 1) == ' ') {
		for (i = 1; i < 12; i++)
			ram_write(CCP_PAR_FCB + i, '?');
	}

	dir_head[0] = ram_read(CCP_PAR_FCB) ? ram_read(CCP_PAR_FCB) + '@' : 'A';

	pal_puts("\r\n");
	if (!disk_search_first(CCP_PAR_FCB, 1)) {
		pal_puts((char*)dir_head);
		ccp_printfcb(GLB_TMP_FCB_ADDR, 0);
		fcount++; ccount++;
		while (!disk_search_next(CCP_PAR_FCB, 1)) {
			if (!ccount) {
				pal_puts("\r\n");
				pal_puts((char*)dir_head);
			} else {
				pal_puts((char*)dir_sep);
			}
			ccp_printfcb(GLB_TMP_FCB_ADDR, 0);
			fcount++; ccount++;
			if (ccount > 3)
				ccount = 0;
		}
	} else {
		pal_puts("No file");
	}
}

// ERA command
static void ccp_era(void) {
	if (ccp_bdos(CCP_F_DELETE, CCP_PAR_FCB))
		pal_puts("\r\nNo file");
}

// TYPE command
static uint8_t ccp_type(void) {
	uint8_t i, c, l = 0, error = 1;
	uint16_t a;

	if (!ccp_bdos(CCP_F_OPEN, CCP_PAR_FCB)) {
		pal_puts("\r\n");
		while (!ccp_bdos(CCP_F_READ, CCP_PAR_FCB)) {
			i = 128;
			a = glb_dma_addr;
			while (i) {
				c = ram_read(a);
				if (c == 0x1a)
					break;
				ccp_bdos(CCP_C_WRITE, c);
				if (c == 0x0a) {
					l++;
					if (l == CCP_PG_SIZE) {
						l = 0;
						ccp_bdos(CCP_C_READ, 0x0000);
					}
				}
				i--; a++;
			}
		}
		error = 0;
	}
	return(error);
}

// SAVE command
static uint8_t ccp_save(void) {
	uint8_t error = 1;
	uint16_t pages = ccp_fcb_to_num();
	uint16_t i, dma;

	if (pages < 256) {
		error = 0;
		while (ram_read(ccp_pbuf) == ' ' && ccp_blen) {     // Skips any leading spaces
			ccp_pbuf++; ccp_blen--;
		}
		ccp_name_to_fcb(CCP_PAR_FCB);                       // Loads file name onto the CCP_PAR_FCB
		if (ccp_bdos(CCP_F_MAKE, CCP_PAR_FCB)) {
			pal_puts("Err: create");
		} else {
			if (ccp_bdos(CCP_F_OPEN, CCP_PAR_FCB)) {
				pal_puts("Err: open");
			} else {
				pages *= 2;                                 // Calculates the number of CP/M blocks to write
				dma = CCP_DEF_LOAD;
				pal_puts("\r\n");
				for (i = 0; i < pages; i++) {
					ccp_bdos(CCP_F_DMAOFF, dma);
					ccp_bdos(CCP_F_WRITE, CCP_PAR_FCB);
					dma += 128;
					ccp_bdos(CCP_C_WRITE, '.');
				}
				ccp_bdos(CCP_F_CLOSE, CCP_PAR_FCB);
			}
		}
	}
	return(error);
}

// REN command
static void ccp_ren(void) {
	uint8_t ch, i;
	ccp_pbuf++;

	ccp_name_to_fcb(CCP_SEC_FCB);
	for (i = 0; i < 12; i++) {  // Swap the filenames on the fcb
		ch = ram_read(CCP_PAR_FCB + i);
		ram_write(CCP_PAR_FCB + i, ram_read(CCP_SEC_FCB + i));
		ram_write(CCP_SEC_FCB + i, ch);
	}
	if (ccp_bdos(CCP_F_RENAME, CCP_PAR_FCB)) {
		pal_puts("\r\nNo file");
	}
}

// USER command
static uint8_t ccp_user(void) {
	uint8_t error = 1;

	ccp_cur_user = (uint8_t)ccp_fcb_to_num();
	if (ccp_cur_user < 16) {
		ccp_bdos(CCP_F_USERNUM, ccp_cur_user);
		error = 0;
	}
	return(error);
}

#ifdef EMULATOR_HAS_LUA
// External (.LUA) command
static uint8_t ccp_lua(void) {
	uint8_t error = 1;
	uint8_t found, drive, user = 0;
	//uint16_t load_addr = CCP_DEF_LOAD;

	ram_write(CCP_CMD_FCB + 9, 'L');
	ram_write(CCP_CMD_FCB + 10, 'U');
	ram_write(CCP_CMD_FCB + 11, 'A');

	drive = ram_read(CCP_CMD_FCB);
	found = !ccp_bdos(CCP_F_OPEN, CCP_CMD_FCB);                         // Look for the program on the FCB drive, current or specified
	if (!found) {                                               // If not found
		if (!drive) {                                           // and the search was on the default drive
			ram_write(CCP_CMD_FCB, 0x01);                           // Then look on drive A: user 0
			if (ccp_cur_user) {
				user = ccp_cur_user;                                    // Save the current user
				ccp_bdos(CCP_F_USERNUM, 0x0000);                    // then set it to 0
			}
			found = !ccp_bdos(CCP_F_OPEN, CCP_CMD_FCB);
			if (!found) {                                       // If still not found then
				if (ccp_cur_user) {                                 // If current user not = 0
					ram_write(CCP_CMD_FCB, 0x00);                   // look on current drive user 0
					found = !ccp_bdos(CCP_F_OPEN, CCP_CMD_FCB);         // and try again
				}
			}
		}
	}
	if (found) {
		pal_puts("\r\n");

		ccp_bdos(CCP_F_RUNLUA, CCP_CMD_FCB);

		if (user) {                                 // If a user was selected
			user = 0;
			ccp_bdos(CCP_F_USERNUM, ccp_cur_user);          // Set it back
			ram_write(CCP_CMD_FCB, 0x00);
		}
		error = 0;
	}

	if (user) {                                 // If a user was selected
		ccp_bdos(CCP_F_USERNUM, ccp_cur_user);          // Set it back
		ram_write(CCP_CMD_FCB, 0x00);
	}

	return(error);
}
#endif

// External (.COM) command
static uint8_t ccp_ext(void) {
	uint8_t error = 1;
	uint8_t found, drive, user = 0;
	uint16_t load_addr = CCP_DEF_LOAD;

	ram_write(CCP_CMD_FCB + 9, 'C');
	ram_write(CCP_CMD_FCB + 10, 'O');
	ram_write(CCP_CMD_FCB + 11, 'M');

	drive = ram_read(CCP_CMD_FCB);
	found = !ccp_bdos(CCP_F_OPEN, CCP_CMD_FCB);                         // Look for the program on the FCB drive, current or specified
	if (!found) {                                               // If not found
		if (!drive) {                                           // and the search was on the default drive
			ram_write(CCP_CMD_FCB, 0x01);                           // Then look on drive A: user 0
			if (ccp_cur_user) {
				user = ccp_cur_user;                                    // Save the current user
				ccp_bdos(CCP_F_USERNUM, 0x0000);                    // then set it to 0
			}
			found = !ccp_bdos(CCP_F_OPEN, CCP_CMD_FCB);
			if (!found) {                                       // If still not found then
				if (ccp_cur_user) {                                 // If current user not = 0
					ram_write(CCP_CMD_FCB, 0x00);                   // look on current drive user 0
					found = !ccp_bdos(CCP_F_OPEN, CCP_CMD_FCB);         // and try again
				}
			}
		}
	}
	if (found) {
		pal_puts("\r\n");
		ccp_bdos(CCP_F_DMAOFF, load_addr);
		while (!ccp_bdos(CCP_F_READ, CCP_CMD_FCB)) {
			load_addr += 128;
			ccp_bdos(CCP_F_DMAOFF, load_addr);
		}
		ccp_bdos(CCP_F_DMAOFF, CCP_DEF_DMA);

		if (user) {                                 // If a user was selected
			user = 0;
			ccp_bdos(CCP_F_USERNUM, ccp_cur_user);          // Set it back
		}
		ram_write(CCP_CMD_FCB, drive);

		// Place a trampoline to call the external command
		// as it may return using RET instead of CCP_JP 0000h
		load_addr = CCP_TRAMPOLINE;
		ram_write(load_addr, CCP_CALL);
		ram_write16(load_addr + 1, CCP_DEF_LOAD);
		ram_write(load_addr + 3, CCP_JP);
		ram_write16(load_addr + 4, GLB_BIOS_JUMP_PAGE + 0x33);

		cpu_reset();            // Resets the Z80 CPU
		CPU_REG_SET_LOW(cpu_regs.bc, ram_read(0x0004)); // Sets C to the current drive/user
		cpu_regs.pc = load_addr;        // Sets CP/M application jump point
		cpu_regs.sp = GLB_BDOS_JUMP_PAGE;

		cpu_run();          // Starts simulation

		error = 0;
	}

	if (user) {                                 // If a user was selected
		ccp_bdos(CCP_F_USERNUM, ccp_cur_user);          // Set it back
	}
	ram_write(CCP_CMD_FCB, drive);

	return(error);
}

// Prints a command error
static void ccp_cmd_error() {
	uint8_t ch;

	pal_puts("\r\n");
	while ((ch = ram_read(ccp_perr++))) {
		if (ch == ' ')
			break;
		ccp_bdos(CCP_C_WRITE, toupper(ch));
	}
	pal_puts("?\r\n");
}

// Reads input, either from the $$$.SUB or console
static void ccp_read_input(void) {
	uint8_t i;
	int j;
	uint8_t recs = 0;
	uint8_t chars;

	if (ccp_s_flag) {                                   // Are we running a submit?
		if (!ccp_bdos(CCP_F_OPEN, GLB_BATCH_FCB_ADDR)) {            // Open batch file
			recs = ram_read(GLB_BATCH_FCB_ADDR + 15);           // Gets its record count
			if (recs) {
				recs--;                             // Counts one less
				ram_write(GLB_BATCH_FCB_ADDR + 32, recs);       // And sets to be the next read
				ccp_bdos(CCP_F_DMAOFF, CCP_DEF_DMA);        // Reset current DMA
				ccp_bdos(CCP_F_READ, GLB_BATCH_FCB_ADDR);       // And reads the last sector
				chars = ram_read(CCP_DEF_DMA);          // Then moves it to the input buffer
				for (i = 0; i <= chars; i++)
					ram_write(CCP_IN_BUFFER + i + 1, ram_read(CCP_DEF_DMA + i));
				ram_write(CCP_IN_BUFFER + i + 1, 0);

				j=0;
				while(1) {
					uint8_t c=ram_read(CCP_IN_BUFFER+2+j++);
					if(!c) {
						break;
					}
					pal_putch(c);
				}
				ram_write(GLB_BATCH_FCB_ADDR + 15, recs);       // Prepare the file to be truncated
				ccp_bdos(CCP_F_CLOSE, GLB_BATCH_FCB_ADDR);      // And truncates it
			}
		}
		if (!recs) {
			ccp_bdos(CCP_F_DELETE, GLB_BATCH_FCB_ADDR);         // Or else just deletes it
			ccp_s_flag = 0;                             // and clears the submit flag
		}
	} else {
		ccp_bdos(CCP_C_READSTR, CCP_IN_BUFFER);             // Reads the command line from console
	}
}

// Main CCP code
void ccp(void) {

	uint8_t i;

	ccp_s_flag = (uint8_t)ccp_bdos(CCP_DRV_ALLRESET, 0x0000);
	ccp_bdos(CCP_DRV_SET, ccp_cur_drive);

	for (i = 0; i < 36; i++)
		ram_write(GLB_BATCH_FCB_ADDR + i, ram_read(GLB_TMP_FCB_ADDR + i));

	while (1) {
		ccp_cur_drive = (uint8_t)ccp_bdos(CCP_DRV_GET, 0x0000);         // Get current drive
		ccp_cur_user = (uint8_t)ccp_bdos(CCP_F_USERNUM, 0x00FF);            // Get current user
		ram_write(0x0004, (ccp_cur_user << 4) + ccp_cur_drive); // Set user/drive on addr 0x0004

		ccp_par_drive = ccp_cur_drive;                          // Initially the parameter drive is the same as the current drive

		ccp_prompt[2] = 'A' + ccp_cur_drive;                        // Shows the ccp_prompt
		pal_puts((char*)ccp_prompt);

		ram_write(CCP_IN_BUFFER, CMD_LEN);                      // Sets the buffer size to read the command line
		ccp_read_input();

		ccp_blen = ram_read(CCP_IN_BUFFER + 1);                     // Obtains the number of bytes read

		ccp_bdos(CCP_F_DMAOFF, CCP_DEF_DMA);                    // Reset current DMA

		if (ccp_blen) {
			ram_write(CCP_IN_BUFFER + 2 + ccp_blen, 0);             // "Closes" the read buffer with a \0
			ccp_pbuf = CCP_IN_BUFFER + 2;                           // Points ccp_pbuf to the first command character

			while (ram_read(ccp_pbuf) == ' ' && ccp_blen) {     // Skips any leading spaces
				ccp_pbuf++; ccp_blen--;
			}
			if (!ccp_blen)                                  // There were only spaces
				continue;
			if (ram_read(ccp_pbuf) == ';')                  // Found a comment line
				continue;

			ccp_init_fcb(CCP_CMD_FCB);                      // Initializes the command FCB

			ccp_perr = ccp_pbuf;                                // Saves the pointer in case there's an error
			if (ccp_name_to_fcb(CCP_CMD_FCB) > 8) {         // Extracts the command from the buffer
				ccp_cmd_error();                        // Command name cannot be non-unique or have an extension
				continue;
			}

			if (ram_read(CCP_CMD_FCB) && ram_read(CCP_CMD_FCB + 1) == ' ') {    // Command was a simple drive select
				ccp_bdos(CCP_DRV_SET, ram_read(CCP_CMD_FCB) - 1);
				continue;
			}

			ram_write(CCP_DEF_DMA, ccp_blen);                   // Move the command line at this point to 0x0080
			for (i = 0; i < ccp_blen; i++) {
				ram_write(CCP_DEF_DMA + i + 1, ram_read(ccp_pbuf + i));
			}
			ram_write(CCP_DEF_DMA + i + 1, 0);

			while (ram_read(ccp_pbuf) == ' ' && ccp_blen) {     // Skips any leading spaces
				ccp_pbuf++; ccp_blen--;
			}

			ccp_init_fcb(CCP_PAR_FCB);                      // Initializes the parameter FCB
			ccp_name_to_fcb(CCP_PAR_FCB);                       // Loads the next file parameter onto the parameter FCB

			i = 0;                                  // Checks if the command is valid and executes
			switch (ccp_cnum()) {
			case 0:     // DIR
				ccp_dir();          break;
			case 1:     // ERA
				ccp_era();          break;
			case 2:     // TYPE
				i = ccp_type(); break;
			case 3:     // SAVE
				i = ccp_save(); break;
			case 4:     // REN
				ccp_ren();          break;
			case 5:     // USER
				i = ccp_user(); break;
			// Extra commands
			case 6:     // CLS
				pal_clrscr();           break;
			case 7:     // DEL is an alias to ERA
				ccp_era();          break;
			case 8:     // EXIT
				cpu_status = 1;         break;
			case 255:   // It is an external command
				i = ccp_ext();
#ifdef EMULATOR_HAS_LUA
				if (i)
					i = ccp_lua();
#endif
				break;
			default:
				i = 1;          break;
			}
			if (i)
				ccp_cmd_error();
		}
		if (cpu_status == 1 || cpu_status == 2)
			break;
	}
	pal_puts("\r\n");
}

#endif
