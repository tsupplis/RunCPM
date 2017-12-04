#include "defaults.h"
#include "globals.h"
#include "cpu.h"
#include "cpm.h"
#include "ram.h"
#include "pal.h"

/* see main.c for definition */

cpu_regs_t _cpu_regs;

int32_t _cpu_status = 0; /* _cpu_status of the CPU 0=running 1=end request 2=back to CCP */
int32_t _cpu_debug = 0;
int32_t _cpu_break = -1;
int32_t _cpu_step = -1;

/*
	Functions needed by the soft CPU implementation
*/
void cpu_out(const uint32_t Port, const uint32_t Value) {
	_cpm_bios();
}

uint32_t cpu_in(const uint32_t Port) {
	_cpm_bdos();
	return(CPU_REG_GET_HIGH(_cpu_regs.af));
}

/* Z80 Custom soft core */

/* simulator stop codes */
#define STOP_HALT       0   /* HALT                                             */
#define STOP_IBKPT      1   /* breakpoint   (program counter)                   */
#define STOP_MEM        2   /* breakpoint   (memory access)                     */
#define STOP_INSTR      3   /* breakpoint   (instruction access)                */
#define STOP_OPCODE     4   /* invalid operation encountered (8080, Z80, 8086)  */

#define ADDRMASK        0xffff

#define FLAG_C  1
#define FLAG_N  2
#define FLAG_P  4
#define FLAG_H  16
#define FLAG_Z  64
#define FLAG_S  128

#define SETFLAG(f,c)    (_cpu_regs.af = (c) ? _cpu_regs.af | FLAG_ ## f : _cpu_regs.af & ~FLAG_ ## f)
#define TSTFLAG(f)      ((_cpu_regs.af & FLAG_ ## f) != 0)

#define PARITY(x)   parityTable[(x) & 0xff]
/*  SET_PV and SET_PV2 are used to provide correct PARITY flag semantics for the 8080 in cases
where the Z80 uses the overflow flag
*/
#define SET_PVS(s)  (((cbits >> 6) ^ (cbits >> 5)) & 4)
#define SET_PV      (SET_PVS(sum))
#define SET_PV2(x)  ((temp == (x)) << 2)

#define POP(x)  {                               \
    register uint32_t y = RAM_PP(_cpu_regs.sp);             \
    x = y + (RAM_PP(_cpu_regs.sp) << 8);                  \
}

#define JPC(cond) {                             \
    if (cond) {                                 \
        _cpu_regs.pc = GET_WORD(_cpu_regs.pc);                      \
    }                                           \
    else {                                      \
        _cpu_regs.pc += 2;                                \
    }                                           \
}

#define CALLC(cond) {                           \
    if (cond) {                                 \
        register uint32_t adrr = GET_WORD(_cpu_regs.pc);    \
        PUSH(_cpu_regs.pc + 2);                           \
        _cpu_regs.pc = adrr;                              \
    }                                           \
    else {                                      \
        _cpu_regs.pc += 2;                                \
    }                                           \
}

/* the following tables precompute some common subexpressions
parityTable[i]          0..255  (number of 1's in i is odd) ? 0 : 4
incTable[i]             0..256! (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0) << 4)
decTable[i]             0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0xf) << 4) | 2
cbitsTable[i]           0..511  (i & 0x10) | ((i >> 8) & 1)
cbitsDup8Table[i]       0..511  (i & 0x10) | ((i >> 8) & 1) | ((i & 0xff) << 8) | (i & 0xa8) |
(((i & 0xff) == 0) << 6)
cbitsDup16Table[i]      0..511  (i & 0x10) | ((i >> 8) & 1) | (i & 0x28)
cbits2Table[i]          0..511  (i & 0x10) | ((i >> 8) & 1) | 2
rrcaTable[i]            0..255  ((i & 1) << 15) | ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1)
rraTable[i]             0..255  ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1)
addTable[i]             0..511  ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6)
subTable[i]             0..255  ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | 2
andTable[i]             0..255  (i << 8) | (i & 0xa8) | ((i == 0) << 6) | 0x10 | parityTable[i]
xororTable[i]           0..255  (i << 8) | (i & 0xa8) | ((i == 0) << 6) | parityTable[i]
rotateShiftTable[i]     0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) | parityTable[i & 0xff]
incZ80Table[i]          0..256! (i & 0xa8) | (((i & 0xff) == 0) << 6) |
(((i & 0xf) == 0) << 4) | ((i == 0x80) << 2)
decZ80Table[i]          0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) |
(((i & 0xf) == 0xf) << 4) | ((i == 0x7f) << 2) | 2
cbitsZ80Table[i]        0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1)
cbitsZ80DupTable[i]     0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) |
((i >> 8) & 1) | (i & 0xa8)
cbits2Z80Table[i]       0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1) | 2
cbits2Z80DupTable[i]    0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1) | 2 |
(i & 0xa8)
negTable[i]             0..255  (((i & 0x0f) != 0) << 4) | ((i == 0x80) << 2) | 2 | (i != 0)
rrdrldTable[i]          0..255  (i << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | parityTable[i]
cpTable[i]              0..255  (i & 0x80) | (((i & 0xff) == 0) << 6)
*/

#include "cpu_tables.h"

/* Memory management    */
uint8_t GET_BYTE(register uint32_t Addr) {
	return _ram_read(Addr & ADDRMASK);
}

void PUT_BYTE(register uint32_t Addr, register uint32_t Value) {
	_ram_write(Addr & ADDRMASK, Value);
}

void PUT_WORD(register uint32_t Addr, register uint32_t Value) {
	_ram_write(Addr & ADDRMASK, Value);
	_ram_write((Addr + 1) & ADDRMASK, Value >> 8);
}

uint16_t GET_WORD(register uint32_t a) {
	return GET_BYTE(a) | (GET_BYTE(a + 1) << 8);
}

#define RAM_MM(a)   GET_BYTE(a--)
#define RAM_PP(a)   GET_BYTE(a++)

#define PUT_BYTE_PP(a,v) PUT_BYTE(a++, v)
#define PUT_BYTE_MM(a,v) PUT_BYTE(a--, v)
#define MM_PUT_BYTE(a,v) PUT_BYTE(--a, v)

#define PUSH(x) do {            \
    MM_PUT_BYTE(_cpu_regs.sp, (x) >> 8);  \
    MM_PUT_BYTE(_cpu_regs.sp, x);         \
} while (0)

/*  Macros for the IN/OUT instructions INI/INIR/IND/INDR/OUTI/OTIR/OUTD/OTDR

Pre condition
temp == value of register B at entry of the instruction
acu == value of transferred byte (IN or OUT)
Post condition
F is set correctly

Use INOUTFLAGS_ZERO(x) for INIR/INDR/OTIR/OTDR where
x == (C + 1) & 0xff for INIR
x == L              for OTIR and OTDR
x == (C - 1) & 0xff for INDR
Use INOUTFLAGS_NONZERO(x) for INI/IND/OUTI/OUTD where
x == (C + 1) & 0xff for INI
x == L              for OUTI and OUTD
x == (C - 1) & 0xff for IND
*/
#define INOUTFLAGS(syxz, x)                                             \
    _cpu_regs.af = (_cpu_regs.af & 0xff00) | (syxz) |               /* SF, YF, XF, ZF   */  \
        ((acu & 0x80) >> 6) |                           /* NF       */  \
        ((acu + (x)) > 0xff ? (FLAG_C | FLAG_H) : 0) |  /* CF, HF   */  \
        parityTable[((acu + (x)) & 7) ^ temp]           /* PF       */

#define INOUTFLAGS_ZERO(x)      INOUTFLAGS(FLAG_Z, x)
#define INOUTFLAGS_NONZERO(x)                                           \
    INOUTFLAGS((CPU_REG_GET_HIGH(_cpu_regs.bc) & 0xa8) | ((CPU_REG_GET_HIGH(_cpu_regs.bc) == 0) << 6), x)

void _cpu_reset(void) {
	_cpu_regs.pcx = 0;
	_cpu_regs.af = 0;
	_cpu_regs.bc = 0;
	_cpu_regs.de = 0;
	_cpu_regs.hl = 0;
	_cpu_regs.ix = 0;
	_cpu_regs.iy = 0;
	_cpu_regs.pc = 0;
	_cpu_regs.sp = 0;
	_cpu_regs.af1 = 0;
	_cpu_regs.bc1 = 0;
	_cpu_regs.de1 = 0;
	_cpu_regs.hl1 = 0;
	_cpu_regs.iff = 0;
	_cpu_regs.ir = 0;
	_cpu_status = 0;
	_cpu_debug = 0;
	_cpu_break = -1;
	_cpu_step = -1;
}

#ifdef DEBUG
void watchprint(uint16_t pos) {
	uint8_t I, J;
	_pal_puts("\r\n");
	_pal_puts("  _cpu_watch : "); _puthex16(_cpu_watch);
	_pal_puts(" = "); _puthex8(_ram_read(_cpu_watch)); _putcon(':'); _puthex8(_ram_read(_cpu_watch + 1));
	_pal_puts(" / ");
	for (J = 0, I = _ram_read(_cpu_watch); J < 8; J++, I <<= 1) _putcon(I & 0x80 ? '1' : '0');
	_putcon(':');
	for (J = 0, I = _ram_read(_cpu_watch + 1); J < 8; J++, I <<= 1) _putcon(I & 0x80 ? '1' : '0');
}

void memdump(uint16_t pos) {
	uint16_t h = pos;
	uint16_t c = pos;
	uint8_t l, i;
	uint8_t ch = pos & 0xff;

	_pal_puts("       ");
	for (i = 0; i < 16; i++) {
		_puthex8(ch++ & 0x0f);
		_pal_puts(" ");
	}
	_pal_puts("\r\n");
	_pal_puts("       ");
	for (i = 0; i < 16; i++)
		_pal_puts("---");
	_pal_puts("\r\n");
	for (l = 0; l < 16; l++) {
		_puthex16(h);
		_pal_puts(" : ");
		for (i = 0; i < 16; i++) {
			_puthex8(_ram_read(h++));
			_pal_puts(" ");
		}
		for (i = 0; i < 16; i++) {
			ch = _ram_read(c++);
			_putcon(ch > 31 && ch < 127 ? ch : '.');
		}
		_pal_puts("\r\n");
	}
}

uint8_t Disasm(uint16_t pos) {
	const char *txt;
	char jr;
	uint8_t ch = _ram_read(pos);
	uint8_t count = 1;
	uint8_t C;

	switch (ch) {
	case 0xCB: pos++; txt = MnemonicsCB[_ram_read(pos++)]; count++; break;
	case 0xED: pos++; txt = MnemonicsED[_ram_read(pos++)]; count++; break;
	case 0xDD: pos++; C = 'X';
		if (_ram_read(pos) != 0xCB) {
			txt = MnemonicsXX[_ram_read(pos++)]; count++;
		} else {
			pos++; txt = MnemonicsXCB[_ram_read(pos++)]; count += 2;
		}
		break;
	case 0xFD: pos++; C = 'Y';
		if (_ram_read(pos) != 0xCB) {
			txt = MnemonicsXX[_ram_read(pos++)]; count++;
		} else {
			pos++; txt = MnemonicsXCB[_ram_read(pos++)]; count += 2;
		}
		break;
	default:   txt = Mnemonics[_ram_read(pos++)];
	}
	while (*txt != 0) {
		switch (*txt) {
		case '*':
			txt += 2;
			count++;
			_puthex8(_ram_read(pos++));
			break;
		case '^':
			txt += 2;
			count++;
			_puthex8(_ram_read(pos++));
			break;
		case '#':
			txt += 2;
			count += 2;
			_puthex8(_ram_read(pos + 1));
			_puthex8(_ram_read(pos));
			break;
		case '@':
			txt += 2;
			count++;
			jr = _ram_read(pos++);
			_puthex16(pos + jr);
			break;
		case '%':
			_putch(C);
			txt++;
			break;
		default:
			_putch(*txt);
			txt++;
		}
	}

	return(count);
}

void Z80debug(void) {
	uint8_t ch = 0;
	uint16_t pos, l;
	static const char Flags[9] = "SZ5H3PNC";
	uint8_t J, I;
	unsigned int bpoint;
	uint8_t loop = 1;

	while (loop) {
		pos = _cpu_regs.pc;
		_pal_puts("\r\n");
		_pal_puts("_cpu_regs.bc:");  _puthex16(_cpu_regs.bc);
		_pal_puts(" DE:"); _puthex16(_cpu_regs.de);
		_pal_puts(" _cpu_regs.hl:"); _puthex16(_cpu_regs.hl);
		_pal_puts(" _cpu_regs.af:"); _puthex16(_cpu_regs.af);
		_pal_puts(" : [");
		for (J = 0, I = CPU_REG_GET_LOW(_cpu_regs.af); J < 8; J++, I <<= 1) _putcon(I & 0x80 ? Flags[J] : '.');
		_pal_puts("]\r\n");
		_pal_puts("_cpu_regs.ix:");  _puthex16(_cpu_regs.ix);
		_pal_puts(" _cpu_regs.iy:"); _puthex16(_cpu_regs.iy);
		_pal_puts(" _cpu_regs.sp:"); _puthex16(_cpu_regs.sp);
		_pal_puts(" _cpu_regs.pc:"); _puthex16(_cpu_regs.pc);
		_pal_puts(" : ");

		Disasm(pos);

		if (_cpu_regs.pc == 0x0005) {
			if (CPU_REG_GET_LOW(_cpu_regs.bc) > 40) {
				_pal_puts(" (Unknown)");
			} else {
				_pal_puts(" (");
				_pal_puts(_cpm_calls[CPU_REG_GET_LOW(_cpu_regs.bc)]);
				_pal_puts(")");
			}
		}

		if (_cpu_watch != -1) {
			watchprint(_cpu_watch);
		}

		_pal_puts("\r\n");
		_pal_puts("Command|? : ");
		ch = _getch();
		if (ch > 21 && ch < 127)
			_putch(ch);
		switch(ch) {
		case 't':
			loop = 0;
			break;
		case 'c':
			loop = 0;
			_pal_puts("\r\n");
			_cpu_debug = 0;
			break;
		case 'b':
			_pal_puts("\r\n"); memdump(_cpu_regs.bc); break;
		case 'd':
			_pal_puts("\r\n"); memdump(_cpu_regs.de); break;
		case 'h':
			_pal_puts("\r\n"); memdump(_cpu_regs.hl); break;
		case 'p':
			_pal_puts("\r\n"); memdump(_cpu_regs.pc & 0xFF00); break;
		case 's':
			_pal_puts("\r\n"); memdump(_cpu_regs.sp & 0xFF00); break;
		case 'x':
			_pal_puts("\r\n"); memdump(_cpu_regs.ix & 0xFF00); break;
		case 'y':
			_pal_puts("\r\n"); memdump(_cpu_regs.iy & 0xFF00); break;
		case 'a':
			_pal_puts("\r\n"); memdump(glb_dma_addr); break;
		case 'l':
			_pal_puts("\r\n");
			I = 16;
			l = pos;
			while (I > 0) {
				_puthex16(l);
				_pal_puts(" : ");
				l += Disasm(l);
				_pal_puts("\r\n");
				I--;
			}
			break;
		case 'B':
			_pal_puts(" Addr: ");
			scanf("%04x", &bpoint);
			_cpu_break = bpoint;
			_pal_puts("Breakpoint set to ");
			_puthex16(_cpu_break);
			_pal_puts("\r\n");
			break;
		case 'C':
			_cpu_break = -1;
			_pal_puts(" Breakpoint cleared\r\n");
			break;
		case 'D':
			_pal_puts(" Addr: ");
			scanf("%04x", &bpoint);
			memdump(bpoint);
			break;
		case 'L':
			_pal_puts(" Addr: ");
			scanf("%04x", &bpoint);
			I = 16;
			l = bpoint;
			while (I > 0) {
				_puthex16(l);
				_pal_puts(" : ");
				l += Disasm(l);
				_pal_puts("\r\n");
				I--;
			}
			break;
		case 'T':
			loop = 0;
			_cpu_step = pos + 3; // This only works correctly with CALL
							// If the called function messes with the stack, this will fail as well.
			_cpu_debug = 0;
			break;
		case 'W':
			_pal_puts(" Addr: ");
			scanf("%04x", &bpoint);
			_cpu_watch = bpoint;
			_pal_puts("_cpu_watch set to ");
			_puthex16(_cpu_watch);
			_pal_puts("\r\n");
			break;
		case '?':
			_pal_puts("\r\n");
			_pal_puts("Lowercase commands:\r\n");
			_pal_puts("  t - traces to the next instruction\r\n");
			_pal_puts("  c - Continue execution\r\n");
			_pal_puts("  b - Dumps memory pointed by (bc)\r\n");
			_pal_puts("  d - Dumps memory pointed by (de)\r\n");
			_pal_puts("  h - Dumps memory pointed by (hl)\r\n");
			_pal_puts("  p - Dumps the page (pc) points to\r\n");
			_pal_puts("  s - Dumps the page (sp) points to\r\n");
			_pal_puts("  x - Dumps the page (ix) points to\r\n");
			_pal_puts("  y - Dumps the page (iy) points to\r\n");
			_pal_puts("  a - Dumps memory pointed by dma address\r\n");
			_pal_puts("  l - Disassembles from current pc\r\n");
			_pal_puts("Uppercase commands:\r\n");
			_pal_puts("  B - Sets breakpoint at address\r\n");
			_pal_puts("  C - Clears breakpoint\r\n");
			_pal_puts("  D - Dumps memory at address\r\n");
			_pal_puts("  L - Disassembles at address\r\n");
			_pal_puts("  T - Steps over a call\r\n");
			_pal_puts("  W - Sets a byte/word watch\r\n");
			break;
		default:
			_pal_puts(" ???\r\n");
		}
	}
}
#endif

void _cpu_run(void) {
	register uint32_t temp = 0;
	register uint32_t acu = 0;
	register uint32_t sum;
	register uint32_t cbits;
	register uint32_t op;
	register uint32_t adr;

	/* main instruction fetch/decode loop */
	while (!_cpu_status) {	/* loop until _cpu_status != 0 */

#ifdef DEBUG
		if (_cpu_regs.pc == _cpu_break) {
			_pal_puts(":BREAK at ");
			_puthex16(_cpu_break);
			_pal_puts(":");
			_cpu_debug = 1;
		}
		if (_cpu_regs.pc == _cpu_step) {
			_cpu_debug = 1;
			_cpu_step = -1;
		}
		if (_cpu_debug)
			Z80debug();
#endif

		_cpu_regs.pcx = _cpu_regs.pc;

		switch (RAM_PP(_cpu_regs.pc)) {

		case 0x00:      /* NOP */
			break;

		case 0x01:      /* LD _cpu_regs.bc,nnnn */
			_cpu_regs.bc = GET_WORD(_cpu_regs.pc);
			_cpu_regs.pc += 2;
			break;

		case 0x02:      /* LD (_cpu_regs.bc),A */
			PUT_BYTE(_cpu_regs.bc, CPU_REG_GET_HIGH(_cpu_regs.af));
			break;

		case 0x03:      /* INC _cpu_regs.bc */
			++_cpu_regs.bc;
			break;

		case 0x04:      /* INC B */
			_cpu_regs.bc += 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
			break;

		case 0x05:      /* DEC B */
			_cpu_regs.bc -= 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
			break;

		case 0x06:      /* LD B,nn */
			CPU_REG_SET_HIGH(_cpu_regs.bc, RAM_PP(_cpu_regs.pc));
			break;

		case 0x07:      /* RLCA */
			_cpu_regs.af = ((_cpu_regs.af >> 7) & 0x0128) | ((_cpu_regs.af << 1) & ~0x1ff) |
				(_cpu_regs.af & 0xc4) | ((_cpu_regs.af >> 15) & 1);
			break;

		case 0x08:      /* EX _cpu_regs.af,_cpu_regs.af' */
			temp = _cpu_regs.af;
			_cpu_regs.af = _cpu_regs.af1;
			_cpu_regs.af1 = temp;
			break;

		case 0x09:      /* ADD _cpu_regs.hl,_cpu_regs.bc */
			_cpu_regs.hl &= ADDRMASK;
			_cpu_regs.bc &= ADDRMASK;
			sum = _cpu_regs.hl + _cpu_regs.bc;
			_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.hl ^ _cpu_regs.bc ^ sum) >> 8];
			_cpu_regs.hl = sum;
			break;

		case 0x0a:      /* LD A,(_cpu_regs.bc) */
			CPU_REG_SET_HIGH(_cpu_regs.af, GET_BYTE(_cpu_regs.bc));
			break;

		case 0x0b:      /* DEC _cpu_regs.bc */
			--_cpu_regs.bc;
			break;

		case 0x0c:      /* INC C */
			temp = CPU_REG_GET_LOW(_cpu_regs.bc) + 1;
			CPU_REG_SET_LOW(_cpu_regs.bc, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80);
			break;

		case 0x0d:      /* DEC C */
			temp = CPU_REG_GET_LOW(_cpu_regs.bc) - 1;
			CPU_REG_SET_LOW(_cpu_regs.bc, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
			break;

		case 0x0e:      /* LD C,nn */
			CPU_REG_SET_LOW(_cpu_regs.bc, RAM_PP(_cpu_regs.pc));
			break;

		case 0x0f:      /* RRCA */
			_cpu_regs.af = (_cpu_regs.af & 0xc4) | rrcaTable[CPU_REG_GET_HIGH(_cpu_regs.af)];
			break;

		case 0x10:      /* DJNZ dd */
			if ((_cpu_regs.bc -= 0x100) & 0xff00)
				_cpu_regs.pc += (int8_t)GET_BYTE(_cpu_regs.pc) + 1;
			else
				_cpu_regs.pc++;
			break;

		case 0x11:      /* LD _cpu_regs.de,nnnn */
			_cpu_regs.de = GET_WORD(_cpu_regs.pc);
			_cpu_regs.pc += 2;
			break;

		case 0x12:      /* LD (_cpu_regs.de),A */
			PUT_BYTE(_cpu_regs.de, CPU_REG_GET_HIGH(_cpu_regs.af));
			break;

		case 0x13:      /* INC _cpu_regs.de */
			++_cpu_regs.de;
			break;

		case 0x14:      /* INC D */
			_cpu_regs.de += 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.de);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
			break;

		case 0x15:      /* DEC D */
			_cpu_regs.de -= 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.de);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
			break;

		case 0x16:      /* LD D,nn */
			CPU_REG_SET_HIGH(_cpu_regs.de, RAM_PP(_cpu_regs.pc));
			break;

		case 0x17:      /* RLA */
			_cpu_regs.af = ((_cpu_regs.af << 8) & 0x0100) | ((_cpu_regs.af >> 7) & 0x28) | ((_cpu_regs.af << 1) & ~0x01ff) |
				(_cpu_regs.af & 0xc4) | ((_cpu_regs.af >> 15) & 1);
			break;

		case 0x18:      /* JR dd */
			_cpu_regs.pc += (int8_t)GET_BYTE(_cpu_regs.pc) + 1;
			break;

		case 0x19:      /* ADD _cpu_regs.hl,_cpu_regs.de */
			_cpu_regs.hl &= ADDRMASK;
			_cpu_regs.de &= ADDRMASK;
			sum = _cpu_regs.hl + _cpu_regs.de;
			_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.hl ^ _cpu_regs.de ^ sum) >> 8];
			_cpu_regs.hl = sum;
			break;

		case 0x1a:      /* LD A,(_cpu_regs.de) */
			CPU_REG_SET_HIGH(_cpu_regs.af, GET_BYTE(_cpu_regs.de));
			break;

		case 0x1b:      /* DEC _cpu_regs.de */
			--_cpu_regs.de;
			break;

		case 0x1c:      /* INC E */
			temp = CPU_REG_GET_LOW(_cpu_regs.de) + 1;
			CPU_REG_SET_LOW(_cpu_regs.de, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80);
			break;

		case 0x1d:      /* DEC E */
			temp = CPU_REG_GET_LOW(_cpu_regs.de) - 1;
			CPU_REG_SET_LOW(_cpu_regs.de, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
			break;

		case 0x1e:      /* LD E,nn */
			CPU_REG_SET_LOW(_cpu_regs.de, RAM_PP(_cpu_regs.pc));
			break;

		case 0x1f:      /* RRA */
			_cpu_regs.af = ((_cpu_regs.af & 1) << 15) | (_cpu_regs.af & 0xc4) | rraTable[CPU_REG_GET_HIGH(_cpu_regs.af)];
			break;

		case 0x20:      /* JR NZ,dd */
			if (TSTFLAG(Z))
				_cpu_regs.pc++;
			else
				_cpu_regs.pc += (int8_t)GET_BYTE(_cpu_regs.pc) + 1;
			break;

		case 0x21:      /* LD _cpu_regs.hl,nnnn */
			_cpu_regs.hl = GET_WORD(_cpu_regs.pc);
			_cpu_regs.pc += 2;
			break;

		case 0x22:      /* LD (nnnn),_cpu_regs.hl */
			temp = GET_WORD(_cpu_regs.pc);
			PUT_WORD(temp, _cpu_regs.hl);
			_cpu_regs.pc += 2;
			break;

		case 0x23:      /* INC _cpu_regs.hl */
			++_cpu_regs.hl;
			break;

		case 0x24:      /* INC H */
			_cpu_regs.hl += 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.hl);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
			break;

		case 0x25:      /* DEC H */
			_cpu_regs.hl -= 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.hl);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
			break;

		case 0x26:      /* LD H,nn */
			CPU_REG_SET_HIGH(_cpu_regs.hl, RAM_PP(_cpu_regs.pc));
			break;

		case 0x27:      /* DAA */
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			temp = CPU_LOW_DIGIT(acu);
			cbits = TSTFLAG(C);
			if (TSTFLAG(N)) {   /* last operation was a subtract */
				int hd = cbits || acu > 0x99;
				if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
					if (temp > 5)
						SETFLAG(H, 0);
					acu -= 6;
					acu &= 0xff;
				}
				if (hd)
					acu -= 0x160;   /* adjust high digit */
			} else {          /* last operation was an add */
				if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
					SETFLAG(H, (temp > 9));
					acu += 6;
				}
				if (cbits || ((acu & 0x1f0) > 0x90))
					acu += 0x60;   /* adjust high digit */
			}
			_cpu_regs.af = (_cpu_regs.af & 0x12) | rrdrldTable[acu & 0xff] | ((acu >> 8) & 1) | cbits;
			break;

		case 0x28:      /* JR Z,dd */
			if (TSTFLAG(Z))
				_cpu_regs.pc += (int8_t)GET_BYTE(_cpu_regs.pc) + 1;
			else
				_cpu_regs.pc++;
			break;

		case 0x29:      /* ADD _cpu_regs.hl,_cpu_regs.hl */
			_cpu_regs.hl &= ADDRMASK;
			sum = _cpu_regs.hl + _cpu_regs.hl;
			_cpu_regs.af = (_cpu_regs.af & ~0x3b) | cbitsDup16Table[sum >> 8];
			_cpu_regs.hl = sum;
			break;

		case 0x2a:      /* LD _cpu_regs.hl,(nnnn) */
			temp = GET_WORD(_cpu_regs.pc);
			_cpu_regs.hl = GET_WORD(temp);
			_cpu_regs.pc += 2;
			break;

		case 0x2b:      /* DEC _cpu_regs.hl */
			--_cpu_regs.hl;
			break;

		case 0x2c:      /* INC L */
			temp = CPU_REG_GET_LOW(_cpu_regs.hl) + 1;
			CPU_REG_SET_LOW(_cpu_regs.hl, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80);
			break;

		case 0x2d:      /* DEC L */
			temp = CPU_REG_GET_LOW(_cpu_regs.hl) - 1;
			CPU_REG_SET_LOW(_cpu_regs.hl, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
			break;

		case 0x2e:      /* LD L,nn */
			CPU_REG_SET_LOW(_cpu_regs.hl, RAM_PP(_cpu_regs.pc));
			break;

		case 0x2f:      /* CPL */
			_cpu_regs.af = (~_cpu_regs.af & ~0xff) | (_cpu_regs.af & 0xc5) | ((~_cpu_regs.af >> 8) & 0x28) | 0x12;
			break;

		case 0x30:      /* JR NC,dd */
			if (TSTFLAG(C))
				_cpu_regs.pc++;
			else
				_cpu_regs.pc += (int8_t)GET_BYTE(_cpu_regs.pc) + 1;
			break;

		case 0x31:      /* LD _cpu_regs.sp,nnnn */
			_cpu_regs.sp = GET_WORD(_cpu_regs.pc);
			_cpu_regs.pc += 2;
			break;

		case 0x32:      /* LD (nnnn),A */
			temp = GET_WORD(_cpu_regs.pc);
			PUT_BYTE(temp, CPU_REG_GET_HIGH(_cpu_regs.af));
			_cpu_regs.pc += 2;
			break;

		case 0x33:      /* INC _cpu_regs.sp */
			++_cpu_regs.sp;
			break;

		case 0x34:      /* INC (_cpu_regs.hl) */
			temp = GET_BYTE(_cpu_regs.hl) + 1;
			PUT_BYTE(_cpu_regs.hl, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80);
			break;

		case 0x35:      /* DEC (_cpu_regs.hl) */
			temp = GET_BYTE(_cpu_regs.hl) - 1;
			PUT_BYTE(_cpu_regs.hl, temp);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
			break;

		case 0x36:      /* LD (_cpu_regs.hl),nn */
			PUT_BYTE(_cpu_regs.hl, RAM_PP(_cpu_regs.pc));
			break;

		case 0x37:      /* SCF */
			_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((_cpu_regs.af >> 8) & 0x28) | 1;
			break;

		case 0x38:      /* JR C,dd */
			if (TSTFLAG(C))
				_cpu_regs.pc += (int8_t)GET_BYTE(_cpu_regs.pc) + 1;
			else
				_cpu_regs.pc++;
			break;

		case 0x39:      /* ADD _cpu_regs.hl,_cpu_regs.sp */
			_cpu_regs.hl &= ADDRMASK;
			_cpu_regs.sp &= ADDRMASK;
			sum = _cpu_regs.hl + _cpu_regs.sp;
			_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.hl ^ _cpu_regs.sp ^ sum) >> 8];
			_cpu_regs.hl = sum;
			break;

		case 0x3a:      /* LD A,(nnnn) */
			temp = GET_WORD(_cpu_regs.pc);
			CPU_REG_SET_HIGH(_cpu_regs.af, GET_BYTE(temp));
			_cpu_regs.pc += 2;
			break;

		case 0x3b:      /* DEC _cpu_regs.sp */
			--_cpu_regs.sp;
			break;

		case 0x3c:      /* INC A */
			_cpu_regs.af += 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.af);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
			break;

		case 0x3d:      /* DEC A */
			_cpu_regs.af -= 0x100;
			temp = CPU_REG_GET_HIGH(_cpu_regs.af);
			_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
			break;

		case 0x3e:      /* LD A,nn */
			CPU_REG_SET_HIGH(_cpu_regs.af, RAM_PP(_cpu_regs.pc));
			break;

		case 0x3f:      /* CCF */
			_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((_cpu_regs.af >> 8) & 0x28) | ((_cpu_regs.af & 1) << 4) | (~_cpu_regs.af & 1);
			break;

		case 0x40:      /* LD B,B */
			break;

		case 0x41:      /* LD B,C */
			_cpu_regs.bc = (_cpu_regs.bc & 0xff) | ((_cpu_regs.bc & 0xff) << 8);
			break;

		case 0x42:      /* LD B,D */
			_cpu_regs.bc = (_cpu_regs.bc & 0xff) | (_cpu_regs.de & ~0xff);
			break;

		case 0x43:      /* LD B,E */
			_cpu_regs.bc = (_cpu_regs.bc & 0xff) | ((_cpu_regs.de & 0xff) << 8);
			break;

		case 0x44:      /* LD B,H */
			_cpu_regs.bc = (_cpu_regs.bc & 0xff) | (_cpu_regs.hl & ~0xff);
			break;

		case 0x45:      /* LD B,L */
			_cpu_regs.bc = (_cpu_regs.bc & 0xff) | ((_cpu_regs.hl & 0xff) << 8);
			break;

		case 0x46:      /* LD B,(_cpu_regs.hl) */
			CPU_REG_SET_HIGH(_cpu_regs.bc, GET_BYTE(_cpu_regs.hl));
			break;

		case 0x47:      /* LD B,A */
			_cpu_regs.bc = (_cpu_regs.bc & 0xff) | (_cpu_regs.af & ~0xff);
			break;

		case 0x48:      /* LD C,B */
			_cpu_regs.bc = (_cpu_regs.bc & ~0xff) | ((_cpu_regs.bc >> 8) & 0xff);
			break;

		case 0x49:      /* LD C,C */
			break;

		case 0x4a:      /* LD C,D */
			_cpu_regs.bc = (_cpu_regs.bc & ~0xff) | ((_cpu_regs.de >> 8) & 0xff);
			break;

		case 0x4b:      /* LD C,E */
			_cpu_regs.bc = (_cpu_regs.bc & ~0xff) | (_cpu_regs.de & 0xff);
			break;

		case 0x4c:      /* LD C,H */
			_cpu_regs.bc = (_cpu_regs.bc & ~0xff) | ((_cpu_regs.hl >> 8) & 0xff);
			break;

		case 0x4d:      /* LD C,L */
			_cpu_regs.bc = (_cpu_regs.bc & ~0xff) | (_cpu_regs.hl & 0xff);
			break;

		case 0x4e:      /* LD C,(_cpu_regs.hl) */
			CPU_REG_SET_LOW(_cpu_regs.bc, GET_BYTE(_cpu_regs.hl));
			break;

		case 0x4f:      /* LD C,A */
			_cpu_regs.bc = (_cpu_regs.bc & ~0xff) | ((_cpu_regs.af >> 8) & 0xff);
			break;

		case 0x50:      /* LD D,B */
			_cpu_regs.de = (_cpu_regs.de & 0xff) | (_cpu_regs.bc & ~0xff);
			break;

		case 0x51:      /* LD D,C */
			_cpu_regs.de = (_cpu_regs.de & 0xff) | ((_cpu_regs.bc & 0xff) << 8);
			break;

		case 0x52:      /* LD D,D */
			break;

		case 0x53:      /* LD D,E */
			_cpu_regs.de = (_cpu_regs.de & 0xff) | ((_cpu_regs.de & 0xff) << 8);
			break;

		case 0x54:      /* LD D,H */
			_cpu_regs.de = (_cpu_regs.de & 0xff) | (_cpu_regs.hl & ~0xff);
			break;

		case 0x55:      /* LD D,L */
			_cpu_regs.de = (_cpu_regs.de & 0xff) | ((_cpu_regs.hl & 0xff) << 8);
			break;

		case 0x56:      /* LD D,(_cpu_regs.hl) */
			CPU_REG_SET_HIGH(_cpu_regs.de, GET_BYTE(_cpu_regs.hl));
			break;

		case 0x57:      /* LD D,A */
			_cpu_regs.de = (_cpu_regs.de & 0xff) | (_cpu_regs.af & ~0xff);
			break;

		case 0x58:      /* LD E,B */
			_cpu_regs.de = (_cpu_regs.de & ~0xff) | ((_cpu_regs.bc >> 8) & 0xff);
			break;

		case 0x59:      /* LD E,C */
			_cpu_regs.de = (_cpu_regs.de & ~0xff) | (_cpu_regs.bc & 0xff);
			break;

		case 0x5a:      /* LD E,D */
			_cpu_regs.de = (_cpu_regs.de & ~0xff) | ((_cpu_regs.de >> 8) & 0xff);
			break;

		case 0x5b:      /* LD E,E */
			break;

		case 0x5c:      /* LD E,H */
			_cpu_regs.de = (_cpu_regs.de & ~0xff) | ((_cpu_regs.hl >> 8) & 0xff);
			break;

		case 0x5d:      /* LD E,L */
			_cpu_regs.de = (_cpu_regs.de & ~0xff) | (_cpu_regs.hl & 0xff);
			break;

		case 0x5e:      /* LD E,(_cpu_regs.hl) */
			CPU_REG_SET_LOW(_cpu_regs.de, GET_BYTE(_cpu_regs.hl));
			break;

		case 0x5f:      /* LD E,A */
			_cpu_regs.de = (_cpu_regs.de & ~0xff) | ((_cpu_regs.af >> 8) & 0xff);
			break;

		case 0x60:      /* LD H,B */
			_cpu_regs.hl = (_cpu_regs.hl & 0xff) | (_cpu_regs.bc & ~0xff);
			break;

		case 0x61:      /* LD H,C */
			_cpu_regs.hl = (_cpu_regs.hl & 0xff) | ((_cpu_regs.bc & 0xff) << 8);
			break;

		case 0x62:      /* LD H,D */
			_cpu_regs.hl = (_cpu_regs.hl & 0xff) | (_cpu_regs.de & ~0xff);
			break;

		case 0x63:      /* LD H,E */
			_cpu_regs.hl = (_cpu_regs.hl & 0xff) | ((_cpu_regs.de & 0xff) << 8);
			break;

		case 0x64:      /* LD H,H */
			break;

		case 0x65:      /* LD H,L */
			_cpu_regs.hl = (_cpu_regs.hl & 0xff) | ((_cpu_regs.hl & 0xff) << 8);
			break;

		case 0x66:      /* LD H,(_cpu_regs.hl) */
			CPU_REG_SET_HIGH(_cpu_regs.hl, GET_BYTE(_cpu_regs.hl));
			break;

		case 0x67:      /* LD H,A */
			_cpu_regs.hl = (_cpu_regs.hl & 0xff) | (_cpu_regs.af & ~0xff);
			break;

		case 0x68:      /* LD L,B */
			_cpu_regs.hl = (_cpu_regs.hl & ~0xff) | ((_cpu_regs.bc >> 8) & 0xff);
			break;

		case 0x69:      /* LD L,C */
			_cpu_regs.hl = (_cpu_regs.hl & ~0xff) | (_cpu_regs.bc & 0xff);
			break;

		case 0x6a:      /* LD L,D */
			_cpu_regs.hl = (_cpu_regs.hl & ~0xff) | ((_cpu_regs.de >> 8) & 0xff);
			break;

		case 0x6b:      /* LD L,E */
			_cpu_regs.hl = (_cpu_regs.hl & ~0xff) | (_cpu_regs.de & 0xff);
			break;

		case 0x6c:      /* LD L,H */
			_cpu_regs.hl = (_cpu_regs.hl & ~0xff) | ((_cpu_regs.hl >> 8) & 0xff);
			break;

		case 0x6d:      /* LD L,L */
			break;

		case 0x6e:      /* LD L,(_cpu_regs.hl) */
			CPU_REG_SET_LOW(_cpu_regs.hl, GET_BYTE(_cpu_regs.hl));
			break;

		case 0x6f:      /* LD L,A */
			_cpu_regs.hl = (_cpu_regs.hl & ~0xff) | ((_cpu_regs.af >> 8) & 0xff);
			break;

		case 0x70:      /* LD (_cpu_regs.hl),B */
			PUT_BYTE(_cpu_regs.hl, CPU_REG_GET_HIGH(_cpu_regs.bc));
			break;

		case 0x71:      /* LD (_cpu_regs.hl),C */
			PUT_BYTE(_cpu_regs.hl, CPU_REG_GET_LOW(_cpu_regs.bc));
			break;

		case 0x72:      /* LD (_cpu_regs.hl),D */
			PUT_BYTE(_cpu_regs.hl, CPU_REG_GET_HIGH(_cpu_regs.de));
			break;

		case 0x73:      /* LD (_cpu_regs.hl),E */
			PUT_BYTE(_cpu_regs.hl, CPU_REG_GET_LOW(_cpu_regs.de));
			break;

		case 0x74:      /* LD (_cpu_regs.hl),H */
			PUT_BYTE(_cpu_regs.hl, CPU_REG_GET_HIGH(_cpu_regs.hl));
			break;

		case 0x75:      /* LD (_cpu_regs.hl),L */
			PUT_BYTE(_cpu_regs.hl, CPU_REG_GET_LOW(_cpu_regs.hl));
			break;

		case 0x76:      /* HALT */
#ifdef DEBUG
			_pal_puts("\r\n::CPU HALTED::");	// A halt is a good indicator of broken code
			_pal_puts("Press any key...");
			_getch();
#endif
			_cpu_regs.pc--;
			goto end_decode;
			break;

		case 0x77:      /* LD (_cpu_regs.hl),A */
			PUT_BYTE(_cpu_regs.hl, CPU_REG_GET_HIGH(_cpu_regs.af));
			break;

		case 0x78:      /* LD A,B */
			_cpu_regs.af = (_cpu_regs.af & 0xff) | (_cpu_regs.bc & ~0xff);
			break;

		case 0x79:      /* LD A,C */
			_cpu_regs.af = (_cpu_regs.af & 0xff) | ((_cpu_regs.bc & 0xff) << 8);
			break;

		case 0x7a:      /* LD A,D */
			_cpu_regs.af = (_cpu_regs.af & 0xff) | (_cpu_regs.de & ~0xff);
			break;

		case 0x7b:      /* LD A,E */
			_cpu_regs.af = (_cpu_regs.af & 0xff) | ((_cpu_regs.de & 0xff) << 8);
			break;

		case 0x7c:      /* LD A,H */
			_cpu_regs.af = (_cpu_regs.af & 0xff) | (_cpu_regs.hl & ~0xff);
			break;

		case 0x7d:      /* LD A,L */
			_cpu_regs.af = (_cpu_regs.af & 0xff) | ((_cpu_regs.hl & 0xff) << 8);
			break;

		case 0x7e:      /* LD A,(_cpu_regs.hl) */
			CPU_REG_SET_HIGH(_cpu_regs.af, GET_BYTE(_cpu_regs.hl));
			break;

		case 0x7f:      /* LD A,A */
			break;

		case 0x80:      /* ADD A,B */
			temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x81:      /* ADD A,C */
			temp = CPU_REG_GET_LOW(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x82:      /* ADD A,D */
			temp = CPU_REG_GET_HIGH(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x83:      /* ADD A,E */
			temp = CPU_REG_GET_LOW(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x84:      /* ADD A,H */
			temp = CPU_REG_GET_HIGH(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x85:      /* ADD A,L */
			temp = CPU_REG_GET_LOW(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x86:      /* ADD A,(_cpu_regs.hl) */
			temp = GET_BYTE(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x87:      /* ADD A,A */
			cbits = 2 * CPU_REG_GET_HIGH(_cpu_regs.af);
			_cpu_regs.af = cbitsDup8Table[cbits] | (SET_PVS(cbits));
			break;

		case 0x88:      /* ADC A,B */
			temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x89:      /* ADC A,C */
			temp = CPU_REG_GET_LOW(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x8a:      /* ADC A,D */
			temp = CPU_REG_GET_HIGH(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x8b:      /* ADC A,E */
			temp = CPU_REG_GET_LOW(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x8c:      /* ADC A,H */
			temp = CPU_REG_GET_HIGH(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x8d:      /* ADC A,L */
			temp = CPU_REG_GET_LOW(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x8e:      /* ADC A,(_cpu_regs.hl) */
			temp = GET_BYTE(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0x8f:      /* ADC A,A */
			cbits = 2 * CPU_REG_GET_HIGH(_cpu_regs.af) + TSTFLAG(C);
			_cpu_regs.af = cbitsDup8Table[cbits] | (SET_PVS(cbits));
			break;

		case 0x90:      /* SUB B */
			temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x91:      /* SUB C */
			temp = CPU_REG_GET_LOW(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x92:      /* SUB D */
			temp = CPU_REG_GET_HIGH(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x93:      /* SUB E */
			temp = CPU_REG_GET_LOW(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x94:      /* SUB H */
			temp = CPU_REG_GET_HIGH(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x95:      /* SUB L */
			temp = CPU_REG_GET_LOW(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x96:      /* SUB (_cpu_regs.hl) */
			temp = GET_BYTE(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x97:      /* SUB A */
			_cpu_regs.af = 0x42;
			break;

		case 0x98:      /* SBC A,B */
			temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x99:      /* SBC A,C */
			temp = CPU_REG_GET_LOW(_cpu_regs.bc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x9a:      /* SBC A,D */
			temp = CPU_REG_GET_HIGH(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x9b:      /* SBC A,E */
			temp = CPU_REG_GET_LOW(_cpu_regs.de);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x9c:      /* SBC A,H */
			temp = CPU_REG_GET_HIGH(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x9d:      /* SBC A,L */
			temp = CPU_REG_GET_LOW(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x9e:      /* SBC A,(_cpu_regs.hl) */
			temp = GET_BYTE(_cpu_regs.hl);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0x9f:      /* SBC A,A */
			cbits = -TSTFLAG(C);
			_cpu_regs.af = subTable[cbits & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PVS(cbits));
			break;

		case 0xa0:      /* AND B */
			_cpu_regs.af = andTable[((_cpu_regs.af & _cpu_regs.bc) >> 8) & 0xff];
			break;

		case 0xa1:      /* AND C */
			_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & _cpu_regs.bc) & 0xff];
			break;

		case 0xa2:      /* AND D */
			_cpu_regs.af = andTable[((_cpu_regs.af & _cpu_regs.de) >> 8) & 0xff];
			break;

		case 0xa3:      /* AND E */
			_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & _cpu_regs.de) & 0xff];
			break;

		case 0xa4:      /* AND H */
			_cpu_regs.af = andTable[((_cpu_regs.af & _cpu_regs.hl) >> 8) & 0xff];
			break;

		case 0xa5:      /* AND L */
			_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & _cpu_regs.hl) & 0xff];
			break;

		case 0xa6:      /* AND (_cpu_regs.hl) */
			_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & GET_BYTE(_cpu_regs.hl)) & 0xff];
			break;

		case 0xa7:      /* AND A */
			_cpu_regs.af = andTable[(_cpu_regs.af >> 8) & 0xff];
			break;

		case 0xa8:      /* XOR B */
			_cpu_regs.af = xororTable[((_cpu_regs.af ^ _cpu_regs.bc) >> 8) & 0xff];
			break;

		case 0xa9:      /* XOR C */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ _cpu_regs.bc) & 0xff];
			break;

		case 0xaa:      /* XOR D */
			_cpu_regs.af = xororTable[((_cpu_regs.af ^ _cpu_regs.de) >> 8) & 0xff];
			break;

		case 0xab:      /* XOR E */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ _cpu_regs.de) & 0xff];
			break;

		case 0xac:      /* XOR H */
			_cpu_regs.af = xororTable[((_cpu_regs.af ^ _cpu_regs.hl) >> 8) & 0xff];
			break;

		case 0xad:      /* XOR L */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ _cpu_regs.hl) & 0xff];
			break;

		case 0xae:      /* XOR (_cpu_regs.hl) */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ GET_BYTE(_cpu_regs.hl)) & 0xff];
			break;

		case 0xaf:      /* XOR A */
			_cpu_regs.af = 0x44;
			break;

		case 0xb0:      /* OR B */
			_cpu_regs.af = xororTable[((_cpu_regs.af | _cpu_regs.bc) >> 8) & 0xff];
			break;

		case 0xb1:      /* OR C */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | _cpu_regs.bc) & 0xff];
			break;

		case 0xb2:      /* OR D */
			_cpu_regs.af = xororTable[((_cpu_regs.af | _cpu_regs.de) >> 8) & 0xff];
			break;

		case 0xb3:      /* OR E */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | _cpu_regs.de) & 0xff];
			break;

		case 0xb4:      /* OR H */
			_cpu_regs.af = xororTable[((_cpu_regs.af | _cpu_regs.hl) >> 8) & 0xff];
			break;

		case 0xb5:      /* OR L */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | _cpu_regs.hl) & 0xff];
			break;

		case 0xb6:      /* OR (_cpu_regs.hl) */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | GET_BYTE(_cpu_regs.hl)) & 0xff];
			break;

		case 0xb7:      /* OR A */
			_cpu_regs.af = xororTable[(_cpu_regs.af >> 8) & 0xff];
			break;

		case 0xb8:      /* CP B */
			temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xb9:      /* CP C */
			temp = CPU_REG_GET_LOW(_cpu_regs.bc);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xba:      /* CP D */
			temp = CPU_REG_GET_HIGH(_cpu_regs.de);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xbb:      /* CP E */
			temp = CPU_REG_GET_LOW(_cpu_regs.de);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xbc:      /* CP H */
			temp = CPU_REG_GET_HIGH(_cpu_regs.hl);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xbd:      /* CP L */
			temp = CPU_REG_GET_LOW(_cpu_regs.hl);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xbe:      /* CP (_cpu_regs.hl) */
			temp = GET_BYTE(_cpu_regs.hl);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xbf:      /* CP A */
			CPU_REG_SET_LOW(_cpu_regs.af, (CPU_REG_GET_HIGH(_cpu_regs.af) & 0x28) | 0x42);
			break;

		case 0xc0:      /* RET NZ */
			if (!(TSTFLAG(Z)))
				POP(_cpu_regs.pc);
			break;

		case 0xc1:      /* POP _cpu_regs.bc */
			POP(_cpu_regs.bc);
			break;

		case 0xc2:      /* JP NZ,nnnn */
			JPC(!TSTFLAG(Z));
			break;

		case 0xc3:      /* JP nnnn */
			JPC(1);
			break;

		case 0xc4:      /* CALL NZ,nnnn */
			CALLC(!TSTFLAG(Z));
			break;

		case 0xc5:      /* PUSH _cpu_regs.bc */
			PUSH(_cpu_regs.bc);
			break;

		case 0xc6:      /* ADD A,nn */
			temp = RAM_PP(_cpu_regs.pc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0xc7:      /* RST 0 */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 0;
			break;

		case 0xc8:      /* RET Z */
			if (TSTFLAG(Z))
				POP(_cpu_regs.pc);
			break;

		case 0xc9:      /* RET */
			POP(_cpu_regs.pc);
			break;

		case 0xca:      /* JP Z,nnnn */
			JPC(TSTFLAG(Z));
			break;

		case 0xcb:      /* CB prefix */
			adr = _cpu_regs.hl;
			switch ((op = GET_BYTE(_cpu_regs.pc)) & 7) {

			case 0:
				++_cpu_regs.pc;
				acu = CPU_REG_GET_HIGH(_cpu_regs.bc);
				break;

			case 1:
				++_cpu_regs.pc;
				acu = CPU_REG_GET_LOW(_cpu_regs.bc);
				break;

			case 2:
				++_cpu_regs.pc;
				acu = CPU_REG_GET_HIGH(_cpu_regs.de);
				break;

			case 3:
				++_cpu_regs.pc;
				acu = CPU_REG_GET_LOW(_cpu_regs.de);
				break;

			case 4:
				++_cpu_regs.pc;
				acu = CPU_REG_GET_HIGH(_cpu_regs.hl);
				break;

			case 5:
				++_cpu_regs.pc;
				acu = CPU_REG_GET_LOW(_cpu_regs.hl);
				break;

			case 6:
				++_cpu_regs.pc;
				acu = GET_BYTE(adr);
				break;

			case 7:
				++_cpu_regs.pc;
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				break;
			}
			switch (op & 0xc0) {

			case 0x00:  /* shift/rotate */
				switch (op & 0x38) {

				case 0x00:/* RLC */
					temp = (acu << 1) | (acu >> 7);
					cbits = temp & 1;
					goto cbshflg1;

				case 0x08:/* RRC */
					temp = (acu >> 1) | (acu << 7);
					cbits = temp & 0x80;
					goto cbshflg1;

				case 0x10:/* RL */
					temp = (acu << 1) | TSTFLAG(C);
					cbits = acu & 0x80;
					goto cbshflg1;

				case 0x18:/* RR */
					temp = (acu >> 1) | (TSTFLAG(C) << 7);
					cbits = acu & 1;
					goto cbshflg1;

				case 0x20:/* SLA */
					temp = acu << 1;
					cbits = acu & 0x80;
					goto cbshflg1;

				case 0x28:/* SRA */
					temp = (acu >> 1) | (acu & 0x80);
					cbits = acu & 1;
					goto cbshflg1;

				case 0x30:/* SLIA */
					temp = (acu << 1) | 1;
					cbits = acu & 0x80;
					goto cbshflg1;

				case 0x38:/* SRL */
					temp = acu >> 1;
					cbits = acu & 1;
				cbshflg1:
					_cpu_regs.af = (_cpu_regs.af & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
				}
				break;

			case 0x40:  /* BIT */
				if (acu & (1 << ((op >> 3) & 7)))
					_cpu_regs.af = (_cpu_regs.af & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
				else
					_cpu_regs.af = (_cpu_regs.af & ~0xfe) | 0x54;
				if ((op & 7) != 6)
					_cpu_regs.af |= (acu & 0x28);
				temp = acu;
				break;

			case 0x80:  /* RES */
				temp = acu & ~(1 << ((op >> 3) & 7));
				break;

			case 0xc0:  /* SET */
				temp = acu | (1 << ((op >> 3) & 7));
				break;
			}
			switch (op & 7) {

			case 0:
				CPU_REG_SET_HIGH(_cpu_regs.bc, temp);
				break;

			case 1:
				CPU_REG_SET_LOW(_cpu_regs.bc, temp);
				break;

			case 2:
				CPU_REG_SET_HIGH(_cpu_regs.de, temp);
				break;

			case 3:
				CPU_REG_SET_LOW(_cpu_regs.de, temp);
				break;

			case 4:
				CPU_REG_SET_HIGH(_cpu_regs.hl, temp);
				break;

			case 5:
				CPU_REG_SET_LOW(_cpu_regs.hl, temp);
				break;

			case 6:
				PUT_BYTE(adr, temp);
				break;

			case 7:
				CPU_REG_SET_HIGH(_cpu_regs.af, temp);
				break;
			}
			break;

		case 0xcc:      /* CALL Z,nnnn */
			CALLC(TSTFLAG(Z));
			break;

		case 0xcd:      /* CALL nnnn */
			CALLC(1);
			break;

		case 0xce:      /* ADC A,nn */
			temp = RAM_PP(_cpu_regs.pc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu + temp + TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
			break;

		case 0xcf:      /* RST 8 */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 8;
			break;

		case 0xd0:      /* RET NC */
			if (!(TSTFLAG(C)))
				POP(_cpu_regs.pc);
			break;

		case 0xd1:      /* POP _cpu_regs.de */
			POP(_cpu_regs.de);
			break;

		case 0xd2:      /* JP NC,nnnn */
			JPC(!TSTFLAG(C));
			break;

		case 0xd3:      /* OUT (nn),A */
			cpu_out(RAM_PP(_cpu_regs.pc), CPU_REG_GET_HIGH(_cpu_regs.af));
			break;

		case 0xd4:      /* CALL NC,nnnn */
			CALLC(!TSTFLAG(C));
			break;

		case 0xd5:      /* PUSH _cpu_regs.de */
			PUSH(_cpu_regs.de);
			break;

		case 0xd6:      /* SUB nn */
			temp = RAM_PP(_cpu_regs.pc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0xd7:      /* RST 10H */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 0x10;
			break;

		case 0xd8:      /* RET C */
			if (TSTFLAG(C))
				POP(_cpu_regs.pc);
			break;

		case 0xd9:      /* EXX */
			temp = _cpu_regs.bc;
			_cpu_regs.bc = _cpu_regs.bc1;
			_cpu_regs.bc1 = temp;
			temp = _cpu_regs.de;
			_cpu_regs.de = _cpu_regs.de1;
			_cpu_regs.de1 = temp;
			temp = _cpu_regs.hl;
			_cpu_regs.hl = _cpu_regs.hl1;
			_cpu_regs.hl1 = temp;
			break;

		case 0xda:      /* JP C,nnnn */
			JPC(TSTFLAG(C));
			break;

		case 0xdb:      /* IN A,(nn) */
			CPU_REG_SET_HIGH(_cpu_regs.af, cpu_in(RAM_PP(_cpu_regs.pc)));
			break;

		case 0xdc:      /* CALL C,nnnn */
			CALLC(TSTFLAG(C));
			break;

		case 0xdd:      /* DD prefix */
			switch (RAM_PP(_cpu_regs.pc)) {

			case 0x09:      /* ADD _cpu_regs.ix,_cpu_regs.bc */
				_cpu_regs.ix &= ADDRMASK;
				_cpu_regs.bc &= ADDRMASK;
				sum = _cpu_regs.ix + _cpu_regs.bc;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.ix ^ _cpu_regs.bc ^ sum) >> 8];
				_cpu_regs.ix = sum;
				break;

			case 0x19:      /* ADD _cpu_regs.ix,_cpu_regs.de */
				_cpu_regs.ix &= ADDRMASK;
				_cpu_regs.de &= ADDRMASK;
				sum = _cpu_regs.ix + _cpu_regs.de;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.ix ^ _cpu_regs.de ^ sum) >> 8];
				_cpu_regs.ix = sum;
				break;

			case 0x21:      /* LD _cpu_regs.ix,nnnn */
				_cpu_regs.ix = GET_WORD(_cpu_regs.pc);
				_cpu_regs.pc += 2;
				break;

			case 0x22:      /* LD (nnnn),_cpu_regs.ix */
				temp = GET_WORD(_cpu_regs.pc);
				PUT_WORD(temp, _cpu_regs.ix);
				_cpu_regs.pc += 2;
				break;

			case 0x23:      /* INC _cpu_regs.ix */
				++_cpu_regs.ix;
				break;

			case 0x24:      /* INC IXH */
				_cpu_regs.ix += 0x100;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incZ80Table[CPU_REG_GET_HIGH(_cpu_regs.ix)];
				break;

			case 0x25:      /* DEC IXH */
				_cpu_regs.ix -= 0x100;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decZ80Table[CPU_REG_GET_HIGH(_cpu_regs.ix)];
				break;

			case 0x26:      /* LD IXH,nn */
				CPU_REG_SET_HIGH(_cpu_regs.ix, RAM_PP(_cpu_regs.pc));
				break;

			case 0x29:      /* ADD _cpu_regs.ix,_cpu_regs.ix */
				_cpu_regs.ix &= ADDRMASK;
				sum = _cpu_regs.ix + _cpu_regs.ix;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | cbitsDup16Table[sum >> 8];
				_cpu_regs.ix = sum;
				break;

			case 0x2a:      /* LD _cpu_regs.ix,(nnnn) */
				temp = GET_WORD(_cpu_regs.pc);
				_cpu_regs.ix = GET_WORD(temp);
				_cpu_regs.pc += 2;
				break;

			case 0x2b:      /* DEC _cpu_regs.ix */
				--_cpu_regs.ix;
				break;

			case 0x2c:      /* INC IXL */
				temp = CPU_REG_GET_LOW(_cpu_regs.ix) + 1;
				CPU_REG_SET_LOW(_cpu_regs.ix, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incZ80Table[temp];
				break;

			case 0x2d:      /* DEC IXL */
				temp = CPU_REG_GET_LOW(_cpu_regs.ix) - 1;
				CPU_REG_SET_LOW(_cpu_regs.ix, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
				break;

			case 0x2e:      /* LD IXL,nn */
				CPU_REG_SET_LOW(_cpu_regs.ix, RAM_PP(_cpu_regs.pc));
				break;

			case 0x34:      /* INC (_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr) + 1;
				PUT_BYTE(adr, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incZ80Table[temp];
				break;

			case 0x35:      /* DEC (_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr) - 1;
				PUT_BYTE(adr, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
				break;

			case 0x36:      /* LD (_cpu_regs.ix+dd),nn */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, RAM_PP(_cpu_regs.pc));
				break;

			case 0x39:      /* ADD _cpu_regs.ix,_cpu_regs.sp */
				_cpu_regs.ix &= ADDRMASK;
				_cpu_regs.sp &= ADDRMASK;
				sum = _cpu_regs.ix + _cpu_regs.sp;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.ix ^ _cpu_regs.sp ^ sum) >> 8];
				_cpu_regs.ix = sum;
				break;

			case 0x44:      /* LD B,IXH */
				CPU_REG_SET_HIGH(_cpu_regs.bc, CPU_REG_GET_HIGH(_cpu_regs.ix));
				break;

			case 0x45:      /* LD B,IXL */
				CPU_REG_SET_HIGH(_cpu_regs.bc, CPU_REG_GET_LOW(_cpu_regs.ix));
				break;

			case 0x46:      /* LD B,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.bc, GET_BYTE(adr));
				break;

			case 0x4c:      /* LD C,IXH */
				CPU_REG_SET_LOW(_cpu_regs.bc, CPU_REG_GET_HIGH(_cpu_regs.ix));
				break;

			case 0x4d:      /* LD C,IXL */
				CPU_REG_SET_LOW(_cpu_regs.bc, CPU_REG_GET_LOW(_cpu_regs.ix));
				break;

			case 0x4e:      /* LD C,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_LOW(_cpu_regs.bc, GET_BYTE(adr));
				break;

			case 0x54:      /* LD D,IXH */
				CPU_REG_SET_HIGH(_cpu_regs.de, CPU_REG_GET_HIGH(_cpu_regs.ix));
				break;

			case 0x55:      /* LD D,IXL */
				CPU_REG_SET_HIGH(_cpu_regs.de, CPU_REG_GET_LOW(_cpu_regs.ix));
				break;

			case 0x56:      /* LD D,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.de, GET_BYTE(adr));
				break;

			case 0x5c:      /* LD E,IXH */
				CPU_REG_SET_LOW(_cpu_regs.de, CPU_REG_GET_HIGH(_cpu_regs.ix));
				break;

			case 0x5d:      /* LD E,IXL */
				CPU_REG_SET_LOW(_cpu_regs.de, CPU_REG_GET_LOW(_cpu_regs.ix));
				break;

			case 0x5e:      /* LD E,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_LOW(_cpu_regs.de, GET_BYTE(adr));
				break;

			case 0x60:      /* LD IXH,B */
				CPU_REG_SET_HIGH(_cpu_regs.ix, CPU_REG_GET_HIGH(_cpu_regs.bc));
				break;

			case 0x61:      /* LD IXH,C */
				CPU_REG_SET_HIGH(_cpu_regs.ix, CPU_REG_GET_LOW(_cpu_regs.bc));
				break;

			case 0x62:      /* LD IXH,D */
				CPU_REG_SET_HIGH(_cpu_regs.ix, CPU_REG_GET_HIGH(_cpu_regs.de));
				break;

			case 0x63:      /* LD IXH,E */
				CPU_REG_SET_HIGH(_cpu_regs.ix, CPU_REG_GET_LOW(_cpu_regs.de));
				break;

			case 0x64:      /* LD IXH,IXH */
				break;

			case 0x65:      /* LD IXH,IXL */
				CPU_REG_SET_HIGH(_cpu_regs.ix, CPU_REG_GET_LOW(_cpu_regs.ix));
				break;

			case 0x66:      /* LD H,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.hl, GET_BYTE(adr));
				break;

			case 0x67:      /* LD IXH,A */
				CPU_REG_SET_HIGH(_cpu_regs.ix, CPU_REG_GET_HIGH(_cpu_regs.af));
				break;

			case 0x68:      /* LD IXL,B */
				CPU_REG_SET_LOW(_cpu_regs.ix, CPU_REG_GET_HIGH(_cpu_regs.bc));
				break;

			case 0x69:      /* LD IXL,C */
				CPU_REG_SET_LOW(_cpu_regs.ix, CPU_REG_GET_LOW(_cpu_regs.bc));
				break;

			case 0x6a:      /* LD IXL,D */
				CPU_REG_SET_LOW(_cpu_regs.ix, CPU_REG_GET_HIGH(_cpu_regs.de));
				break;

			case 0x6b:      /* LD IXL,E */
				CPU_REG_SET_LOW(_cpu_regs.ix, CPU_REG_GET_LOW(_cpu_regs.de));
				break;

			case 0x6c:      /* LD IXL,IXH */
				CPU_REG_SET_LOW(_cpu_regs.ix, CPU_REG_GET_HIGH(_cpu_regs.ix));
				break;

			case 0x6d:      /* LD IXL,IXL */
				break;

			case 0x6e:      /* LD L,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_LOW(_cpu_regs.hl, GET_BYTE(adr));
				break;

			case 0x6f:      /* LD IXL,A */
				CPU_REG_SET_LOW(_cpu_regs.ix, CPU_REG_GET_HIGH(_cpu_regs.af));
				break;

			case 0x70:      /* LD (_cpu_regs.ix+dd),B */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.bc));
				break;

			case 0x71:      /* LD (_cpu_regs.ix+dd),C */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_LOW(_cpu_regs.bc));
				break;

			case 0x72:      /* LD (_cpu_regs.ix+dd),D */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.de));
				break;

			case 0x73:      /* LD (_cpu_regs.ix+dd),E */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_LOW(_cpu_regs.de));
				break;

			case 0x74:      /* LD (_cpu_regs.ix+dd),H */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.hl));
				break;

			case 0x75:      /* LD (_cpu_regs.ix+dd),L */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_LOW(_cpu_regs.hl));
				break;

			case 0x77:      /* LD (_cpu_regs.ix+dd),A */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.af));
				break;

			case 0x7c:      /* LD A,IXH */
				CPU_REG_SET_HIGH(_cpu_regs.af, CPU_REG_GET_HIGH(_cpu_regs.ix));
				break;

			case 0x7d:      /* LD A,IXL */
				CPU_REG_SET_HIGH(_cpu_regs.af, CPU_REG_GET_LOW(_cpu_regs.ix));
				break;

			case 0x7e:      /* LD A,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.af, GET_BYTE(adr));
				break;

			case 0x84:      /* ADD A,IXH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.ix);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp;
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x85:      /* ADD A,IXL */
				temp = CPU_REG_GET_LOW(_cpu_regs.ix);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp;
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x86:      /* ADD A,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp;
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x8c:      /* ADC A,IXH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.ix);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp + TSTFLAG(C);
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x8d:      /* ADC A,IXL */
				temp = CPU_REG_GET_LOW(_cpu_regs.ix);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp + TSTFLAG(C);
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x8e:      /* ADC A,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp + TSTFLAG(C);
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x96:      /* SUB (_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0x94:      /* SUB IXH */
				SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

			case 0x9c:      /* SBC A,IXH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.ix);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp - TSTFLAG(C);
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0x95:      /* SUB IXL */
				SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

			case 0x9d:      /* SBC A,IXL */
				temp = CPU_REG_GET_LOW(_cpu_regs.ix);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp - TSTFLAG(C);
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0x9e:      /* SBC A,(_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp - TSTFLAG(C);
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xa4:      /* AND IXH */
				_cpu_regs.af = andTable[((_cpu_regs.af & _cpu_regs.ix) >> 8) & 0xff];
				break;

			case 0xa5:      /* AND IXL */
				_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & _cpu_regs.ix) & 0xff];
				break;

			case 0xa6:      /* AND (_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & GET_BYTE(adr)) & 0xff];
				break;

			case 0xac:      /* XOR IXH */
				_cpu_regs.af = xororTable[((_cpu_regs.af ^ _cpu_regs.ix) >> 8) & 0xff];
				break;

			case 0xad:      /* XOR IXL */
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ _cpu_regs.ix) & 0xff];
				break;

			case 0xae:      /* XOR (_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ GET_BYTE(adr)) & 0xff];
				break;

			case 0xb4:      /* OR IXH */
				_cpu_regs.af = xororTable[((_cpu_regs.af | _cpu_regs.ix) >> 8) & 0xff];
				break;

			case 0xb5:      /* OR IXL */
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | _cpu_regs.ix) & 0xff];
				break;

			case 0xb6:      /* OR (_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | GET_BYTE(adr)) & 0xff];
				break;

			case 0xbc:      /* CP IXH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.ix);
				_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
					cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xbd:      /* CP IXL */
				temp = CPU_REG_GET_LOW(_cpu_regs.ix);
				_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
					cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xbe:      /* CP (_cpu_regs.ix+dd) */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
					cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xcb:      /* CB prefix */
				adr = _cpu_regs.ix + (int8_t)RAM_PP(_cpu_regs.pc);
				switch ((op = GET_BYTE(_cpu_regs.pc)) & 7) {

				case 0:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.bc);
					break;

				case 1:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_LOW(_cpu_regs.bc);
					break;

				case 2:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.de);
					break;

				case 3:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_LOW(_cpu_regs.de);
					break;

				case 4:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.hl);
					break;

				case 5:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_LOW(_cpu_regs.hl);
					break;

				case 6:
					++_cpu_regs.pc;
					acu = GET_BYTE(adr);
					break;

				case 7:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.af);
					break;
				}
				switch (op & 0xc0) {

				case 0x00:  /* shift/rotate */
					switch (op & 0x38) {

					case 0x00:/* RLC */
						temp = (acu << 1) | (acu >> 7);
						cbits = temp & 1;
						goto cbshflg2;

					case 0x08:/* RRC */
						temp = (acu >> 1) | (acu << 7);
						cbits = temp & 0x80;
						goto cbshflg2;

					case 0x10:/* RL */
						temp = (acu << 1) | TSTFLAG(C);
						cbits = acu & 0x80;
						goto cbshflg2;

					case 0x18:/* RR */
						temp = (acu >> 1) | (TSTFLAG(C) << 7);
						cbits = acu & 1;
						goto cbshflg2;

					case 0x20:/* SLA */
						temp = acu << 1;
						cbits = acu & 0x80;
						goto cbshflg2;

					case 0x28:/* SRA */
						temp = (acu >> 1) | (acu & 0x80);
						cbits = acu & 1;
						goto cbshflg2;

					case 0x30:/* SLIA */
						temp = (acu << 1) | 1;
						cbits = acu & 0x80;
						goto cbshflg2;

					case 0x38:/* SRL */
						temp = acu >> 1;
						cbits = acu & 1;
					cbshflg2:
						_cpu_regs.af = (_cpu_regs.af & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
					}
					break;

				case 0x40:  /* BIT */
					if (acu & (1 << ((op >> 3) & 7)))
						_cpu_regs.af = (_cpu_regs.af & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
					else
						_cpu_regs.af = (_cpu_regs.af & ~0xfe) | 0x54;
					if ((op & 7) != 6)
						_cpu_regs.af |= (acu & 0x28);
					temp = acu;
					break;

				case 0x80:  /* RES */
					temp = acu & ~(1 << ((op >> 3) & 7));
					break;

				case 0xc0:  /* SET */
					temp = acu | (1 << ((op >> 3) & 7));
					break;
				}
				switch (op & 7) {

				case 0:
					CPU_REG_SET_HIGH(_cpu_regs.bc, temp);
					break;

				case 1:
					CPU_REG_SET_LOW(_cpu_regs.bc, temp);
					break;

				case 2:
					CPU_REG_SET_HIGH(_cpu_regs.de, temp);
					break;

				case 3:
					CPU_REG_SET_LOW(_cpu_regs.de, temp);
					break;

				case 4:
					CPU_REG_SET_HIGH(_cpu_regs.hl, temp);
					break;

				case 5:
					CPU_REG_SET_LOW(_cpu_regs.hl, temp);
					break;

				case 6:
					PUT_BYTE(adr, temp);
					break;

				case 7:
					CPU_REG_SET_HIGH(_cpu_regs.af, temp);
					break;
				}
				break;

			case 0xe1:      /* POP _cpu_regs.ix */
				POP(_cpu_regs.ix);
				break;

			case 0xe3:      /* EX (_cpu_regs.sp),_cpu_regs.ix */
				temp = _cpu_regs.ix;
				POP(_cpu_regs.ix);
				PUSH(temp);
				break;

			case 0xe5:      /* PUSH _cpu_regs.ix */
				PUSH(_cpu_regs.ix);
				break;

			case 0xe9:      /* JP (_cpu_regs.ix) */
				_cpu_regs.pc = _cpu_regs.ix;
				break;

			case 0xf9:      /* LD _cpu_regs.sp,_cpu_regs.ix */
				_cpu_regs.sp = _cpu_regs.ix;
				break;

			default:                /* ignore DD */
				_cpu_regs.pc--;
			}
			break;

		case 0xde:          /* SBC A,nn */
			temp = RAM_PP(_cpu_regs.pc);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp - TSTFLAG(C);
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
			break;

		case 0xdf:      /* RST 18H */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 0x18;
			break;

		case 0xe0:      /* RET PO */
			if (!(TSTFLAG(P)))
				POP(_cpu_regs.pc);
			break;

		case 0xe1:      /* POP _cpu_regs.hl */
			POP(_cpu_regs.hl);
			break;

		case 0xe2:      /* JP PO,nnnn */
			JPC(!TSTFLAG(P));
			break;

		case 0xe3:      /* EX (_cpu_regs.sp),_cpu_regs.hl */
			temp = _cpu_regs.hl;
			POP(_cpu_regs.hl);
			PUSH(temp);
			break;

		case 0xe4:      /* CALL PO,nnnn */
			CALLC(!TSTFLAG(P));
			break;

		case 0xe5:      /* PUSH _cpu_regs.hl */
			PUSH(_cpu_regs.hl);
			break;

		case 0xe6:      /* AND nn */
			_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & RAM_PP(_cpu_regs.pc)) & 0xff];
			break;

		case 0xe7:      /* RST 20H */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 0x20;
			break;

		case 0xe8:      /* RET PE */
			if (TSTFLAG(P))
				POP(_cpu_regs.pc);
			break;

		case 0xe9:      /* JP (_cpu_regs.hl) */
			_cpu_regs.pc = _cpu_regs.hl;
			break;

		case 0xea:      /* JP PE,nnnn */
			JPC(TSTFLAG(P));
			break;

		case 0xeb:      /* EX _cpu_regs.de,_cpu_regs.hl */
			temp = _cpu_regs.hl;
			_cpu_regs.hl = _cpu_regs.de;
			_cpu_regs.de = temp;
			break;

		case 0xec:      /* CALL PE,nnnn */
			CALLC(TSTFLAG(P));
			break;

		case 0xed:      /* ED prefix */
			switch (RAM_PP(_cpu_regs.pc)) {

			case 0x40:      /* IN B,(C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_HIGH(_cpu_regs.bc, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x41:      /* OUT (C),B */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), CPU_REG_GET_HIGH(_cpu_regs.bc));
				break;

			case 0x42:      /* SBC _cpu_regs.hl,_cpu_regs.bc */
				_cpu_regs.hl &= ADDRMASK;
				_cpu_regs.bc &= ADDRMASK;
				sum = _cpu_regs.hl - _cpu_regs.bc - TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
					cbits2Z80Table[((_cpu_regs.hl ^ _cpu_regs.bc ^ sum) >> 8) & 0x1ff];
				_cpu_regs.hl = sum;
				break;

			case 0x43:      /* LD (nnnn),_cpu_regs.bc */
				temp = GET_WORD(_cpu_regs.pc);
				PUT_WORD(temp, _cpu_regs.bc);
				_cpu_regs.pc += 2;
				break;

			case 0x44:      /* NEG */

			case 0x4C:      /* NEG, unofficial */

			case 0x54:      /* NEG, unofficial */

			case 0x5C:      /* NEG, unofficial */

			case 0x64:      /* NEG, unofficial */

			case 0x6C:      /* NEG, unofficial */

			case 0x74:      /* NEG, unofficial */

			case 0x7C:      /* NEG, unofficial */
				temp = CPU_REG_GET_HIGH(_cpu_regs.af);
				_cpu_regs.af = ((~(_cpu_regs.af & 0xff00) + 1) & 0xff00); /* _cpu_regs.af = (-(_cpu_regs.af & 0xff00) & 0xff00); */
				_cpu_regs.af |= ((_cpu_regs.af >> 8) & 0xa8) | (((_cpu_regs.af & 0xff00) == 0) << 6) | negTable[temp];
				break;

			case 0x45:      /* RETN */

			case 0x55:      /* RETN, unofficial */

			case 0x5D:      /* RETN, unofficial */

			case 0x65:      /* RETN, unofficial */

			case 0x6D:      /* RETN, unofficial */

			case 0x75:      /* RETN, unofficial */

			case 0x7D:      /* RETN, unofficial */
				_cpu_regs.iff |= _cpu_regs.iff >> 1;
				POP(_cpu_regs.pc);
				break;

			case 0x46:      /* IM 0 */
							/* interrupt mode 0 */
				break;

			case 0x47:      /* LD I,A */
				_cpu_regs.ir = (_cpu_regs.ir & 0xff) | (_cpu_regs.af & ~0xff);
				break;

			case 0x48:      /* IN C,(C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_LOW(_cpu_regs.bc, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x49:      /* OUT (C),C */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), CPU_REG_GET_LOW(_cpu_regs.bc));
				break;

			case 0x4a:      /* ADC _cpu_regs.hl,_cpu_regs.bc */
				_cpu_regs.hl &= ADDRMASK;
				_cpu_regs.bc &= ADDRMASK;
				sum = _cpu_regs.hl + _cpu_regs.bc + TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
					cbitsZ80Table[(_cpu_regs.hl ^ _cpu_regs.bc ^ sum) >> 8];
				_cpu_regs.hl = sum;
				break;

			case 0x4b:      /* LD _cpu_regs.bc,(nnnn) */
				temp = GET_WORD(_cpu_regs.pc);
				_cpu_regs.bc = GET_WORD(temp);
				_cpu_regs.pc += 2;
				break;

			case 0x4d:      /* RETI */
				_cpu_regs.iff |= _cpu_regs.iff >> 1;
				POP(_cpu_regs.pc);
				break;

			case 0x4f:      /* LD R,A */
				_cpu_regs.ir = (_cpu_regs.ir & ~0xff) | ((_cpu_regs.af >> 8) & 0xff);
				break;

			case 0x50:      /* IN D,(C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_HIGH(_cpu_regs.de, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x51:      /* OUT (C),D */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), CPU_REG_GET_HIGH(_cpu_regs.de));
				break;

			case 0x52:      /* SBC _cpu_regs.hl,_cpu_regs.de */
				_cpu_regs.hl &= ADDRMASK;
				_cpu_regs.de &= ADDRMASK;
				sum = _cpu_regs.hl - _cpu_regs.de - TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
					cbits2Z80Table[((_cpu_regs.hl ^ _cpu_regs.de ^ sum) >> 8) & 0x1ff];
				_cpu_regs.hl = sum;
				break;

			case 0x53:      /* LD (nnnn),_cpu_regs.de */
				temp = GET_WORD(_cpu_regs.pc);
				PUT_WORD(temp, _cpu_regs.de);
				_cpu_regs.pc += 2;
				break;

			case 0x56:      /* IM 1 */
							/* interrupt mode 1 */
				break;

			case 0x57:      /* LD A,I */
				_cpu_regs.af = (_cpu_regs.af & 0x29) | (_cpu_regs.ir & ~0xff) | ((_cpu_regs.ir >> 8) & 0x80) | (((_cpu_regs.ir & ~0xff) == 0) << 6) | ((_cpu_regs.iff & 2) << 1);
				break;

			case 0x58:      /* IN E,(C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_LOW(_cpu_regs.de, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x59:      /* OUT (C),E */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), CPU_REG_GET_LOW(_cpu_regs.de));
				break;

			case 0x5a:      /* ADC _cpu_regs.hl,_cpu_regs.de */
				_cpu_regs.hl &= ADDRMASK;
				_cpu_regs.de &= ADDRMASK;
				sum = _cpu_regs.hl + _cpu_regs.de + TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
					cbitsZ80Table[(_cpu_regs.hl ^ _cpu_regs.de ^ sum) >> 8];
				_cpu_regs.hl = sum;
				break;

			case 0x5b:      /* LD _cpu_regs.de,(nnnn) */
				temp = GET_WORD(_cpu_regs.pc);
				_cpu_regs.de = GET_WORD(temp);
				_cpu_regs.pc += 2;
				break;

			case 0x5e:      /* IM 2 */
							/* interrupt mode 2 */
				break;

			case 0x5f:      /* LD A,R */
				_cpu_regs.af = (_cpu_regs.af & 0x29) | ((_cpu_regs.ir & 0xff) << 8) | (_cpu_regs.ir & 0x80) |
					(((_cpu_regs.ir & 0xff) == 0) << 6) | ((_cpu_regs.iff & 2) << 1);
				break;

			case 0x60:      /* IN H,(C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_HIGH(_cpu_regs.hl, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x61:      /* OUT (C),H */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), CPU_REG_GET_HIGH(_cpu_regs.hl));
				break;

			case 0x62:      /* SBC _cpu_regs.hl,_cpu_regs.hl */
				_cpu_regs.hl &= ADDRMASK;
				sum = _cpu_regs.hl - _cpu_regs.hl - TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | (((sum & ADDRMASK) == 0) << 6) |
					cbits2Z80DupTable[(sum >> 8) & 0x1ff];
				_cpu_regs.hl = sum;
				break;

			case 0x63:      /* LD (nnnn),_cpu_regs.hl */
				temp = GET_WORD(_cpu_regs.pc);
				PUT_WORD(temp, _cpu_regs.hl);
				_cpu_regs.pc += 2;
				break;

			case 0x67:      /* RRD */
				temp = GET_BYTE(_cpu_regs.hl);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				PUT_BYTE(_cpu_regs.hl, CPU_HIGH_DIGIT(temp) | (CPU_LOW_DIGIT(acu) << 4));
				_cpu_regs.af = rrdrldTable[(acu & 0xf0) | CPU_LOW_DIGIT(temp)] | (_cpu_regs.af & 1);
				break;

			case 0x68:      /* IN L,(C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_LOW(_cpu_regs.hl, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x69:      /* OUT (C),L */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), CPU_REG_GET_LOW(_cpu_regs.hl));
				break;

			case 0x6a:      /* ADC _cpu_regs.hl,_cpu_regs.hl */
				_cpu_regs.hl &= ADDRMASK;
				sum = _cpu_regs.hl + _cpu_regs.hl + TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | (((sum & ADDRMASK) == 0) << 6) |
					cbitsZ80DupTable[sum >> 8];
				_cpu_regs.hl = sum;
				break;

			case 0x6b:      /* LD _cpu_regs.hl,(nnnn) */
				temp = GET_WORD(_cpu_regs.pc);
				_cpu_regs.hl = GET_WORD(temp);
				_cpu_regs.pc += 2;
				break;

			case 0x6f:      /* RLD */
				temp = GET_BYTE(_cpu_regs.hl);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				PUT_BYTE(_cpu_regs.hl, (CPU_LOW_DIGIT(temp) << 4) | CPU_LOW_DIGIT(acu));
				_cpu_regs.af = rrdrldTable[(acu & 0xf0) | CPU_HIGH_DIGIT(temp)] | (_cpu_regs.af & 1);
				break;

			case 0x70:      /* IN (C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_LOW(temp, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x71:      /* OUT (C),0 */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), 0);
				break;

			case 0x72:      /* SBC _cpu_regs.hl,_cpu_regs.sp */
				_cpu_regs.hl &= ADDRMASK;
				_cpu_regs.sp &= ADDRMASK;
				sum = _cpu_regs.hl - _cpu_regs.sp - TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
					cbits2Z80Table[((_cpu_regs.hl ^ _cpu_regs.sp ^ sum) >> 8) & 0x1ff];
				_cpu_regs.hl = sum;
				break;

			case 0x73:      /* LD (nnnn),_cpu_regs.sp */
				temp = GET_WORD(_cpu_regs.pc);
				PUT_WORD(temp, _cpu_regs.sp);
				_cpu_regs.pc += 2;
				break;

			case 0x78:      /* IN A,(C) */
				temp = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				CPU_REG_SET_HIGH(_cpu_regs.af, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
				break;

			case 0x79:      /* OUT (C),A */
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), CPU_REG_GET_HIGH(_cpu_regs.af));
				break;

			case 0x7a:      /* ADC _cpu_regs.hl,_cpu_regs.sp */
				_cpu_regs.hl &= ADDRMASK;
				_cpu_regs.sp &= ADDRMASK;
				sum = _cpu_regs.hl + _cpu_regs.sp + TSTFLAG(C);
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
					cbitsZ80Table[(_cpu_regs.hl ^ _cpu_regs.sp ^ sum) >> 8];
				_cpu_regs.hl = sum;
				break;

			case 0x7b:      /* LD _cpu_regs.sp,(nnnn) */
				temp = GET_WORD(_cpu_regs.pc);
				_cpu_regs.sp = GET_WORD(temp);
				_cpu_regs.pc += 2;
				break;

			case 0xa0:      /* LDI */
				acu = RAM_PP(_cpu_regs.hl);
				PUT_BYTE_PP(_cpu_regs.de, acu);
				acu += CPU_REG_GET_HIGH(_cpu_regs.af);
				_cpu_regs.af = (_cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
					(((--_cpu_regs.bc & ADDRMASK) != 0) << 2);
				break;

			case 0xa1:      /* CPI */
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				temp = RAM_PP(_cpu_regs.hl);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
					(((sum - ((cbits & 16) >> 4)) & 2) << 4) | (cbits & 16) |
					((sum - ((cbits >> 4) & 1)) & 8) |
					((--_cpu_regs.bc & ADDRMASK) != 0) << 2 | 2;
				if ((sum & 15) == 8 && (cbits & 16) != 0)
					_cpu_regs.af &= ~8;
				break;

				/*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
				NF flag A is copy of bit 7 of the value read from or written to an I/O port.
				INI/INIR/IND/INDR use the C flag in stead of the L register. There is a
				catch though, because not the value of C is used, but C + 1 if it's INI/INIR or
				C - 1 if it's IND/INDR. So, first of all INI/INIR:
				HF and CF Both set if ((_cpu_regs.hl) + ((C + 1) & 255) > 255)
				PF The parity of (((_cpu_regs.hl) + ((C + 1) & 255)) & 7) xor B)                      */
			case 0xa2:      /* INI */
				acu = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				PUT_BYTE(_cpu_regs.hl, acu);
				++_cpu_regs.hl;
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				_cpu_regs.bc -= 0x100;
				INOUTFLAGS_NONZERO((CPU_REG_GET_LOW(_cpu_regs.bc) + 1) & 0xff);
				break;

				/*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
				NF flag A is copy of bit 7 of the value read from or written to an I/O port.
				And now the for OUTI/OTIR/OUTD/OTDR instructions. Take state of the L
				after the increment or decrement of _cpu_regs.hl; add the value written to the I/O port
				to; call that k for now. If k > 255, then the CF and HF flags are set. The PF
				flags is set like the parity of k bitwise and'ed with 7, bitwise xor'ed with B.
				HF and CF Both set if ((_cpu_regs.hl) + L > 255)
				PF The parity of ((((_cpu_regs.hl) + L) & 7) xor B)                                       */
			case 0xa3:      /* OUTI */
				acu = GET_BYTE(_cpu_regs.hl);
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), acu);
				++_cpu_regs.hl;
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				_cpu_regs.bc -= 0x100;
				INOUTFLAGS_NONZERO(CPU_REG_GET_LOW(_cpu_regs.hl));
				break;

			case 0xa8:      /* LDD */
				acu = RAM_MM(_cpu_regs.hl);
				PUT_BYTE_MM(_cpu_regs.de, acu);
				acu += CPU_REG_GET_HIGH(_cpu_regs.af);
				_cpu_regs.af = (_cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
					(((--_cpu_regs.bc & ADDRMASK) != 0) << 2);
				break;

			case 0xa9:      /* CPD */
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				temp = RAM_MM(_cpu_regs.hl);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
					(((sum - ((cbits & 16) >> 4)) & 2) << 4) | (cbits & 16) |
					((sum - ((cbits >> 4) & 1)) & 8) |
					((--_cpu_regs.bc & ADDRMASK) != 0) << 2 | 2;
				if ((sum & 15) == 8 && (cbits & 16) != 0)
					_cpu_regs.af &= ~8;
				break;

				/*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
				NF flag A is copy of bit 7 of the value read from or written to an I/O port.
				INI/INIR/IND/INDR use the C flag in stead of the L register. There is a
				catch though, because not the value of C is used, but C + 1 if it's INI/INIR or
				C - 1 if it's IND/INDR. And last IND/INDR:
				HF and CF Both set if ((_cpu_regs.hl) + ((C - 1) & 255) > 255)
				PF The parity of (((_cpu_regs.hl) + ((C - 1) & 255)) & 7) xor B)                      */
			case 0xaa:      /* IND */
				acu = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
				PUT_BYTE(_cpu_regs.hl, acu);
				--_cpu_regs.hl;
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				_cpu_regs.bc -= 0x100;
				INOUTFLAGS_NONZERO((CPU_REG_GET_LOW(_cpu_regs.bc) - 1) & 0xff);
				break;

			case 0xab:      /* OUTD */
				acu = GET_BYTE(_cpu_regs.hl);
				cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), acu);
				--_cpu_regs.hl;
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				_cpu_regs.bc -= 0x100;
				INOUTFLAGS_NONZERO(CPU_REG_GET_LOW(_cpu_regs.hl));
				break;

			case 0xb0:      /* LDIR */
				_cpu_regs.bc &= ADDRMASK;
				if (_cpu_regs.bc == 0)
					_cpu_regs.bc = 0x10000;
				do {
					acu = RAM_PP(_cpu_regs.hl);
					PUT_BYTE_PP(_cpu_regs.de, acu);
				} while (--_cpu_regs.bc);
				acu += CPU_REG_GET_HIGH(_cpu_regs.af);
				_cpu_regs.af = (_cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
				break;

			case 0xb1:      /* CPIR */
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				_cpu_regs.bc &= ADDRMASK;
				if (_cpu_regs.bc == 0)
					_cpu_regs.bc = 0x10000;
				do {
					temp = RAM_PP(_cpu_regs.hl);
					op = --_cpu_regs.bc != 0;
					sum = acu - temp;
				} while (op && sum != 0);
				cbits = acu ^ temp ^ sum;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
					(((sum - ((cbits & 16) >> 4)) & 2) << 4) |
					(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
					op << 2 | 2;
				if ((sum & 15) == 8 && (cbits & 16) != 0)
					_cpu_regs.af &= ~8;
				break;

			case 0xb2:      /* INIR */
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				if (temp == 0)
					temp = 0x100;
				do {
					acu = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
					PUT_BYTE(_cpu_regs.hl, acu);
					++_cpu_regs.hl;
				} while (--temp);
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				CPU_REG_SET_HIGH(_cpu_regs.bc, 0);
				INOUTFLAGS_ZERO((CPU_REG_GET_LOW(_cpu_regs.bc) + 1) & 0xff);
				break;

			case 0xb3:      /* OTIR */
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				if (temp == 0)
					temp = 0x100;
				do {
					acu = GET_BYTE(_cpu_regs.hl);
					cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), acu);
					++_cpu_regs.hl;
				} while (--temp);
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				CPU_REG_SET_HIGH(_cpu_regs.bc, 0);
				INOUTFLAGS_ZERO(CPU_REG_GET_LOW(_cpu_regs.hl));
				break;

			case 0xb8:      /* LDDR */
				_cpu_regs.bc &= ADDRMASK;
				if (_cpu_regs.bc == 0)
					_cpu_regs.bc = 0x10000;
				do {
					acu = RAM_MM(_cpu_regs.hl);
					PUT_BYTE_MM(_cpu_regs.de, acu);
				} while (--_cpu_regs.bc);
				acu += CPU_REG_GET_HIGH(_cpu_regs.af);
				_cpu_regs.af = (_cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
				break;

			case 0xb9:      /* CPDR */
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				_cpu_regs.bc &= ADDRMASK;
				if (_cpu_regs.bc == 0)
					_cpu_regs.bc = 0x10000;
				do {
					temp = RAM_MM(_cpu_regs.hl);
					op = --_cpu_regs.bc != 0;
					sum = acu - temp;
				} while (op && sum != 0);
				cbits = acu ^ temp ^ sum;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
					(((sum - ((cbits & 16) >> 4)) & 2) << 4) |
					(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
					op << 2 | 2;
				if ((sum & 15) == 8 && (cbits & 16) != 0)
					_cpu_regs.af &= ~8;
				break;

			case 0xba:      /* INDR */
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				if (temp == 0)
					temp = 0x100;
				do {
					acu = cpu_in(CPU_REG_GET_LOW(_cpu_regs.bc));
					PUT_BYTE(_cpu_regs.hl, acu);
					--_cpu_regs.hl;
				} while (--temp);
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				CPU_REG_SET_HIGH(_cpu_regs.bc, 0);
				INOUTFLAGS_ZERO((CPU_REG_GET_LOW(_cpu_regs.bc) - 1) & 0xff);
				break;

			case 0xbb:      /* OTDR */
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				if (temp == 0)
					temp = 0x100;
				do {
					acu = GET_BYTE(_cpu_regs.hl);
					cpu_out(CPU_REG_GET_LOW(_cpu_regs.bc), acu);
					--_cpu_regs.hl;
				} while (--temp);
				temp = CPU_REG_GET_HIGH(_cpu_regs.bc);
				CPU_REG_SET_HIGH(_cpu_regs.bc, 0);
				INOUTFLAGS_ZERO(CPU_REG_GET_LOW(_cpu_regs.hl));
				break;

			default:    /* ignore ED and following byte */
				break;
			}
			break;

		case 0xee:      /* XOR nn */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ RAM_PP(_cpu_regs.pc)) & 0xff];
			break;

		case 0xef:      /* RST 28H */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 0x28;
			break;

		case 0xf0:      /* RET P */
			if (!(TSTFLAG(S)))
				POP(_cpu_regs.pc);
			break;

		case 0xf1:      /* POP _cpu_regs.af */
			POP(_cpu_regs.af);
			break;

		case 0xf2:      /* JP P,nnnn */
			JPC(!TSTFLAG(S));
			break;

		case 0xf3:      /* DI */
			_cpu_regs.iff = 0;
			break;

		case 0xf4:      /* CALL P,nnnn */
			CALLC(!TSTFLAG(S));
			break;

		case 0xf5:      /* PUSH _cpu_regs.af */
			PUSH(_cpu_regs.af);
			break;

		case 0xf6:      /* OR nn */
			_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | RAM_PP(_cpu_regs.pc)) & 0xff];
			break;

		case 0xf7:      /* RST 30H */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 0x30;
			break;

		case 0xf8:      /* RET M */
			if (TSTFLAG(S))
				POP(_cpu_regs.pc);
			break;

		case 0xf9:      /* LD _cpu_regs.sp,_cpu_regs.hl */
			_cpu_regs.sp = _cpu_regs.hl;
			break;

		case 0xfa:      /* JP M,nnnn */
			JPC(TSTFLAG(S));
			break;

		case 0xfb:      /* EI */
			_cpu_regs.iff = 3;
			break;

		case 0xfc:      /* CALL M,nnnn */
			CALLC(TSTFLAG(S));
			break;

		case 0xfd:      /* FD prefix */
			switch (RAM_PP(_cpu_regs.pc)) {

			case 0x09:      /* ADD _cpu_regs.iy,_cpu_regs.bc */
				_cpu_regs.iy &= ADDRMASK;
				_cpu_regs.bc &= ADDRMASK;
				sum = _cpu_regs.iy + _cpu_regs.bc;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.iy ^ _cpu_regs.bc ^ sum) >> 8];
				_cpu_regs.iy = sum;
				break;

			case 0x19:      /* ADD _cpu_regs.iy,_cpu_regs.de */
				_cpu_regs.iy &= ADDRMASK;
				_cpu_regs.de &= ADDRMASK;
				sum = _cpu_regs.iy + _cpu_regs.de;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.iy ^ _cpu_regs.de ^ sum) >> 8];
				_cpu_regs.iy = sum;
				break;

			case 0x21:      /* LD _cpu_regs.iy,nnnn */
				_cpu_regs.iy = GET_WORD(_cpu_regs.pc);
				_cpu_regs.pc += 2;
				break;

			case 0x22:      /* LD (nnnn),_cpu_regs.iy */
				temp = GET_WORD(_cpu_regs.pc);
				PUT_WORD(temp, _cpu_regs.iy);
				_cpu_regs.pc += 2;
				break;

			case 0x23:      /* INC _cpu_regs.iy */
				++_cpu_regs.iy;
				break;

			case 0x24:      /* INC IYH */
				_cpu_regs.iy += 0x100;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incZ80Table[CPU_REG_GET_HIGH(_cpu_regs.iy)];
				break;

			case 0x25:      /* DEC IYH */
				_cpu_regs.iy -= 0x100;
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decZ80Table[CPU_REG_GET_HIGH(_cpu_regs.iy)];
				break;

			case 0x26:      /* LD IYH,nn */
				CPU_REG_SET_HIGH(_cpu_regs.iy, RAM_PP(_cpu_regs.pc));
				break;

			case 0x29:      /* ADD _cpu_regs.iy,_cpu_regs.iy */
				_cpu_regs.iy &= ADDRMASK;
				sum = _cpu_regs.iy + _cpu_regs.iy;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | cbitsDup16Table[sum >> 8];
				_cpu_regs.iy = sum;
				break;

			case 0x2a:      /* LD _cpu_regs.iy,(nnnn) */
				temp = GET_WORD(_cpu_regs.pc);
				_cpu_regs.iy = GET_WORD(temp);
				_cpu_regs.pc += 2;
				break;

			case 0x2b:      /* DEC _cpu_regs.iy */
				--_cpu_regs.iy;
				break;

			case 0x2c:      /* INC IYL */
				temp = CPU_REG_GET_LOW(_cpu_regs.iy) + 1;
				CPU_REG_SET_LOW(_cpu_regs.iy, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incZ80Table[temp];
				break;

			case 0x2d:      /* DEC IYL */
				temp = CPU_REG_GET_LOW(_cpu_regs.iy) - 1;
				CPU_REG_SET_LOW(_cpu_regs.iy, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
				break;

			case 0x2e:      /* LD IYL,nn */
				CPU_REG_SET_LOW(_cpu_regs.iy, RAM_PP(_cpu_regs.pc));
				break;

			case 0x34:      /* INC (_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr) + 1;
				PUT_BYTE(adr, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | incZ80Table[temp];
				break;

			case 0x35:      /* DEC (_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr) - 1;
				PUT_BYTE(adr, temp);
				_cpu_regs.af = (_cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
				break;

			case 0x36:      /* LD (_cpu_regs.iy+dd),nn */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, RAM_PP(_cpu_regs.pc));
				break;

			case 0x39:      /* ADD _cpu_regs.iy,_cpu_regs.sp */
				_cpu_regs.iy &= ADDRMASK;
				_cpu_regs.sp &= ADDRMASK;
				sum = _cpu_regs.iy + _cpu_regs.sp;
				_cpu_regs.af = (_cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(_cpu_regs.iy ^ _cpu_regs.sp ^ sum) >> 8];
				_cpu_regs.iy = sum;
				break;

			case 0x44:      /* LD B,IYH */
				CPU_REG_SET_HIGH(_cpu_regs.bc, CPU_REG_GET_HIGH(_cpu_regs.iy));
				break;

			case 0x45:      /* LD B,IYL */
				CPU_REG_SET_HIGH(_cpu_regs.bc, CPU_REG_GET_LOW(_cpu_regs.iy));
				break;

			case 0x46:      /* LD B,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.bc, GET_BYTE(adr));
				break;

			case 0x4c:      /* LD C,IYH */
				CPU_REG_SET_LOW(_cpu_regs.bc, CPU_REG_GET_HIGH(_cpu_regs.iy));
				break;

			case 0x4d:      /* LD C,IYL */
				CPU_REG_SET_LOW(_cpu_regs.bc, CPU_REG_GET_LOW(_cpu_regs.iy));
				break;

			case 0x4e:      /* LD C,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_LOW(_cpu_regs.bc, GET_BYTE(adr));
				break;

			case 0x54:      /* LD D,IYH */
				CPU_REG_SET_HIGH(_cpu_regs.de, CPU_REG_GET_HIGH(_cpu_regs.iy));
				break;

			case 0x55:      /* LD D,IYL */
				CPU_REG_SET_HIGH(_cpu_regs.de, CPU_REG_GET_LOW(_cpu_regs.iy));
				break;

			case 0x56:      /* LD D,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.de, GET_BYTE(adr));
				break;

			case 0x5c:      /* LD E,IYH */
				CPU_REG_SET_LOW(_cpu_regs.de, CPU_REG_GET_HIGH(_cpu_regs.iy));
				break;

			case 0x5d:      /* LD E,IYL */
				CPU_REG_SET_LOW(_cpu_regs.de, CPU_REG_GET_LOW(_cpu_regs.iy));
				break;

			case 0x5e:      /* LD E,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_LOW(_cpu_regs.de, GET_BYTE(adr));
				break;

			case 0x60:      /* LD IYH,B */
				CPU_REG_SET_HIGH(_cpu_regs.iy, CPU_REG_GET_HIGH(_cpu_regs.bc));
				break;

			case 0x61:      /* LD IYH,C */
				CPU_REG_SET_HIGH(_cpu_regs.iy, CPU_REG_GET_LOW(_cpu_regs.bc));
				break;

			case 0x62:      /* LD IYH,D */
				CPU_REG_SET_HIGH(_cpu_regs.iy, CPU_REG_GET_HIGH(_cpu_regs.de));
				break;

			case 0x63:      /* LD IYH,E */
				CPU_REG_SET_HIGH(_cpu_regs.iy, CPU_REG_GET_LOW(_cpu_regs.de));
				break;

			case 0x64:      /* LD IYH,IYH */
				break;

			case 0x65:      /* LD IYH,IYL */
				CPU_REG_SET_HIGH(_cpu_regs.iy, CPU_REG_GET_LOW(_cpu_regs.iy));
				break;

			case 0x66:      /* LD H,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.hl, GET_BYTE(adr));
				break;

			case 0x67:      /* LD IYH,A */
				CPU_REG_SET_HIGH(_cpu_regs.iy, CPU_REG_GET_HIGH(_cpu_regs.af));
				break;

			case 0x68:      /* LD IYL,B */
				CPU_REG_SET_LOW(_cpu_regs.iy, CPU_REG_GET_HIGH(_cpu_regs.bc));
				break;

			case 0x69:      /* LD IYL,C */
				CPU_REG_SET_LOW(_cpu_regs.iy, CPU_REG_GET_LOW(_cpu_regs.bc));
				break;

			case 0x6a:      /* LD IYL,D */
				CPU_REG_SET_LOW(_cpu_regs.iy, CPU_REG_GET_HIGH(_cpu_regs.de));
				break;

			case 0x6b:      /* LD IYL,E */
				CPU_REG_SET_LOW(_cpu_regs.iy, CPU_REG_GET_LOW(_cpu_regs.de));
				break;

			case 0x6c:      /* LD IYL,IYH */
				CPU_REG_SET_LOW(_cpu_regs.iy, CPU_REG_GET_HIGH(_cpu_regs.iy));
				break;

			case 0x6d:      /* LD IYL,IYL */
				break;

			case 0x6e:      /* LD L,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_LOW(_cpu_regs.hl, GET_BYTE(adr));
				break;

			case 0x6f:      /* LD IYL,A */
				CPU_REG_SET_LOW(_cpu_regs.iy, CPU_REG_GET_HIGH(_cpu_regs.af));
				break;

			case 0x70:      /* LD (_cpu_regs.iy+dd),B */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.bc));
				break;

			case 0x71:      /* LD (_cpu_regs.iy+dd),C */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_LOW(_cpu_regs.bc));
				break;

			case 0x72:      /* LD (_cpu_regs.iy+dd),D */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.de));
				break;

			case 0x73:      /* LD (_cpu_regs.iy+dd),E */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_LOW(_cpu_regs.de));
				break;

			case 0x74:      /* LD (_cpu_regs.iy+dd),H */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.hl));
				break;

			case 0x75:      /* LD (_cpu_regs.iy+dd),L */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_LOW(_cpu_regs.hl));
				break;

			case 0x77:      /* LD (_cpu_regs.iy+dd),A */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				PUT_BYTE(adr, CPU_REG_GET_HIGH(_cpu_regs.af));
				break;

			case 0x7c:      /* LD A,IYH */
				CPU_REG_SET_HIGH(_cpu_regs.af, CPU_REG_GET_HIGH(_cpu_regs.iy));
				break;

			case 0x7d:      /* LD A,IYL */
				CPU_REG_SET_HIGH(_cpu_regs.af, CPU_REG_GET_LOW(_cpu_regs.iy));
				break;

			case 0x7e:      /* LD A,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				CPU_REG_SET_HIGH(_cpu_regs.af, GET_BYTE(adr));
				break;

			case 0x84:      /* ADD A,IYH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.iy);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp;
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x85:      /* ADD A,IYL */
				temp = CPU_REG_GET_LOW(_cpu_regs.iy);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp;
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x86:      /* ADD A,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp;
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x8c:      /* ADC A,IYH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.iy);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp + TSTFLAG(C);
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x8d:      /* ADC A,IYL */
				temp = CPU_REG_GET_LOW(_cpu_regs.iy);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp + TSTFLAG(C);
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x8e:      /* ADC A,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu + temp + TSTFLAG(C);
				_cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
				break;

			case 0x96:      /* SUB (_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0x94:      /* SUB IYH */
				SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

			case 0x9c:      /* SBC A,IYH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.iy);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp - TSTFLAG(C);
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0x95:      /* SUB IYL */
				SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

			case 0x9d:      /* SBC A,IYL */
				temp = CPU_REG_GET_LOW(_cpu_regs.iy);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp - TSTFLAG(C);
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0x9e:      /* SBC A,(_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp - TSTFLAG(C);
				_cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xa4:      /* AND IYH */
				_cpu_regs.af = andTable[((_cpu_regs.af & _cpu_regs.iy) >> 8) & 0xff];
				break;

			case 0xa5:      /* AND IYL */
				_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & _cpu_regs.iy) & 0xff];
				break;

			case 0xa6:      /* AND (_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				_cpu_regs.af = andTable[((_cpu_regs.af >> 8) & GET_BYTE(adr)) & 0xff];
				break;

			case 0xac:      /* XOR IYH */
				_cpu_regs.af = xororTable[((_cpu_regs.af ^ _cpu_regs.iy) >> 8) & 0xff];
				break;

			case 0xad:      /* XOR IYL */
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ _cpu_regs.iy) & 0xff];
				break;

			case 0xae:      /* XOR (_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) ^ GET_BYTE(adr)) & 0xff];
				break;

			case 0xb4:      /* OR IYH */
				_cpu_regs.af = xororTable[((_cpu_regs.af | _cpu_regs.iy) >> 8) & 0xff];
				break;

			case 0xb5:      /* OR IYL */
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | _cpu_regs.iy) & 0xff];
				break;

			case 0xb6:      /* OR (_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				_cpu_regs.af = xororTable[((_cpu_regs.af >> 8) | GET_BYTE(adr)) & 0xff];
				break;

			case 0xbc:      /* CP IYH */
				temp = CPU_REG_GET_HIGH(_cpu_regs.iy);
				_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
					cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xbd:      /* CP IYL */
				temp = CPU_REG_GET_LOW(_cpu_regs.iy);
				_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
					cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xbe:      /* CP (_cpu_regs.iy+dd) */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				temp = GET_BYTE(adr);
				_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
				acu = CPU_REG_GET_HIGH(_cpu_regs.af);
				sum = acu - temp;
				_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
					cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
				break;

			case 0xcb:      /* CB prefix */
				adr = _cpu_regs.iy + (int8_t)RAM_PP(_cpu_regs.pc);
				switch ((op = GET_BYTE(_cpu_regs.pc)) & 7) {

				case 0:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.bc);
					break;

				case 1:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_LOW(_cpu_regs.bc);
					break;

				case 2:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.de);
					break;

				case 3:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_LOW(_cpu_regs.de);
					break;

				case 4:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.hl);
					break;

				case 5:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_LOW(_cpu_regs.hl);
					break;

				case 6:
					++_cpu_regs.pc;
					acu = GET_BYTE(adr);
					break;

				case 7:
					++_cpu_regs.pc;
					acu = CPU_REG_GET_HIGH(_cpu_regs.af);
					break;
				}
				switch (op & 0xc0) {

				case 0x00:  /* shift/rotate */
					switch (op & 0x38) {

					case 0x00:/* RLC */
						temp = (acu << 1) | (acu >> 7);
						cbits = temp & 1;
						goto cbshflg3;

					case 0x08:/* RRC */
						temp = (acu >> 1) | (acu << 7);
						cbits = temp & 0x80;
						goto cbshflg3;

					case 0x10:/* RL */
						temp = (acu << 1) | TSTFLAG(C);
						cbits = acu & 0x80;
						goto cbshflg3;

					case 0x18:/* RR */
						temp = (acu >> 1) | (TSTFLAG(C) << 7);
						cbits = acu & 1;
						goto cbshflg3;

					case 0x20:/* SLA */
						temp = acu << 1;
						cbits = acu & 0x80;
						goto cbshflg3;

					case 0x28:/* SRA */
						temp = (acu >> 1) | (acu & 0x80);
						cbits = acu & 1;
						goto cbshflg3;

					case 0x30:/* SLIA */
						temp = (acu << 1) | 1;
						cbits = acu & 0x80;
						goto cbshflg3;

					case 0x38:/* SRL */
						temp = acu >> 1;
						cbits = acu & 1;
					cbshflg3:
						_cpu_regs.af = (_cpu_regs.af & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
					}
					break;

				case 0x40:  /* BIT */
					if (acu & (1 << ((op >> 3) & 7)))
						_cpu_regs.af = (_cpu_regs.af & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
					else
						_cpu_regs.af = (_cpu_regs.af & ~0xfe) | 0x54;
					if ((op & 7) != 6)
						_cpu_regs.af |= (acu & 0x28);
					temp = acu;
					break;

				case 0x80:  /* RES */
					temp = acu & ~(1 << ((op >> 3) & 7));
					break;

				case 0xc0:  /* SET */
					temp = acu | (1 << ((op >> 3) & 7));
					break;
				}
				switch (op & 7) {

				case 0:
					CPU_REG_SET_HIGH(_cpu_regs.bc, temp);
					break;

				case 1:
					CPU_REG_SET_LOW(_cpu_regs.bc, temp);
					break;

				case 2:
					CPU_REG_SET_HIGH(_cpu_regs.de, temp);
					break;

				case 3:
					CPU_REG_SET_LOW(_cpu_regs.de, temp);
					break;

				case 4:
					CPU_REG_SET_HIGH(_cpu_regs.hl, temp);
					break;

				case 5:
					CPU_REG_SET_LOW(_cpu_regs.hl, temp);
					break;

				case 6:
					PUT_BYTE(adr, temp);
					break;

				case 7:
					CPU_REG_SET_HIGH(_cpu_regs.af, temp);
					break;
				}
				break;

			case 0xe1:      /* POP _cpu_regs.iy */
				POP(_cpu_regs.iy);
				break;

			case 0xe3:      /* EX (_cpu_regs.sp),_cpu_regs.iy */
				temp = _cpu_regs.iy;
				POP(_cpu_regs.iy);
				PUSH(temp);
				break;

			case 0xe5:      /* PUSH _cpu_regs.iy */
				PUSH(_cpu_regs.iy);
				break;

			case 0xe9:      /* JP (_cpu_regs.iy) */
				_cpu_regs.pc = _cpu_regs.iy;
				break;

			case 0xf9:      /* LD _cpu_regs.sp,_cpu_regs.iy */
				_cpu_regs.sp = _cpu_regs.iy;
				break;

			default:            /* ignore FD */
				_cpu_regs.pc--;
			}
			break;

		case 0xfe:      /* CP nn */
			temp = RAM_PP(_cpu_regs.pc);
			_cpu_regs.af = (_cpu_regs.af & ~0x28) | (temp & 0x28);
			acu = CPU_REG_GET_HIGH(_cpu_regs.af);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			_cpu_regs.af = (_cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
				(SET_PV) | cbits2Table[cbits & 0x1ff];
			break;

		case 0xff:      /* RST 38H */
			PUSH(_cpu_regs.pc);
			_cpu_regs.pc = 0x38;
		}
	}
end_decode:
	;
}
