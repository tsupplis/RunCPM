#include "defaults.h"
#include "globals.h"
#include "cpu.h"
#include "cpm.h"
#include "ram.h"
#include "disk.h"
#include "pal.h"

/* see main.c for definition */

#define NOP		0x00
#define JP		0xc3
#define CALL	0xcd
#define RET		0xc9
#define INa		0xdb	// Triggers a BIOS call
#define OUTa	0xd3	// Triggers a BDOS call

void _cpm_patch(void) {
	uint16_t i;

	//**********  Patch CP/M page zero into the memory  **********

	/* BIOS entry point */
	_ram_write(0x0000, JP);		/* JP BIOS+3 (warm boot) */
	_ram_write16(0x0001, GLB_BIOS_JUMP_PAGE + 3);

	/* IOBYTE - Points to Console */
	_ram_write(0x0003, 0x3D);

	/* Current drive/user - A:/0 */
	if (_cpu_status != 2)
		_ram_write(0x0004, 0x00);

	/* BDOS entry point (0x0005) */
	_ram_write(0x0005, JP);
	_ram_write16(0x0006, GLB_BDOS_JUMP_PAGE + 0x06);

	//**********  Patch CP/M Version into the memory so the CCP can see it
	_ram_write16(GLB_BDOS_JUMP_PAGE, 0x1600);
	_ram_write16(GLB_BDOS_JUMP_PAGE + 2, 0x0000);
	_ram_write16(GLB_BDOS_JUMP_PAGE + 4, 0x0000);

	// Patches in the BDOS jump destination
	_ram_write(GLB_BDOS_JUMP_PAGE + 6, JP);
	_ram_write16(GLB_BDOS_JUMP_PAGE + 7, GLB_BDOS_PAGE);

	// Patches in the BDOS page content
	_ram_write(GLB_BDOS_PAGE, INa);
	_ram_write(GLB_BDOS_PAGE + 1, 0x00);
	_ram_write(GLB_BDOS_PAGE + 2, RET);

	// Patches in the BIOS jump destinations
	for (i = 0; i < 0x36; i = i + 3) {
		_ram_write(GLB_BIOS_JUMP_PAGE + i, JP);
		_ram_write16(GLB_BIOS_JUMP_PAGE + i + 1, GLB_BIOS_PAGE + i);
	}

	// Patches in the BIOS page content
	for (i = 0; i < 0x36; i = i + 3) {
		_ram_write(GLB_BIOS_PAGE + i, OUTa);
		_ram_write(GLB_BIOS_PAGE + i + 1, i & 0xff);
		_ram_write(GLB_BIOS_PAGE + i + 2, RET);
	}

	//**********  Patch CP/M (fake) Disk Paramater Table after the BDOS call entry  **********
	i = GLB_DPB_ADDR;
	_ram_write(i++, 0x20);		/* spt - Sectors Per Track */
	_ram_write(i++, 0x00);
	_ram_write(i++, 0x04);		/* bsh - Data allocation "Block Shift Factor" */
	_ram_write(i++, 0x0f);		/* blm - Data allocation Block Mask */
	_ram_write(i++, 0x00);		/* exm - Extent Mask */
	_ram_write(i++, 0xff);		/* dsm - Total storage capacity of the disk drive */
	_ram_write(i++, 0x01);
	_ram_write(i++, 0xfe);		/* drm - Number of the last directory entry */
	_ram_write(i++, 0x00);
	_ram_write(i++, 0xF0);		/* al0 */
	_ram_write(i++, 0x00);		/* al1 */
	_ram_write(i++, 0x3f);		/* cks - Check area Size */
	_ram_write(i++, 0x00);
	_ram_write(i++, 0x02);		/* off - Number of system reserved tracks at the beginning of the ( logical ) disk */
	_ram_write(i++, 0x00);

}

#ifdef DEBUG_LOG
uint8_t LogBuffer[128];

void _logRegs(void) {
	uint8_t J, I;
	uint8_t Flags[9] = { 'S','Z','5','H','3','P','N','C' };
	for (J = 0, I = CPU_REG_GET_LOW(_cpu_regs.af); J < 8; J++, I <<= 1) Flags[J] = I & 0x80 ? Flags[J] : '.';
	sprintf((char*)LogBuffer, "  _cpu_regs.bc:%04x _cpu_regs.de:%04x _cpu_regs.hl:%04x _cpu_regs.af:%02x|%s| IX:%04x IY:%04x SP:%04x PC:%04x\n",
		CPU_WORD16(_cpu_regs.bc), CPU_WORD16(_cpu_regs.de), CPU_WORD16(_cpu_regs.hl), CPU_REG_GET_HIGH(_cpu_regs.af), Flags, CPU_WORD16(_cpu_regs.ix), CPU_WORD16(_cpu_regs.iy), CPU_WORD16(_cpu_regs.sp), CPU_WORD16(_cpu_regs.pc)); _sys_logbuffer(LogBuffer);
}

void _logMem(uint16_t address, uint8_t amount)	// Amount = number of 16 bytes lines, so 1 CP/M block = 8, not 128
{
	uint8_t i, m, c, pos;
	uint8_t head = 8;
	uint8_t hexa[] = "0123456789ABCDEF";
	for (i = 0; i < amount; i++) {
		pos = 0;
		for (m = 0; m < head; m++)
			LogBuffer[pos++] = ' ';
		sprintf((char*)LogBuffer, "  %04x: ", address);
		for (m = 0; m < 16; m++) {
			c = _ram_read(address++);
			LogBuffer[pos++] = hexa[c >> 4];
			LogBuffer[pos++] = hexa[c & 0x0f];
			LogBuffer[pos++] = ' ';
			LogBuffer[m + head + 48] = c > 31 && c < 127 ? c : '.';
		}
		pos += 16;
		LogBuffer[pos++] = 0x0a;
		LogBuffer[pos++] = 0x00;
		_sys_logbuffer(LogBuffer);
	}
}

void _logChar(char *txt, uint8_t c) {
	uint8_t asc[2];

	asc[0] = c > 31 && c < 127 ? c : '.';
	asc[1] = 0;
	sprintf((char *)LogBuffer, "        %s = %02xh:%3d (%s)\n", txt, c, c, asc);
	_sys_logbuffer(LogBuffer);
}

void _logBiosIn(uint8_t ch) {
	static const char *BIOSCalls[18] =
	{
		"boot", "wboot", "const", "conin", "conout", "list", "punch/aux", "reader", "home", "seldisk", "settrk", "setsec", "setdma",
		"read", "write", "listst", "sectran", "altwboot"
	};
	int index = ch / 3;
	if (index < 18) {
		sprintf((char *)LogBuffer, "\nBios call: %3d (%s) IN:\n", ch, BIOSCalls[index]); _sys_logbuffer(LogBuffer);
	} else {
		sprintf((char *)LogBuffer, "\nBios call: %3d IN:\n", ch); _sys_logbuffer(LogBuffer);
	}

	_logRegs();
}

void _logBiosOut(uint8_t ch) {
	sprintf((char *)LogBuffer, "              OUT:\n"); _sys_logbuffer(LogBuffer);
	_logRegs();
}

void _logBdosIn(uint8_t ch) {
	uint16_t address = 0;
	uint8_t size = 0;

	static const char *CPMCalls[41] =
	{
		"System Reset", "Console Input", "Console Output", "Reader Input", "Punch Output", "List Output", "Direct I/O", "Get IOByte",
		"Set IOByte", "Print String", "Read Buffered", "Console _cpu_status", "Get Version", "Reset Disk", "Select Disk", "Open File",
		"Close File", "Search First", "Search Next", "Delete File", "Read Sequential", "Write Sequential", "Make File", "Rename File",
		"Get Login Vector", "Get Current Disk", "Set DMA Address", "Get Alloc", "Write Protect Disk", "Get R/O Vector", "Set File Attr", "Get Disk Params",
		"Get/Set User", "Read Random", "Write Random", "Get File Size", "Set Random Record", "Reset Drive", "N/A", "N/A", "Write Random 0 fill"
	};

	if (ch < 41) {
		sprintf((char *)LogBuffer, "\nBdos call: %3d (%s) IN from 0x%04x:\n", ch, CPMCalls[ch], _ram_read16(_cpu_regs.sp)-3); _sys_logbuffer(LogBuffer);
	} else {
		sprintf((char *)LogBuffer, "\nBdos call: %3d IN from 0x%04x:\n", ch, _ram_read16(_cpu_regs.sp)-3); _sys_logbuffer(LogBuffer);
	}
	_logRegs();
	switch (ch) {
	case 2:
	case 4:
	case 5:
	case 6:
		_logChar("E", CPU_REG_GET_LOW(_cpu_regs.de)); break;
	case 9:
	case 10:
		address = _cpu_regs.de; size = 8; break;
	case 15:
	case 16:
	case 17:
	case 18:
	case 19:
	case 22:
	case 23:
	case 30:
	case 35:
	case 36:
		address = _cpu_regs.de; size = 3; break;
	case 20:
	case 21:
	case 33:
	case 34:
	case 40:
		address = _cpu_regs.de; size = 3; _logMem(address, size);
		sprintf((char *)LogBuffer, "\n");  _sys_logbuffer(LogBuffer);
		address = _glb_dma_addr; size = 8; break;
	default:
		break;
	}
	if (size)
		_logMem(address, size);
}

void _logBdosOut(uint8_t ch) {
	uint16_t address = 0;
	uint8_t size = 0;

	sprintf((char *)LogBuffer, "              OUT:\n"); _sys_logbuffer(LogBuffer);
	_logRegs();
	switch (ch) {
	case 1:
	case 3:
	case 6:
		_logChar("A", CPU_REG_GET_HIGH(_cpu_regs.af)); break;
	case 10:
		address = _cpu_regs.de; size = 8; break;
	case 20:
	case 21:
	case 33:
	case 34:
	case 40:
		address = _cpu_regs.de; size = 3; _logMem(address, size);
		sprintf((char *)LogBuffer, "\n");  _sys_logbuffer(LogBuffer);
		address = glb_dma_addr; size = 8; break;
	case 26:
		address = glb_dma_addr; size = 8; break;
	case 35:
	case 36:
		address = _cpu_regs.de; size = 3; break;
	default:
		break;
	}
	if (size)
		_logMem(address, size);
}
#endif

void _cpm_bios(void) {
	uint8_t ch = CPU_REG_GET_LOW(_cpu_regs.pcx);

#ifdef DEBUG_LOG
#ifdef DEBUG_LOG_ONLY
	if (ch == DEBUG_LOG_ONLY)
#endif
		_logBiosIn(ch);
#endif

	switch (ch) {
	case 0x00:
		_cpu_status = 1;			// 0 - BOOT - Ends RunCPM
		break;
	case 0x03:
		_cpu_status = 2;			// 1 - WBOOT - Back to CCP
		break;
	case 0x06:				// 2 - CONST - Console status
		CPU_REG_SET_HIGH(_cpu_regs.af, _chready());
		break;
	case 0x09:				// 3 - CONIN - Console input
		CPU_REG_SET_HIGH(_cpu_regs.af, _getch());
#ifdef DEBUG
		if (CPU_REG_GET_HIGH(_cpu_regs.af) == 4)
			_cpu_debug = 1;
#endif
		break;
	case 0x0C:				// 4 - CONOUT - Console output
		_putcon(CPU_REG_GET_LOW(_cpu_regs.bc));
		break;
	case 0x0F:				// 5 - LIST - List output
		break;
	case 0x12:				// 6 - PUNCH/AUXOUT - Punch output
		break;
	case 0x15:				// 7 - READER - Reader input (0x1a = device not implemented)
		CPU_REG_SET_HIGH(_cpu_regs.af, 0x1a);
		break;
	case 0x18:				// 8 - HOME - Home disk head
		break;
	case 0x1B:				// 9 - SELDSK - Select disk drive
		_cpu_regs.hl = 0x0000;
		break;
	case 0x1E:				// 10 - SETTRK - Set track number
		break;
	case 0x21:				// 11 - SETSEC - Set sector number
		break;
	case 0x24:				// 12 - SETDMA - Set DMA address
		_cpu_regs.hl = _cpu_regs.bc;
		_glb_dma_addr = _cpu_regs.bc;
		break;
	case 0x27:				// 13 - READ - Read selected sector
		CPU_REG_SET_HIGH(_cpu_regs.af, 0x00);
		break;
	case 0x2A:				// 14 - WRITE - Write selected sector
		CPU_REG_SET_HIGH(_cpu_regs.af, 0x00);
		break;
	case 0x2D:				// 15 - LISTST - Get list device status
		CPU_REG_SET_HIGH(_cpu_regs.af, 0x0ff);
		break;
	case 0x30:				// 16 - SECTRAN - Sector translate
		_cpu_regs.hl = _cpu_regs.bc;			// _cpu_regs.hl=_cpu_regs.bc=No translation (1:1)
		break;
	case 0x33:				// This lets programs ending in RET be able to return to internal CCP
		_cpu_status = 3;
		break;
	default:
#ifdef DEBUG	// Show unimplemented BIOS calls only when debugging
		_pal_puts("\r\nUnimplemented BIOS call.\r\n");
		_pal_puts("C = 0x");
		_puthex8(ch);
		_pal_puts("\r\n");
#endif
		break;
	}
#ifdef DEBUG_LOG
#ifdef DEBUG_LOG_ONLY
	if (ch == DEBUG_LOG_ONLY)
#endif
		_logBiosOut(ch);
#endif

}

void _cpm_bdos(void) {
	int32_t i, j, c, chr, count;
	uint8_t ch = CPU_REG_GET_LOW(_cpu_regs.bc);

#ifdef DEBUG_LOG
#ifdef DEBUG_LOG_ONLY
	if (ch == DEBUG_LOG_ONLY)
#endif
		_logBdosIn(ch);
#endif

	_cpu_regs.hl = 0x0000;	// _cpu_regs.hl is reset by the BDOS
	CPU_REG_SET_LOW(_cpu_regs.bc, CPU_REG_GET_LOW(_cpu_regs.de)); // C ends up equal to E

	switch (ch) {
		/*
		C = 0 : System reset
		Doesn't return. Reloads CP/M
		*/
	case 0:
		_cpu_status = 2;	// Same as call to "BOOT"
		break;
		/*
		C = 1 : Console input
		Gets a char from the console
		Returns: A=Char
		*/
	case 1:
		_cpu_regs.hl = _getche();
#ifdef DEBUG
		if (_cpu_regs.hl == 4)
			_cpu_debug = 1;
#endif
		break;
		/*
		C = 2 : Console output
		E = Char
		Sends the char in E to the console
		*/
	case 2:
		_putcon(CPU_REG_GET_LOW(_cpu_regs.de));
		break;
		/*
		C = 3 : Auxiliary (Reader) input
		Returns: A=Char
		*/
	case 3:
		_cpu_regs.hl = 0x1a;
		break;
		/*
		C = 4 : Auxiliary (Punch) output
		*/
	case 4:
		break;
		/*
		C = 5 : Printer output
		*/
	case 5:
		break;
		/*
		C = 6 : Direct console IO
		E = 0xFF : Checks for char available and returns it, or 0x00 if none (read)
		E = char : Outputs char (write)
		Returns: A=Char or 0x00 (on read)
		*/
	case 6:
		if (CPU_REG_GET_LOW(_cpu_regs.de) == 0xff) {
			_cpu_regs.hl = _getchNB();
#ifdef DEBUG
			if (_cpu_regs.hl == 4)
				_cpu_debug = 1;
#endif
		} else {
			_putcon(CPU_REG_GET_LOW(_cpu_regs.de));
		}
		break;
		/*
		C = 7 : Get IOBYTE
		Gets the system IOBYTE
		Returns: A = IOBYTE
		*/
	case 7:
		_cpu_regs.hl = _ram_read(0x0003);
		break;
		/*
		C = 8 : Set IOBYTE
		E = IOBYTE
		Sets the system IOBYTE to E
		*/
	case 8:
		_ram_write(0x0003, CPU_REG_GET_LOW(_cpu_regs.de));
		break;
		/*
		C = 9 : Output string
		_cpu_regs.de = Address of string
		Sends the $ terminated string pointed by (_cpu_regs.de) to the screen
		*/
	case 9:
		while ((ch = _ram_read(_cpu_regs.de++)) != '$')
			_putcon(ch);
		break;
		/*
		C = 10 (0Ah) : Buffered input
		_cpu_regs.de = Address of buffer
		Reads (_cpu_regs.de) bytes from the console
		Returns: A = Number os chars read
		_cpu_regs.de) = First char
		*/
	case 10:
		i = CPU_WORD16(_cpu_regs.de);
		c = _ram_read(i);	// Gets the number of characters to read
		i++;	// Points to the number read
		count = 0;
		while (c)	// Very simplistic line input
		{
			chr = _getch();
			if (chr == 3 && count == 0) {						// ^C
				_pal_puts("^C");
				_cpu_status = 2;
				break;
			}
#ifdef DEBUG
			if (chr == 4)										// ^D
				_cpu_debug = 1;
#endif
			if (chr == 5)										// ^E
				_pal_puts("\r\n");
			if ((chr == 0x08 || chr == 0x7F) && count > 0) {	// ^H and DEL
				_pal_puts("\b \b");
				count--;
				continue;
			}
			if (chr == 0x0A || chr == 0x0D)						// ^J and ^M
				break;
			if (chr == 18) {									// ^R
				_pal_puts("#\r\n  ");
				for (j = 1; j <= count; j++)
					_putcon(_ram_read(i + j));
			}
			if (chr == 21) {									// ^U
				_pal_puts("#\r\n  ");
				i = CPU_WORD16(_cpu_regs.de);
				c = _ram_read(i);
				i++;
				count = 0;
			}
			if (chr == 24) {									// ^X
				for (j = 0; j < count; j++)
					_pal_puts("\b \b");
				i = CPU_WORD16(_cpu_regs.de);
				c = _ram_read(i);
				i++;
				count = 0;
			}
			if (chr < 0x20 || chr > 0x7E)						// Invalid character
				continue;
			_putcon(chr);
			count++; _ram_write(i + count, chr);
			if (count == c)
				break;
		}
		_ram_write(i, count);	// Saves the number or characters read
		_putcon('\r');	// Gives a visual feedback that read ended
		break;
		/*
		C = 11 (0Bh) : Get console status
		Returns: A=0x00 or 0xFF
		*/
	case 11:
		_cpu_regs.hl = _chready();
		break;
		/*
		C = 12 (0Ch) : Get version number
		Returns: B=H=system type, A=L=version number
		*/
	case 12:
		_cpu_regs.hl = 0x22;
		break;
		/*
		C = 13 (0Dh) : Reset disk system
		*/
	case 13:
		_glb_ro_vector = 0;		// Make all drives R/W
		_glb_login_vector = 0;
		_glb_dma_addr = 0x0080;
		_glb_c_drive = 0;			// userCode remains unchanged
		_cpu_regs.hl = _disk_check_sub();	// Checks if there's a $$$.SUB on the boot disk
		break;
		/*
		C = 14 (0Eh) : Select Disk
		Returns: A=0x00 or 0xFF
		*/
	case 14:
		_glb_o_drive =_glb_c_drive;
		_glb_c_drive = CPU_REG_GET_LOW(_cpu_regs.de);
		_cpu_regs.hl = _disk_select_disk(CPU_REG_GET_LOW(_cpu_regs.de) + 1);	// +1 here is to allow _disk_select_disk to be used directly by disk.h as well
		if (!_cpu_regs.hl)
			_glb_o_drive = _glb_c_drive;
		break;
		/*
		C = 15 (0Fh) : Open file
		Returns: A=0x00 or 0xFF
		*/
	case 15:
		_cpu_regs.hl = _disk_open_file(_cpu_regs.de);
		break;
		/*
		C = 16 (10h) : Close file
		*/
	case 16:
		_cpu_regs.hl = _disk_close_file(_cpu_regs.de);
		break;
		/*
		C = 17 (11h) : Search for first
		*/
	case 17:
		_cpu_regs.hl = _disk_search_first(_cpu_regs.de, 1);	// 1 = Creates a fake dir entry when finding the file
		break;
		/*
		C = 18 (12h) : Search for next
		*/
	case 18:
		_cpu_regs.hl = _disk_search_next(_cpu_regs.de, 1);		// 1 = Creates a fake dir entry when finding the file
		break;
		/*
		C = 19 (13h) : Delete file
		*/
	case 19:
		_cpu_regs.hl = _disk_delete_file(_cpu_regs.de);
		break;
		/*
		C = 20 (14h) : Read sequential
		*/
	case 20:
		_cpu_regs.hl = _disk_read_seq(_cpu_regs.de);
		break;
		/*
		C = 21 (15h) : Write sequential
		*/
	case 21:
		_cpu_regs.hl = _disk_write_seq(_cpu_regs.de);
		break;
		/*
		C = 22 (16h) : Make file
		*/
	case 22:
		_cpu_regs.hl = _disk_make_file(_cpu_regs.de);
		break;
		/*
		C = 23 (17h) : Rename file
		*/
	case 23:
		_cpu_regs.hl = _disk_rename_file(_cpu_regs.de);
		break;
		/*
		C = 24 (18h) : Return log-in vector (active drive map)
		*/
	case 24:
		_cpu_regs.hl = _glb_login_vector;	// (todo) improve this
		break;
		/*
		C = 25 (19h) : Return current disk
		*/
	case 25:
		_cpu_regs.hl = _glb_c_drive;
		break;
		/*
		C = 26 (1Ah) : Set DMA address
		*/
	case 26:
		_glb_dma_addr = _cpu_regs.de;
		break;
		/*
		C = 27 (1Bh) : Get ADDR(Alloc)
		*/
	case 27:
		_cpu_regs.hl = GLB_SCB_ADDR;
		break;
		/*
		C = 28 (1Ch) : Write protect current disk
		*/
	case 28:
		_glb_ro_vector = _glb_ro_vector | (1 << _glb_c_drive);
		break;
		/*
		C = 29 (1Dh) : Get R/O vector
		*/
	case 29:
		_cpu_regs.hl = _glb_ro_vector;
		break;
		/********** (todo) Function 30: Set file attributes **********/
		/*
		C = 31 (1Fh) : Get ADDR(Disk Parms)
		*/
	case 31:
		_cpu_regs.hl = GLB_DPB_ADDR;
		break;
		/*
		C = 32 (20h) : Get/Set user code
		*/
	case 32:
		if (CPU_REG_GET_LOW(_cpu_regs.de) == 0xFF) {
			_cpu_regs.hl = _glb_user_code;
		} else {
			_disk_set_user(_cpu_regs.de);
		}
		break;
		/*
		C = 33 (21h) : Read random
		*/
	case 33:
		_cpu_regs.hl = _disk_read_rand(_cpu_regs.de);
		break;
		/*
		C = 34 (22h) : Write random
		*/
	case 34:
		_cpu_regs.hl = _disk_write_rand(_cpu_regs.de);
		break;
		/*
		C = 35 (23h) : Compute file size
		*/
	case 35:
		_cpu_regs.hl = _disk_get_file_size(_cpu_regs.de);
		break;
		/*
		C = 36 (24h) : Set random record
		*/
	case 36:
		_cpu_regs.hl = _disk_set_random(_cpu_regs.de);
		break;
		/*
		C = 37 (25h) : Reset drive
		*/
	case 37:
		break;
		/********** Function 38: Not supported by CP/M 2.2 **********/
		/********** Function 39: Not supported by CP/M 2.2 **********/
		/********** (todo) Function 40: Write random with zero fill **********/
		/*
		C = 40 (28h) : Write random with zero fill (we have no disk blocks, so just write random)
		*/
	case 40:
		_cpu_regs.hl = _disk_write_rand(_cpu_regs.de);
		break;
#ifdef ARDUINO
		/*
		C = 220 (DCh) : PinMode
		*/
	case 220:
		pinMode(CPU_REG_GET_HIGH(_cpu_regs.de), CPU_REG_GET_LOW(_cpu_regs.de));
		break;
		/*
		C = 221 (DDh) : DigitalRead
		*/
	case 221:
		_cpu_regs.hl = digitalRead(CPU_REG_GET_HIGH(_cpu_regs.de));
		break;
		/*
		C = 222 (DEh) : DigitalWrite
		*/
	case 222:
		digitalWrite(CPU_REG_GET_HIGH(_cpu_regs.de), CPU_REG_GET_LOW(_cpu_regs.de));
		break;
		/*
		C = 223 (DFh) : AnalogRead
		*/
	case 223:
		_cpu_regs.hl = analogRead(CPU_REG_GET_HIGH(_cpu_regs.de));
		break;
		/*
		C = 224 (E0h) : AnalogWrite
		*/
	case 224:
		analogWrite(CPU_REG_GET_HIGH(_cpu_regs.de), CPU_REG_GET_LOW(_cpu_regs.de));
		break;
#endif
		/*
		C = 250 (FAh) : EMULATOR_HOSTOS
		Returns: A = 0x00 - Windows / 0x01 - Arduino / 0x02 - Posix / 0x03 - Dos
		*/
	case 250:
		_cpu_regs.hl = EMULATOR_HOSTOS;
		break;
		/*
		C = 251 (FBh) : Version
		Returns: A = 0xVv - Version in BCD representation: V.v
		*/
	case 251:
		_cpu_regs.hl = EMULATOR_VERSION_BCD;
		break;
		/*
		C = 252 (FCh) : CCP version
		Returns: A = 0x00-0x04 = DRI|CCPZ|ZCPR2|ZCPR3|Z80CCP / 0xVv = Internal version in BCD: V.v
		*/
	case 252:
		_cpu_regs.hl = GLB_CCP_VERSION;
		break;
		/*
		C = 253 (FDh) : CCP address
		*/
	case 253:
		_cpu_regs.hl = GLB_CCP_ADDR;
		break;
		/*
		Unimplemented calls get listed
		*/
	default:
#ifdef DEBUG	// Show unimplemented BDOS calls only when debugging
		_pal_puts("\r\nUnimplemented BDOS call.\r\n");
		_pal_puts("C = 0x");
		_puthex8(ch);
		_pal_puts("\r\n");
#endif
		break;
	}

	// CP/M BDOS does this before returning
	CPU_REG_SET_HIGH(_cpu_regs.bc, CPU_REG_GET_HIGH(_cpu_regs.hl));
	CPU_REG_SET_HIGH(_cpu_regs.af, CPU_REG_GET_LOW(_cpu_regs.hl));

#ifdef DEBUG_LOG
#ifdef DEBUG_LOG_ONLY
	if (ch == DEBUG_LOG_ONLY)
#endif
		_logBdosOut(ch);
#endif

}
