#include "defaults.h"
#include "globals.h"
#include "cpu.h"
#include "cpm.h"
#include "ram.h"
#include "pal.h"

/* see main.c for definition */

cpu_regs_t cpu_regs;

int32_t cpu_status = 0; /* cpu_status of the CPU 0=running 1=end request 2=back to CCP */
int32_t cpu_debug = 0;
int32_t cpu_break = -1;
int32_t cpu_step = -1;

/*
	Functions needed by the soft CPU implementation
*/
static void cpu_out(const uint32_t Port, const uint32_t Value) {
  cpm_bios();
}

uint32_t cpu_in(const uint32_t Port) {
  cpm_bdos();
  return (CPU_REG_GET_HIGH(cpu_regs.af));
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

#define SET_FLAG(f,c)    (cpu_regs.af = (c) ? cpu_regs.af | FLAG_ ## f : cpu_regs.af & ~FLAG_ ## f)
#define TST_FLAG(f)      ((cpu_regs.af & FLAG_ ## f) != 0)

#define PARITY(x)   _parity_table[(x) & 0xff]
/*  SET_PV and SET_PV2 are used to provide correct PARITY flag semantics for the 8080 in cases
  where the Z80 uses the overflow flag
*/
#define SET_PVS(s)  (((cbits >> 6) ^ (cbits >> 5)) & 4)
#define SET_PV      (SET_PVS(sum))
#define SET_PV2(x)  ((temp == (x)) << 2)

#define POP(x)  {                               \
    register uint32_t y = RAM_PP(cpu_regs.sp);             \
    x = y + (RAM_PP(cpu_regs.sp) << 8);                  \
  }

#define JPC(cond) {                             \
    if (cond) {                                 \
      cpu_regs.pc = GET_WORD(cpu_regs.pc);                      \
    }                                           \
    else {                                      \
      cpu_regs.pc += 2;                                \
    }                                           \
  }

#define CALLC(cond) {                           \
    if (cond) {                                 \
      register uint32_t adrr = GET_WORD(cpu_regs.pc);    \
      PUSH(cpu_regs.pc + 2);                           \
      cpu_regs.pc = adrr;                              \
    }                                           \
    else {                                      \
      cpu_regs.pc += 2;                                \
    }                                           \
  }

/* the following tables precompute some common subexpressions
  _parity_table[i]          0..255  (number of 1's in i is odd) ? 0 : 4
  _inc_table[i]             0..256! (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0) << 4)
  _dec_table[i]             0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0xf) << 4) | 2
  cbitsTable[i]           0..511  (i & 0x10) | ((i >> 8) & 1)
  cbitsDup8Table[i]       0..511  (i & 0x10) | ((i >> 8) & 1) | ((i & 0xff) << 8) | (i & 0xa8) |
  (((i & 0xff) == 0) << 6)
  cbitsDup16Table[i]      0..511  (i & 0x10) | ((i >> 8) & 1) | (i & 0x28)
  cbits2Table[i]          0..511  (i & 0x10) | ((i >> 8) & 1) | 2
  rrcaTable[i]            0..255  ((i & 1) << 15) | ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1)
  rraTable[i]             0..255  ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1)
  addTable[i]             0..511  ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6)
  subTable[i]             0..255  ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | 2
  andTable[i]             0..255  (i << 8) | (i & 0xa8) | ((i == 0) << 6) | 0x10 | _parity_table[i]
  xororTable[i]           0..255  (i << 8) | (i & 0xa8) | ((i == 0) << 6) | _parity_table[i]
  rotateShiftTable[i]     0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) | _parity_table[i & 0xff]
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
  rrdrldTable[i]          0..255  (i << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | _parity_table[i]
  cpTable[i]              0..255  (i & 0x80) | (((i & 0xff) == 0) << 6)
*/

#include "cpu_tables.h"

/* Memory management    */
uint8_t GET_BYTE(register uint32_t Addr) {
  return ram_read(Addr & ADDRMASK);
}

void PUT_BYTE(register uint32_t Addr, register uint32_t Value) {
  ram_write(Addr & ADDRMASK, Value);
}

void PUT_WORD(register uint32_t Addr, register uint32_t Value) {
  ram_write(Addr & ADDRMASK, Value);
  ram_write((Addr + 1) & ADDRMASK, Value >> 8);
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
    MM_PUT_BYTE(cpu_regs.sp, (x) >> 8);  \
    MM_PUT_BYTE(cpu_regs.sp, x);         \
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
  cpu_regs.af = (cpu_regs.af & 0xff00) | (syxz) |               /* SF, YF, XF, ZF   */  \
                ((acu & 0x80) >> 6) |                           /* NF       */  \
                ((acu + (x)) > 0xff ? (FLAG_C | FLAG_H) : 0) |  /* CF, HF   */  \
                _parity_table[((acu + (x)) & 7) ^ temp]           /* PF       */

#define INOUTFLAGS_ZERO(x)      INOUTFLAGS(FLAG_Z, x)
#define INOUTFLAGS_NONZERO(x)                                           \
  INOUTFLAGS((CPU_REG_GET_HIGH(cpu_regs.bc) & 0xa8) | ((CPU_REG_GET_HIGH(cpu_regs.bc) == 0) << 6), x)

void cpu_reset(void) {
  cpu_regs.pcx = 0;
  cpu_regs.af = 0;
  cpu_regs.bc = 0;
  cpu_regs.de = 0;
  cpu_regs.hl = 0;
  cpu_regs.ix = 0;
  cpu_regs.iy = 0;
  cpu_regs.pc = 0;
  cpu_regs.sp = 0;
  cpu_regs.af1 = 0;
  cpu_regs.bc1 = 0;
  cpu_regs.de1 = 0;
  cpu_regs.hl1 = 0;
  cpu_regs.iff = 0;
  cpu_regs.ir = 0;
  cpu_status = 0;
  cpu_debug = 0;
  cpu_break = -1;
  cpu_step = -1;
}

#ifdef DEBUG
static void _watch_print(uint16_t pos) {
  uint8_t I, J;
  pal_puts("\r\n");
  pal_puts("  _cpu_watch : "); pal_put_hex16(_cpu_watch);
  pal_puts(" = "); pal_put_hex8(ram_read(_cpu_watch)); pal_put_con(':'); pal_put_hex8(ram_read(_cpu_watch + 1));
  pal_puts(" / ");
  for (J = 0, I = ram_read(_cpu_watch); J < 8; J++, I <<= 1) pal_put_con(I & 0x80 ? '1' : '0');
  pal_put_con(':');
  for (J = 0, I = ram_read(_cpu_watch + 1); J < 8; J++, I <<= 1) pal_put_con(I & 0x80 ? '1' : '0');
}

static void _mem_dump(uint16_t pos) {
  uint16_t h = pos;
  uint16_t c = pos;
  uint8_t l, i;
  uint8_t ch = pos & 0xff;

  pal_puts("       ");
  for (i = 0; i < 16; i++) {
    pal_put_hex8(ch++ & 0x0f);
    pal_puts(" ");
  }
  pal_puts("\r\n");
  pal_puts("       ");
  for (i = 0; i < 16; i++)
    pal_puts("---");
  pal_puts("\r\n");
  for (l = 0; l < 16; l++) {
    pal_put_hex16(h);
    pal_puts(" : ");
    for (i = 0; i < 16; i++) {
      pal_put_hex8(ram_read(h++));
      pal_puts(" ");
    }
    for (i = 0; i < 16; i++) {
      ch = ram_read(c++);
      pal_put_con(ch > 31 && ch < 127 ? ch : '.');
    }
    pal_puts("\r\n");
  }
}

static uint8_t _disasm(uint16_t pos) {
  const char *txt;
  char jr;
  uint8_t ch = ram_read(pos);
  uint8_t count = 1;
  uint8_t c;

  switch (ch) {
    case 0xCB: pos++; txt = _mnemonics_cb[ram_read(pos++)]; count++; break;
    case 0xED: pos++; txt = _mnemonics_ed[ram_read(pos++)]; count++; break;
    case 0xDD: pos++; c = 'X';
      if (ram_read(pos) != 0xCB) {
        txt = _mnemonics_xx[ram_read(pos++)]; count++;
      } else {
        pos++; txt = _mnemonics_xcb[ram_read(pos++)]; count += 2;
      }
      break;
    case 0xFD: pos++; c = 'Y';
      if (ram_read(pos) != 0xCB) {
        txt = _mnemonics_xx[ram_read(pos++)]; count++;
      } else {
        pos++; txt = _mnemonics_xcb[ram_read(pos++)]; count += 2;
      }
      break;
    default:   txt = _mnemonics[ram_read(pos++)];
  }
  while (*txt != 0) {
    switch (*txt) {
      case '*':
        txt += 2;
        count++;
        pal_put_hex8(ram_read(pos++));
        break;
      case '^':
        txt += 2;
        count++;
        pal_put_hex8(ram_read(pos++));
        break;
      case '#':
        txt += 2;
        count += 2;
        pal_put_hex8(ram_read(pos + 1));
        pal_put_hex8(ram_read(pos));
        break;
      case '@':
        txt += 2;
        count++;
        jr = ram_read(pos++);
        pal_put_hex16(pos + jr);
        break;
      case '%':
        pal_putch(c);
        txt++;
        break;
      default:
        pal_putch(*txt);
        txt++;
    }
  }

  return (count);
}

void cpu_debug_out(void) {
  uint8_t ch = 0;
  uint16_t pos, l;
  static const char Flags[9] = "SZ5H3PNC";
  uint8_t J, I;
  unsigned int bpoint;
  uint8_t loop = 1;

  while (loop) {
    pos = cpu_regs.pc;
    pal_puts("\r\n");
    pal_puts("cpu_regs.bc:");  pal_put_hex16(cpu_regs.bc);
    pal_puts(" DE:"); pal_put_hex16(cpu_regs.de);
    pal_puts(" cpu_regs.hl:"); pal_put_hex16(cpu_regs.hl);
    pal_puts(" cpu_regs.af:"); pal_put_hex16(cpu_regs.af);
    pal_puts(" : [");
    for (J = 0, I = CPU_REG_GET_LOW(cpu_regs.af); J < 8; J++, I <<= 1) pal_put_con(I & 0x80 ? Flags[J] : '.');
    pal_puts("]\r\n");
    pal_puts("cpu_regs.ix:");  pal_put_hex16(cpu_regs.ix);
    pal_puts(" cpu_regs.iy:"); pal_put_hex16(cpu_regs.iy);
    pal_puts(" cpu_regs.sp:"); pal_put_hex16(cpu_regs.sp);
    pal_puts(" cpu_regs.pc:"); pal_put_hex16(cpu_regs.pc);
    pal_puts(" : ");

    _disasm(pos);

    if (cpu_regs.pc == 0x0005) {
      if (CPU_REG_GET_LOW(cpu_regs.bc) > 40) {
        pal_puts(" (Unknown)");
      } else {
        pal_puts(" (");
        pal_puts(_cpm_calls[CPU_REG_GET_LOW(cpu_regs.bc)]);
        pal_puts(")");
      }
    }

    if (_cpu_watch != -1) {
      _watch_print(_cpu_watch);
    }

    pal_puts("\r\n");
    pal_puts("Command|? : ");
    ch = pal_getch();
    if (ch > 21 && ch < 127)
      pal_putch(ch);
    switch (ch) {
      case 't':
        loop = 0;
        break;
      case 'c':
        loop = 0;
        pal_puts("\r\n");
        cpu_debug = 0;
        break;
      case 'b':
        pal_puts("\r\n"); _mem_dump(cpu_regs.bc); break;
      case 'd':
        pal_puts("\r\n"); _mem_dump(cpu_regs.de); break;
      case 'h':
        pal_puts("\r\n"); _mem_dump(cpu_regs.hl); break;
      case 'p':
        pal_puts("\r\n"); _mem_dump(cpu_regs.pc & 0xFF00); break;
      case 's':
        pal_puts("\r\n"); _mem_dump(cpu_regs.sp & 0xFF00); break;
      case 'x':
        pal_puts("\r\n"); _mem_dump(cpu_regs.ix & 0xFF00); break;
      case 'y':
        pal_puts("\r\n"); _mem_dump(cpu_regs.iy & 0xFF00); break;
      case 'a':
        pal_puts("\r\n"); _mem_dump(glb_dma_addr); break;
      case 'l':
        pal_puts("\r\n");
        I = 16;
        l = pos;
        while (I > 0) {
          pal_put_hex16(l);
          pal_puts(" : ");
          l += _disasm(l);
          pal_puts("\r\n");
          I--;
        }
        break;
      case 'B':
        pal_puts(" Addr: ");
        scanf("%04x", &bpoint);
        cpu_break = bpoint;
        pal_puts("Breakpoint set to ");
        pal_put_hex16(cpu_break);
        pal_puts("\r\n");
        break;
      case 'S':
        loop = 0;
        cpu_status = 1;
        pal_puts(" Stopping virtual machine\r\n");
        break;
      case 'C':
        cpu_break = -1;
        pal_puts(" Breakpoint cleared\r\n");
        break;
      case 'D':
        pal_puts(" Addr: ");
        scanf("%04x", &bpoint);
        _mem_dump(bpoint);
        break;
      case 'L':
        pal_puts(" Addr: ");
        scanf("%04x", &bpoint);
        I = 16;
        l = bpoint;
        while (I > 0) {
          pal_put_hex16(l);
          pal_puts(" : ");
          l += _disasm(l);
          pal_puts("\r\n");
          I--;
        }
        break;
      case 'T':
        loop = 0;
        cpu_step = pos + 3; // This only works correctly with CALL
        // If the called function messes with the stack, this will fail as well.
        cpu_debug = 0;
        break;
      case 'W':
        pal_puts(" Addr: ");
        scanf("%04x", &bpoint);
        _cpu_watch = bpoint;
        pal_puts("_cpu_watch set to ");
        pal_put_hex16(_cpu_watch);
        pal_puts("\r\n");
        break;
      case '?':
        pal_puts("\r\n");
        pal_puts("Lowercase commands:\r\n");
        pal_puts("  t - traces to the next instruction\r\n");
        pal_puts("  c - Continue execution\r\n");
        pal_puts("  b - Dumps memory pointed by (bc)\r\n");
        pal_puts("  d - Dumps memory pointed by (de)\r\n");
        pal_puts("  h - Dumps memory pointed by (hl)\r\n");
        pal_puts("  p - Dumps the page (pc) points to\r\n");
        pal_puts("  s - Dumps the page (sp) points to\r\n");
        pal_puts("  x - Dumps the page (ix) points to\r\n");
        pal_puts("  y - Dumps the page (iy) points to\r\n");
        pal_puts("  a - Dumps memory pointed by dma address\r\n");
        pal_puts("  l - Disassembles from current pc\r\n");
        pal_puts("Uppercase commands:\r\n");
        pal_puts("  B - Sets breakpoint at address\r\n");
        pal_puts("  C - Clears breakpoint\r\n");
        pal_puts("  D - Dumps memory at address\r\n");
        pal_puts("  L - Disassembles at address\r\n");
        pal_puts("  T - Steps over a call\r\n");
        pal_puts("  W - Sets a byte/word watch\r\n");
        pal_puts("  S - Stop virtual machine\r\n");
        break;
      default:
        pal_puts(" ???\r\n");
    }
  }
}
#endif

void cpu_run(void) {
  register uint32_t temp = 0;
  register uint32_t acu = 0;
  register uint32_t sum;
  register uint32_t cbits;
  register uint32_t op;
  register uint32_t adr;

  /* main instruction fetch/decode loop */
  while (!cpu_status) {	/* loop until cpu_status != 0 */

#ifdef DEBUG
    if (cpu_regs.pc == cpu_break) {
      pal_puts(":BREAK at ");
      pal_put_hex16(cpu_break);
      pal_puts(":");
      cpu_debug = 1;
    }
    if (cpu_regs.pc == cpu_step) {
      cpu_debug = 1;
      cpu_step = -1;
    }
    if (cpu_debug) {
      cpu_debug_out();
    }
#endif

    cpu_regs.pcx = cpu_regs.pc;

    switch (RAM_PP(cpu_regs.pc)) {

      case 0x00:      /* NOP */
        break;

      case 0x01:      /* LD cpu_regs.bc,nnnn */
        cpu_regs.bc = GET_WORD(cpu_regs.pc);
        cpu_regs.pc += 2;
        break;

      case 0x02:      /* LD (cpu_regs.bc),A */
        PUT_BYTE(cpu_regs.bc, CPU_REG_GET_HIGH(cpu_regs.af));
        break;

      case 0x03:      /* INC cpu_regs.bc */
        ++cpu_regs.bc;
        break;

      case 0x04:      /* INC B */
        cpu_regs.bc += 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.bc);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
        break;

      case 0x05:      /* DEC B */
        cpu_regs.bc -= 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.bc);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
        break;

      case 0x06:      /* LD B,nn */
        CPU_REG_SET_HIGH(cpu_regs.bc, RAM_PP(cpu_regs.pc));
        break;

      case 0x07:      /* RLCA */
        cpu_regs.af = ((cpu_regs.af >> 7) & 0x0128) | ((cpu_regs.af << 1) & ~0x1ff) |
                      (cpu_regs.af & 0xc4) | ((cpu_regs.af >> 15) & 1);
        break;

      case 0x08:      /* EX cpu_regs.af,cpu_regs.af' */
        temp = cpu_regs.af;
        cpu_regs.af = cpu_regs.af1;
        cpu_regs.af1 = temp;
        break;

      case 0x09:      /* ADD cpu_regs.hl,cpu_regs.bc */
        cpu_regs.hl &= ADDRMASK;
        cpu_regs.bc &= ADDRMASK;
        sum = cpu_regs.hl + cpu_regs.bc;
        cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.hl ^ cpu_regs.bc ^ sum) >> 8];
        cpu_regs.hl = sum;
        break;

      case 0x0a:      /* LD A,(cpu_regs.bc) */
        CPU_REG_SET_HIGH(cpu_regs.af, GET_BYTE(cpu_regs.bc));
        break;

      case 0x0b:      /* DEC cpu_regs.bc */
        --cpu_regs.bc;
        break;

      case 0x0c:      /* INC C */
        temp = CPU_REG_GET_LOW(cpu_regs.bc) + 1;
        CPU_REG_SET_LOW(cpu_regs.bc, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80);
        break;

      case 0x0d:      /* DEC C */
        temp = CPU_REG_GET_LOW(cpu_regs.bc) - 1;
        CPU_REG_SET_LOW(cpu_regs.bc, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp & 0xff] | SET_PV2(0x7f);
        break;

      case 0x0e:      /* LD C,nn */
        CPU_REG_SET_LOW(cpu_regs.bc, RAM_PP(cpu_regs.pc));
        break;

      case 0x0f:      /* RRCA */
        cpu_regs.af = (cpu_regs.af & 0xc4) | rrcaTable[CPU_REG_GET_HIGH(cpu_regs.af)];
        break;

      case 0x10:      /* DJNZ dd */
        if ((cpu_regs.bc -= 0x100) & 0xff00)
          cpu_regs.pc += (int8_t)GET_BYTE(cpu_regs.pc) + 1;
        else
          cpu_regs.pc++;
        break;

      case 0x11:      /* LD cpu_regs.de,nnnn */
        cpu_regs.de = GET_WORD(cpu_regs.pc);
        cpu_regs.pc += 2;
        break;

      case 0x12:      /* LD (cpu_regs.de),A */
        PUT_BYTE(cpu_regs.de, CPU_REG_GET_HIGH(cpu_regs.af));
        break;

      case 0x13:      /* INC cpu_regs.de */
        ++cpu_regs.de;
        break;

      case 0x14:      /* INC D */
        cpu_regs.de += 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.de);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
        break;

      case 0x15:      /* DEC D */
        cpu_regs.de -= 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.de);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
        break;

      case 0x16:      /* LD D,nn */
        CPU_REG_SET_HIGH(cpu_regs.de, RAM_PP(cpu_regs.pc));
        break;

      case 0x17:      /* RLA */
        cpu_regs.af = ((cpu_regs.af << 8) & 0x0100) | ((cpu_regs.af >> 7) & 0x28) | ((cpu_regs.af << 1) & ~0x01ff) |
                      (cpu_regs.af & 0xc4) | ((cpu_regs.af >> 15) & 1);
        break;

      case 0x18:      /* JR dd */
        cpu_regs.pc += (int8_t)GET_BYTE(cpu_regs.pc) + 1;
        break;

      case 0x19:      /* ADD cpu_regs.hl,cpu_regs.de */
        cpu_regs.hl &= ADDRMASK;
        cpu_regs.de &= ADDRMASK;
        sum = cpu_regs.hl + cpu_regs.de;
        cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.hl ^ cpu_regs.de ^ sum) >> 8];
        cpu_regs.hl = sum;
        break;

      case 0x1a:      /* LD A,(cpu_regs.de) */
        CPU_REG_SET_HIGH(cpu_regs.af, GET_BYTE(cpu_regs.de));
        break;

      case 0x1b:      /* DEC cpu_regs.de */
        --cpu_regs.de;
        break;

      case 0x1c:      /* INC E */
        temp = CPU_REG_GET_LOW(cpu_regs.de) + 1;
        CPU_REG_SET_LOW(cpu_regs.de, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80);
        break;

      case 0x1d:      /* DEC E */
        temp = CPU_REG_GET_LOW(cpu_regs.de) - 1;
        CPU_REG_SET_LOW(cpu_regs.de, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp & 0xff] | SET_PV2(0x7f);
        break;

      case 0x1e:      /* LD E,nn */
        CPU_REG_SET_LOW(cpu_regs.de, RAM_PP(cpu_regs.pc));
        break;

      case 0x1f:      /* RRA */
        cpu_regs.af = ((cpu_regs.af & 1) << 15) | (cpu_regs.af & 0xc4) | rraTable[CPU_REG_GET_HIGH(cpu_regs.af)];
        break;

      case 0x20:      /* JR NZ,dd */
        if (TST_FLAG(Z))
          cpu_regs.pc++;
        else
          cpu_regs.pc += (int8_t)GET_BYTE(cpu_regs.pc) + 1;
        break;

      case 0x21:      /* LD cpu_regs.hl,nnnn */
        cpu_regs.hl = GET_WORD(cpu_regs.pc);
        cpu_regs.pc += 2;
        break;

      case 0x22:      /* LD (nnnn),cpu_regs.hl */
        temp = GET_WORD(cpu_regs.pc);
        PUT_WORD(temp, cpu_regs.hl);
        cpu_regs.pc += 2;
        break;

      case 0x23:      /* INC cpu_regs.hl */
        ++cpu_regs.hl;
        break;

      case 0x24:      /* INC H */
        cpu_regs.hl += 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.hl);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
        break;

      case 0x25:      /* DEC H */
        cpu_regs.hl -= 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.hl);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
        break;

      case 0x26:      /* LD H,nn */
        CPU_REG_SET_HIGH(cpu_regs.hl, RAM_PP(cpu_regs.pc));
        break;

      case 0x27:      /* DAA */
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        temp = CPU_LOW_DIGIT(acu);
        cbits = TST_FLAG(C);
        if (TST_FLAG(N)) {   /* last operation was a subtract */
          int hd = cbits || acu > 0x99;
          if (TST_FLAG(H) || (temp > 9)) { /* adjust low digit */
            if (temp > 5)
              SET_FLAG(H, 0);
            acu -= 6;
            acu &= 0xff;
          }
          if (hd)
            acu -= 0x160;   /* adjust high digit */
        } else {          /* last operation was an add */
          if (TST_FLAG(H) || (temp > 9)) { /* adjust low digit */
            SET_FLAG(H, (temp > 9));
            acu += 6;
          }
          if (cbits || ((acu & 0x1f0) > 0x90))
            acu += 0x60;   /* adjust high digit */
        }
        cpu_regs.af = (cpu_regs.af & 0x12) | rrdrldTable[acu & 0xff] | ((acu >> 8) & 1) | cbits;
        break;

      case 0x28:      /* JR Z,dd */
        if (TST_FLAG(Z))
          cpu_regs.pc += (int8_t)GET_BYTE(cpu_regs.pc) + 1;
        else
          cpu_regs.pc++;
        break;

      case 0x29:      /* ADD cpu_regs.hl,cpu_regs.hl */
        cpu_regs.hl &= ADDRMASK;
        sum = cpu_regs.hl + cpu_regs.hl;
        cpu_regs.af = (cpu_regs.af & ~0x3b) | cbitsDup16Table[sum >> 8];
        cpu_regs.hl = sum;
        break;

      case 0x2a:      /* LD cpu_regs.hl,(nnnn) */
        temp = GET_WORD(cpu_regs.pc);
        cpu_regs.hl = GET_WORD(temp);
        cpu_regs.pc += 2;
        break;

      case 0x2b:      /* DEC cpu_regs.hl */
        --cpu_regs.hl;
        break;

      case 0x2c:      /* INC L */
        temp = CPU_REG_GET_LOW(cpu_regs.hl) + 1;
        CPU_REG_SET_LOW(cpu_regs.hl, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80);
        break;

      case 0x2d:      /* DEC L */
        temp = CPU_REG_GET_LOW(cpu_regs.hl) - 1;
        CPU_REG_SET_LOW(cpu_regs.hl, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp & 0xff] | SET_PV2(0x7f);
        break;

      case 0x2e:      /* LD L,nn */
        CPU_REG_SET_LOW(cpu_regs.hl, RAM_PP(cpu_regs.pc));
        break;

      case 0x2f:      /* CPL */
        cpu_regs.af = (~cpu_regs.af & ~0xff) | (cpu_regs.af & 0xc5) | ((~cpu_regs.af >> 8) & 0x28) | 0x12;
        break;

      case 0x30:      /* JR NC,dd */
        if (TST_FLAG(C))
          cpu_regs.pc++;
        else
          cpu_regs.pc += (int8_t)GET_BYTE(cpu_regs.pc) + 1;
        break;

      case 0x31:      /* LD cpu_regs.sp,nnnn */
        cpu_regs.sp = GET_WORD(cpu_regs.pc);
        cpu_regs.pc += 2;
        break;

      case 0x32:      /* LD (nnnn),A */
        temp = GET_WORD(cpu_regs.pc);
        PUT_BYTE(temp, CPU_REG_GET_HIGH(cpu_regs.af));
        cpu_regs.pc += 2;
        break;

      case 0x33:      /* INC cpu_regs.sp */
        ++cpu_regs.sp;
        break;

      case 0x34:      /* INC (cpu_regs.hl) */
        temp = GET_BYTE(cpu_regs.hl) + 1;
        PUT_BYTE(cpu_regs.hl, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80);
        break;

      case 0x35:      /* DEC (cpu_regs.hl) */
        temp = GET_BYTE(cpu_regs.hl) - 1;
        PUT_BYTE(cpu_regs.hl, temp);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp & 0xff] | SET_PV2(0x7f);
        break;

      case 0x36:      /* LD (cpu_regs.hl),nn */
        PUT_BYTE(cpu_regs.hl, RAM_PP(cpu_regs.pc));
        break;

      case 0x37:      /* SCF */
        cpu_regs.af = (cpu_regs.af & ~0x3b) | ((cpu_regs.af >> 8) & 0x28) | 1;
        break;

      case 0x38:      /* JR C,dd */
        if (TST_FLAG(C))
          cpu_regs.pc += (int8_t)GET_BYTE(cpu_regs.pc) + 1;
        else
          cpu_regs.pc++;
        break;

      case 0x39:      /* ADD cpu_regs.hl,cpu_regs.sp */
        cpu_regs.hl &= ADDRMASK;
        cpu_regs.sp &= ADDRMASK;
        sum = cpu_regs.hl + cpu_regs.sp;
        cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.hl ^ cpu_regs.sp ^ sum) >> 8];
        cpu_regs.hl = sum;
        break;

      case 0x3a:      /* LD A,(nnnn) */
        temp = GET_WORD(cpu_regs.pc);
        CPU_REG_SET_HIGH(cpu_regs.af, GET_BYTE(temp));
        cpu_regs.pc += 2;
        break;

      case 0x3b:      /* DEC cpu_regs.sp */
        --cpu_regs.sp;
        break;

      case 0x3c:      /* INC A */
        cpu_regs.af += 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.af);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _inc_table[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
        break;

      case 0x3d:      /* DEC A */
        cpu_regs.af -= 0x100;
        temp = CPU_REG_GET_HIGH(cpu_regs.af);
        cpu_regs.af = (cpu_regs.af & ~0xfe) | _dec_table[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
        break;

      case 0x3e:      /* LD A,nn */
        CPU_REG_SET_HIGH(cpu_regs.af, RAM_PP(cpu_regs.pc));
        break;

      case 0x3f:      /* CCF */
        cpu_regs.af = (cpu_regs.af & ~0x3b) | ((cpu_regs.af >> 8) & 0x28) | ((cpu_regs.af & 1) << 4) | (~cpu_regs.af & 1);
        break;

      case 0x40:      /* LD B,B */
        break;

      case 0x41:      /* LD B,C */
        cpu_regs.bc = (cpu_regs.bc & 0xff) | ((cpu_regs.bc & 0xff) << 8);
        break;

      case 0x42:      /* LD B,D */
        cpu_regs.bc = (cpu_regs.bc & 0xff) | (cpu_regs.de & ~0xff);
        break;

      case 0x43:      /* LD B,E */
        cpu_regs.bc = (cpu_regs.bc & 0xff) | ((cpu_regs.de & 0xff) << 8);
        break;

      case 0x44:      /* LD B,H */
        cpu_regs.bc = (cpu_regs.bc & 0xff) | (cpu_regs.hl & ~0xff);
        break;

      case 0x45:      /* LD B,L */
        cpu_regs.bc = (cpu_regs.bc & 0xff) | ((cpu_regs.hl & 0xff) << 8);
        break;

      case 0x46:      /* LD B,(cpu_regs.hl) */
        CPU_REG_SET_HIGH(cpu_regs.bc, GET_BYTE(cpu_regs.hl));
        break;

      case 0x47:      /* LD B,A */
        cpu_regs.bc = (cpu_regs.bc & 0xff) | (cpu_regs.af & ~0xff);
        break;

      case 0x48:      /* LD C,B */
        cpu_regs.bc = (cpu_regs.bc & ~0xff) | ((cpu_regs.bc >> 8) & 0xff);
        break;

      case 0x49:      /* LD C,C */
        break;

      case 0x4a:      /* LD C,D */
        cpu_regs.bc = (cpu_regs.bc & ~0xff) | ((cpu_regs.de >> 8) & 0xff);
        break;

      case 0x4b:      /* LD C,E */
        cpu_regs.bc = (cpu_regs.bc & ~0xff) | (cpu_regs.de & 0xff);
        break;

      case 0x4c:      /* LD C,H */
        cpu_regs.bc = (cpu_regs.bc & ~0xff) | ((cpu_regs.hl >> 8) & 0xff);
        break;

      case 0x4d:      /* LD C,L */
        cpu_regs.bc = (cpu_regs.bc & ~0xff) | (cpu_regs.hl & 0xff);
        break;

      case 0x4e:      /* LD C,(cpu_regs.hl) */
        CPU_REG_SET_LOW(cpu_regs.bc, GET_BYTE(cpu_regs.hl));
        break;

      case 0x4f:      /* LD C,A */
        cpu_regs.bc = (cpu_regs.bc & ~0xff) | ((cpu_regs.af >> 8) & 0xff);
        break;

      case 0x50:      /* LD D,B */
        cpu_regs.de = (cpu_regs.de & 0xff) | (cpu_regs.bc & ~0xff);
        break;

      case 0x51:      /* LD D,C */
        cpu_regs.de = (cpu_regs.de & 0xff) | ((cpu_regs.bc & 0xff) << 8);
        break;

      case 0x52:      /* LD D,D */
        break;

      case 0x53:      /* LD D,E */
        cpu_regs.de = (cpu_regs.de & 0xff) | ((cpu_regs.de & 0xff) << 8);
        break;

      case 0x54:      /* LD D,H */
        cpu_regs.de = (cpu_regs.de & 0xff) | (cpu_regs.hl & ~0xff);
        break;

      case 0x55:      /* LD D,L */
        cpu_regs.de = (cpu_regs.de & 0xff) | ((cpu_regs.hl & 0xff) << 8);
        break;

      case 0x56:      /* LD D,(cpu_regs.hl) */
        CPU_REG_SET_HIGH(cpu_regs.de, GET_BYTE(cpu_regs.hl));
        break;

      case 0x57:      /* LD D,A */
        cpu_regs.de = (cpu_regs.de & 0xff) | (cpu_regs.af & ~0xff);
        break;

      case 0x58:      /* LD E,B */
        cpu_regs.de = (cpu_regs.de & ~0xff) | ((cpu_regs.bc >> 8) & 0xff);
        break;

      case 0x59:      /* LD E,C */
        cpu_regs.de = (cpu_regs.de & ~0xff) | (cpu_regs.bc & 0xff);
        break;

      case 0x5a:      /* LD E,D */
        cpu_regs.de = (cpu_regs.de & ~0xff) | ((cpu_regs.de >> 8) & 0xff);
        break;

      case 0x5b:      /* LD E,E */
        break;

      case 0x5c:      /* LD E,H */
        cpu_regs.de = (cpu_regs.de & ~0xff) | ((cpu_regs.hl >> 8) & 0xff);
        break;

      case 0x5d:      /* LD E,L */
        cpu_regs.de = (cpu_regs.de & ~0xff) | (cpu_regs.hl & 0xff);
        break;

      case 0x5e:      /* LD E,(cpu_regs.hl) */
        CPU_REG_SET_LOW(cpu_regs.de, GET_BYTE(cpu_regs.hl));
        break;

      case 0x5f:      /* LD E,A */
        cpu_regs.de = (cpu_regs.de & ~0xff) | ((cpu_regs.af >> 8) & 0xff);
        break;

      case 0x60:      /* LD H,B */
        cpu_regs.hl = (cpu_regs.hl & 0xff) | (cpu_regs.bc & ~0xff);
        break;

      case 0x61:      /* LD H,C */
        cpu_regs.hl = (cpu_regs.hl & 0xff) | ((cpu_regs.bc & 0xff) << 8);
        break;

      case 0x62:      /* LD H,D */
        cpu_regs.hl = (cpu_regs.hl & 0xff) | (cpu_regs.de & ~0xff);
        break;

      case 0x63:      /* LD H,E */
        cpu_regs.hl = (cpu_regs.hl & 0xff) | ((cpu_regs.de & 0xff) << 8);
        break;

      case 0x64:      /* LD H,H */
        break;

      case 0x65:      /* LD H,L */
        cpu_regs.hl = (cpu_regs.hl & 0xff) | ((cpu_regs.hl & 0xff) << 8);
        break;

      case 0x66:      /* LD H,(cpu_regs.hl) */
        CPU_REG_SET_HIGH(cpu_regs.hl, GET_BYTE(cpu_regs.hl));
        break;

      case 0x67:      /* LD H,A */
        cpu_regs.hl = (cpu_regs.hl & 0xff) | (cpu_regs.af & ~0xff);
        break;

      case 0x68:      /* LD L,B */
        cpu_regs.hl = (cpu_regs.hl & ~0xff) | ((cpu_regs.bc >> 8) & 0xff);
        break;

      case 0x69:      /* LD L,C */
        cpu_regs.hl = (cpu_regs.hl & ~0xff) | (cpu_regs.bc & 0xff);
        break;

      case 0x6a:      /* LD L,D */
        cpu_regs.hl = (cpu_regs.hl & ~0xff) | ((cpu_regs.de >> 8) & 0xff);
        break;

      case 0x6b:      /* LD L,E */
        cpu_regs.hl = (cpu_regs.hl & ~0xff) | (cpu_regs.de & 0xff);
        break;

      case 0x6c:      /* LD L,H */
        cpu_regs.hl = (cpu_regs.hl & ~0xff) | ((cpu_regs.hl >> 8) & 0xff);
        break;

      case 0x6d:      /* LD L,L */
        break;

      case 0x6e:      /* LD L,(cpu_regs.hl) */
        CPU_REG_SET_LOW(cpu_regs.hl, GET_BYTE(cpu_regs.hl));
        break;

      case 0x6f:      /* LD L,A */
        cpu_regs.hl = (cpu_regs.hl & ~0xff) | ((cpu_regs.af >> 8) & 0xff);
        break;

      case 0x70:      /* LD (cpu_regs.hl),B */
        PUT_BYTE(cpu_regs.hl, CPU_REG_GET_HIGH(cpu_regs.bc));
        break;

      case 0x71:      /* LD (cpu_regs.hl),C */
        PUT_BYTE(cpu_regs.hl, CPU_REG_GET_LOW(cpu_regs.bc));
        break;

      case 0x72:      /* LD (cpu_regs.hl),D */
        PUT_BYTE(cpu_regs.hl, CPU_REG_GET_HIGH(cpu_regs.de));
        break;

      case 0x73:      /* LD (cpu_regs.hl),E */
        PUT_BYTE(cpu_regs.hl, CPU_REG_GET_LOW(cpu_regs.de));
        break;

      case 0x74:      /* LD (cpu_regs.hl),H */
        PUT_BYTE(cpu_regs.hl, CPU_REG_GET_HIGH(cpu_regs.hl));
        break;

      case 0x75:      /* LD (cpu_regs.hl),L */
        PUT_BYTE(cpu_regs.hl, CPU_REG_GET_LOW(cpu_regs.hl));
        break;

      case 0x76:      /* HALT */
#ifdef DEBUG
        pal_puts("\r\n::CPU HALTED::");	// A halt is a good indicator of broken code
        pal_puts("Press any key...");
        pal_getch();
#endif
        cpu_regs.pc--;
        goto end_decode;
        break;

      case 0x77:      /* LD (cpu_regs.hl),A */
        PUT_BYTE(cpu_regs.hl, CPU_REG_GET_HIGH(cpu_regs.af));
        break;

      case 0x78:      /* LD A,B */
        cpu_regs.af = (cpu_regs.af & 0xff) | (cpu_regs.bc & ~0xff);
        break;

      case 0x79:      /* LD A,C */
        cpu_regs.af = (cpu_regs.af & 0xff) | ((cpu_regs.bc & 0xff) << 8);
        break;

      case 0x7a:      /* LD A,D */
        cpu_regs.af = (cpu_regs.af & 0xff) | (cpu_regs.de & ~0xff);
        break;

      case 0x7b:      /* LD A,E */
        cpu_regs.af = (cpu_regs.af & 0xff) | ((cpu_regs.de & 0xff) << 8);
        break;

      case 0x7c:      /* LD A,H */
        cpu_regs.af = (cpu_regs.af & 0xff) | (cpu_regs.hl & ~0xff);
        break;

      case 0x7d:      /* LD A,L */
        cpu_regs.af = (cpu_regs.af & 0xff) | ((cpu_regs.hl & 0xff) << 8);
        break;

      case 0x7e:      /* LD A,(cpu_regs.hl) */
        CPU_REG_SET_HIGH(cpu_regs.af, GET_BYTE(cpu_regs.hl));
        break;

      case 0x7f:      /* LD A,A */
        break;

      case 0x80:      /* ADD A,B */
        temp = CPU_REG_GET_HIGH(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x81:      /* ADD A,C */
        temp = CPU_REG_GET_LOW(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x82:      /* ADD A,D */
        temp = CPU_REG_GET_HIGH(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x83:      /* ADD A,E */
        temp = CPU_REG_GET_LOW(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x84:      /* ADD A,H */
        temp = CPU_REG_GET_HIGH(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x85:      /* ADD A,L */
        temp = CPU_REG_GET_LOW(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x86:      /* ADD A,(cpu_regs.hl) */
        temp = GET_BYTE(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x87:      /* ADD A,A */
        cbits = 2 * CPU_REG_GET_HIGH(cpu_regs.af);
        cpu_regs.af = cbitsDup8Table[cbits] | (SET_PVS(cbits));
        break;

      case 0x88:      /* ADC A,B */
        temp = CPU_REG_GET_HIGH(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x89:      /* ADC A,C */
        temp = CPU_REG_GET_LOW(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x8a:      /* ADC A,D */
        temp = CPU_REG_GET_HIGH(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x8b:      /* ADC A,E */
        temp = CPU_REG_GET_LOW(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x8c:      /* ADC A,H */
        temp = CPU_REG_GET_HIGH(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x8d:      /* ADC A,L */
        temp = CPU_REG_GET_LOW(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x8e:      /* ADC A,(cpu_regs.hl) */
        temp = GET_BYTE(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0x8f:      /* ADC A,A */
        cbits = 2 * CPU_REG_GET_HIGH(cpu_regs.af) + TST_FLAG(C);
        cpu_regs.af = cbitsDup8Table[cbits] | (SET_PVS(cbits));
        break;

      case 0x90:      /* SUB B */
        temp = CPU_REG_GET_HIGH(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x91:      /* SUB C */
        temp = CPU_REG_GET_LOW(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x92:      /* SUB D */
        temp = CPU_REG_GET_HIGH(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x93:      /* SUB E */
        temp = CPU_REG_GET_LOW(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x94:      /* SUB H */
        temp = CPU_REG_GET_HIGH(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x95:      /* SUB L */
        temp = CPU_REG_GET_LOW(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x96:      /* SUB (cpu_regs.hl) */
        temp = GET_BYTE(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x97:      /* SUB A */
        cpu_regs.af = 0x42;
        break;

      case 0x98:      /* SBC A,B */
        temp = CPU_REG_GET_HIGH(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x99:      /* SBC A,C */
        temp = CPU_REG_GET_LOW(cpu_regs.bc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x9a:      /* SBC A,D */
        temp = CPU_REG_GET_HIGH(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x9b:      /* SBC A,E */
        temp = CPU_REG_GET_LOW(cpu_regs.de);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x9c:      /* SBC A,H */
        temp = CPU_REG_GET_HIGH(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x9d:      /* SBC A,L */
        temp = CPU_REG_GET_LOW(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x9e:      /* SBC A,(cpu_regs.hl) */
        temp = GET_BYTE(cpu_regs.hl);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0x9f:      /* SBC A,A */
        cbits = -TST_FLAG(C);
        cpu_regs.af = subTable[cbits & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PVS(cbits));
        break;

      case 0xa0:      /* AND B */
        cpu_regs.af = andTable[((cpu_regs.af & cpu_regs.bc) >> 8) & 0xff];
        break;

      case 0xa1:      /* AND C */
        cpu_regs.af = andTable[((cpu_regs.af >> 8) & cpu_regs.bc) & 0xff];
        break;

      case 0xa2:      /* AND D */
        cpu_regs.af = andTable[((cpu_regs.af & cpu_regs.de) >> 8) & 0xff];
        break;

      case 0xa3:      /* AND E */
        cpu_regs.af = andTable[((cpu_regs.af >> 8) & cpu_regs.de) & 0xff];
        break;

      case 0xa4:      /* AND H */
        cpu_regs.af = andTable[((cpu_regs.af & cpu_regs.hl) >> 8) & 0xff];
        break;

      case 0xa5:      /* AND L */
        cpu_regs.af = andTable[((cpu_regs.af >> 8) & cpu_regs.hl) & 0xff];
        break;

      case 0xa6:      /* AND (cpu_regs.hl) */
        cpu_regs.af = andTable[((cpu_regs.af >> 8) & GET_BYTE(cpu_regs.hl)) & 0xff];
        break;

      case 0xa7:      /* AND A */
        cpu_regs.af = andTable[(cpu_regs.af >> 8) & 0xff];
        break;

      case 0xa8:      /* XOR B */
        cpu_regs.af = xororTable[((cpu_regs.af ^ cpu_regs.bc) >> 8) & 0xff];
        break;

      case 0xa9:      /* XOR C */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ cpu_regs.bc) & 0xff];
        break;

      case 0xaa:      /* XOR D */
        cpu_regs.af = xororTable[((cpu_regs.af ^ cpu_regs.de) >> 8) & 0xff];
        break;

      case 0xab:      /* XOR E */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ cpu_regs.de) & 0xff];
        break;

      case 0xac:      /* XOR H */
        cpu_regs.af = xororTable[((cpu_regs.af ^ cpu_regs.hl) >> 8) & 0xff];
        break;

      case 0xad:      /* XOR L */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ cpu_regs.hl) & 0xff];
        break;

      case 0xae:      /* XOR (cpu_regs.hl) */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ GET_BYTE(cpu_regs.hl)) & 0xff];
        break;

      case 0xaf:      /* XOR A */
        cpu_regs.af = 0x44;
        break;

      case 0xb0:      /* OR B */
        cpu_regs.af = xororTable[((cpu_regs.af | cpu_regs.bc) >> 8) & 0xff];
        break;

      case 0xb1:      /* OR C */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) | cpu_regs.bc) & 0xff];
        break;

      case 0xb2:      /* OR D */
        cpu_regs.af = xororTable[((cpu_regs.af | cpu_regs.de) >> 8) & 0xff];
        break;

      case 0xb3:      /* OR E */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) | cpu_regs.de) & 0xff];
        break;

      case 0xb4:      /* OR H */
        cpu_regs.af = xororTable[((cpu_regs.af | cpu_regs.hl) >> 8) & 0xff];
        break;

      case 0xb5:      /* OR L */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) | cpu_regs.hl) & 0xff];
        break;

      case 0xb6:      /* OR (cpu_regs.hl) */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) | GET_BYTE(cpu_regs.hl)) & 0xff];
        break;

      case 0xb7:      /* OR A */
        cpu_regs.af = xororTable[(cpu_regs.af >> 8) & 0xff];
        break;

      case 0xb8:      /* CP B */
        temp = CPU_REG_GET_HIGH(cpu_regs.bc);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xb9:      /* CP C */
        temp = CPU_REG_GET_LOW(cpu_regs.bc);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xba:      /* CP D */
        temp = CPU_REG_GET_HIGH(cpu_regs.de);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xbb:      /* CP E */
        temp = CPU_REG_GET_LOW(cpu_regs.de);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xbc:      /* CP H */
        temp = CPU_REG_GET_HIGH(cpu_regs.hl);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xbd:      /* CP L */
        temp = CPU_REG_GET_LOW(cpu_regs.hl);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xbe:      /* CP (cpu_regs.hl) */
        temp = GET_BYTE(cpu_regs.hl);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xbf:      /* CP A */
        CPU_REG_SET_LOW(cpu_regs.af, (CPU_REG_GET_HIGH(cpu_regs.af) & 0x28) | 0x42);
        break;

      case 0xc0:      /* RET NZ */
        if (!(TST_FLAG(Z)))
          POP(cpu_regs.pc);
        break;

      case 0xc1:      /* POP cpu_regs.bc */
        POP(cpu_regs.bc);
        break;

      case 0xc2:      /* JP NZ,nnnn */
        JPC(!TST_FLAG(Z));
        break;

      case 0xc3:      /* JP nnnn */
        JPC(1);
        break;

      case 0xc4:      /* CALL NZ,nnnn */
        CALLC(!TST_FLAG(Z));
        break;

      case 0xc5:      /* PUSH cpu_regs.bc */
        PUSH(cpu_regs.bc);
        break;

      case 0xc6:      /* ADD A,nn */
        temp = RAM_PP(cpu_regs.pc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0xc7:      /* RST 0 */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 0;
        break;

      case 0xc8:      /* RET Z */
        if (TST_FLAG(Z))
          POP(cpu_regs.pc);
        break;

      case 0xc9:      /* RET */
        POP(cpu_regs.pc);
        break;

      case 0xca:      /* JP Z,nnnn */
        JPC(TST_FLAG(Z));
        break;

      case 0xcb:      /* CB prefix */
        adr = cpu_regs.hl;
        switch ((op = GET_BYTE(cpu_regs.pc)) & 7) {

          case 0:
            ++cpu_regs.pc;
            acu = CPU_REG_GET_HIGH(cpu_regs.bc);
            break;

          case 1:
            ++cpu_regs.pc;
            acu = CPU_REG_GET_LOW(cpu_regs.bc);
            break;

          case 2:
            ++cpu_regs.pc;
            acu = CPU_REG_GET_HIGH(cpu_regs.de);
            break;

          case 3:
            ++cpu_regs.pc;
            acu = CPU_REG_GET_LOW(cpu_regs.de);
            break;

          case 4:
            ++cpu_regs.pc;
            acu = CPU_REG_GET_HIGH(cpu_regs.hl);
            break;

          case 5:
            ++cpu_regs.pc;
            acu = CPU_REG_GET_LOW(cpu_regs.hl);
            break;

          case 6:
            ++cpu_regs.pc;
            acu = GET_BYTE(adr);
            break;

          case 7:
            ++cpu_regs.pc;
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
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
                temp = (acu << 1) | TST_FLAG(C);
                cbits = acu & 0x80;
                goto cbshflg1;

              case 0x18:/* RR */
                temp = (acu >> 1) | (TST_FLAG(C) << 7);
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
                cpu_regs.af = (cpu_regs.af & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
            }
            break;

          case 0x40:  /* BIT */
            if (acu & (1 << ((op >> 3) & 7)))
              cpu_regs.af = (cpu_regs.af & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
            else
              cpu_regs.af = (cpu_regs.af & ~0xfe) | 0x54;
            if ((op & 7) != 6)
              cpu_regs.af |= (acu & 0x28);
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
            CPU_REG_SET_HIGH(cpu_regs.bc, temp);
            break;

          case 1:
            CPU_REG_SET_LOW(cpu_regs.bc, temp);
            break;

          case 2:
            CPU_REG_SET_HIGH(cpu_regs.de, temp);
            break;

          case 3:
            CPU_REG_SET_LOW(cpu_regs.de, temp);
            break;

          case 4:
            CPU_REG_SET_HIGH(cpu_regs.hl, temp);
            break;

          case 5:
            CPU_REG_SET_LOW(cpu_regs.hl, temp);
            break;

          case 6:
            PUT_BYTE(adr, temp);
            break;

          case 7:
            CPU_REG_SET_HIGH(cpu_regs.af, temp);
            break;
        }
        break;

      case 0xcc:      /* CALL Z,nnnn */
        CALLC(TST_FLAG(Z));
        break;

      case 0xcd:      /* CALL nnnn */
        CALLC(1);
        break;

      case 0xce:      /* ADC A,nn */
        temp = RAM_PP(cpu_regs.pc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu + temp + TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = addTable[sum] | cbitsTable[cbits] | (SET_PV);
        break;

      case 0xcf:      /* RST 8 */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 8;
        break;

      case 0xd0:      /* RET NC */
        if (!(TST_FLAG(C)))
          POP(cpu_regs.pc);
        break;

      case 0xd1:      /* POP cpu_regs.de */
        POP(cpu_regs.de);
        break;

      case 0xd2:      /* JP NC,nnnn */
        JPC(!TST_FLAG(C));
        break;

      case 0xd3:      /* OUT (nn),A */
        cpu_out(RAM_PP(cpu_regs.pc), CPU_REG_GET_HIGH(cpu_regs.af));
        break;

      case 0xd4:      /* CALL NC,nnnn */
        CALLC(!TST_FLAG(C));
        break;

      case 0xd5:      /* PUSH cpu_regs.de */
        PUSH(cpu_regs.de);
        break;

      case 0xd6:      /* SUB nn */
        temp = RAM_PP(cpu_regs.pc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0xd7:      /* RST 10H */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 0x10;
        break;

      case 0xd8:      /* RET C */
        if (TST_FLAG(C))
          POP(cpu_regs.pc);
        break;

      case 0xd9:      /* EXX */
        temp = cpu_regs.bc;
        cpu_regs.bc = cpu_regs.bc1;
        cpu_regs.bc1 = temp;
        temp = cpu_regs.de;
        cpu_regs.de = cpu_regs.de1;
        cpu_regs.de1 = temp;
        temp = cpu_regs.hl;
        cpu_regs.hl = cpu_regs.hl1;
        cpu_regs.hl1 = temp;
        break;

      case 0xda:      /* JP C,nnnn */
        JPC(TST_FLAG(C));
        break;

      case 0xdb:      /* IN A,(nn) */
        CPU_REG_SET_HIGH(cpu_regs.af, cpu_in(RAM_PP(cpu_regs.pc)));
        break;

      case 0xdc:      /* CALL C,nnnn */
        CALLC(TST_FLAG(C));
        break;

      case 0xdd:      /* DD prefix */
        switch (RAM_PP(cpu_regs.pc)) {

          case 0x09:      /* ADD cpu_regs.ix,cpu_regs.bc */
            cpu_regs.ix &= ADDRMASK;
            cpu_regs.bc &= ADDRMASK;
            sum = cpu_regs.ix + cpu_regs.bc;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.ix ^ cpu_regs.bc ^ sum) >> 8];
            cpu_regs.ix = sum;
            break;

          case 0x19:      /* ADD cpu_regs.ix,cpu_regs.de */
            cpu_regs.ix &= ADDRMASK;
            cpu_regs.de &= ADDRMASK;
            sum = cpu_regs.ix + cpu_regs.de;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.ix ^ cpu_regs.de ^ sum) >> 8];
            cpu_regs.ix = sum;
            break;

          case 0x21:      /* LD cpu_regs.ix,nnnn */
            cpu_regs.ix = GET_WORD(cpu_regs.pc);
            cpu_regs.pc += 2;
            break;

          case 0x22:      /* LD (nnnn),cpu_regs.ix */
            temp = GET_WORD(cpu_regs.pc);
            PUT_WORD(temp, cpu_regs.ix);
            cpu_regs.pc += 2;
            break;

          case 0x23:      /* INC cpu_regs.ix */
            ++cpu_regs.ix;
            break;

          case 0x24:      /* INC IXH */
            cpu_regs.ix += 0x100;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | incZ80Table[CPU_REG_GET_HIGH(cpu_regs.ix)];
            break;

          case 0x25:      /* DEC IXH */
            cpu_regs.ix -= 0x100;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | decZ80Table[CPU_REG_GET_HIGH(cpu_regs.ix)];
            break;

          case 0x26:      /* LD IXH,nn */
            CPU_REG_SET_HIGH(cpu_regs.ix, RAM_PP(cpu_regs.pc));
            break;

          case 0x29:      /* ADD cpu_regs.ix,cpu_regs.ix */
            cpu_regs.ix &= ADDRMASK;
            sum = cpu_regs.ix + cpu_regs.ix;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | cbitsDup16Table[sum >> 8];
            cpu_regs.ix = sum;
            break;

          case 0x2a:      /* LD cpu_regs.ix,(nnnn) */
            temp = GET_WORD(cpu_regs.pc);
            cpu_regs.ix = GET_WORD(temp);
            cpu_regs.pc += 2;
            break;

          case 0x2b:      /* DEC cpu_regs.ix */
            --cpu_regs.ix;
            break;

          case 0x2c:      /* INC IXL */
            temp = CPU_REG_GET_LOW(cpu_regs.ix) + 1;
            CPU_REG_SET_LOW(cpu_regs.ix, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | incZ80Table[temp];
            break;

          case 0x2d:      /* DEC IXL */
            temp = CPU_REG_GET_LOW(cpu_regs.ix) - 1;
            CPU_REG_SET_LOW(cpu_regs.ix, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
            break;

          case 0x2e:      /* LD IXL,nn */
            CPU_REG_SET_LOW(cpu_regs.ix, RAM_PP(cpu_regs.pc));
            break;

          case 0x34:      /* INC (cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr) + 1;
            PUT_BYTE(adr, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | incZ80Table[temp];
            break;

          case 0x35:      /* DEC (cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr) - 1;
            PUT_BYTE(adr, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
            break;

          case 0x36:      /* LD (cpu_regs.ix+dd),nn */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, RAM_PP(cpu_regs.pc));
            break;

          case 0x39:      /* ADD cpu_regs.ix,cpu_regs.sp */
            cpu_regs.ix &= ADDRMASK;
            cpu_regs.sp &= ADDRMASK;
            sum = cpu_regs.ix + cpu_regs.sp;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.ix ^ cpu_regs.sp ^ sum) >> 8];
            cpu_regs.ix = sum;
            break;

          case 0x44:      /* LD B,IXH */
            CPU_REG_SET_HIGH(cpu_regs.bc, CPU_REG_GET_HIGH(cpu_regs.ix));
            break;

          case 0x45:      /* LD B,IXL */
            CPU_REG_SET_HIGH(cpu_regs.bc, CPU_REG_GET_LOW(cpu_regs.ix));
            break;

          case 0x46:      /* LD B,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.bc, GET_BYTE(adr));
            break;

          case 0x4c:      /* LD C,IXH */
            CPU_REG_SET_LOW(cpu_regs.bc, CPU_REG_GET_HIGH(cpu_regs.ix));
            break;

          case 0x4d:      /* LD C,IXL */
            CPU_REG_SET_LOW(cpu_regs.bc, CPU_REG_GET_LOW(cpu_regs.ix));
            break;

          case 0x4e:      /* LD C,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_LOW(cpu_regs.bc, GET_BYTE(adr));
            break;

          case 0x54:      /* LD D,IXH */
            CPU_REG_SET_HIGH(cpu_regs.de, CPU_REG_GET_HIGH(cpu_regs.ix));
            break;

          case 0x55:      /* LD D,IXL */
            CPU_REG_SET_HIGH(cpu_regs.de, CPU_REG_GET_LOW(cpu_regs.ix));
            break;

          case 0x56:      /* LD D,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.de, GET_BYTE(adr));
            break;

          case 0x5c:      /* LD E,IXH */
            CPU_REG_SET_LOW(cpu_regs.de, CPU_REG_GET_HIGH(cpu_regs.ix));
            break;

          case 0x5d:      /* LD E,IXL */
            CPU_REG_SET_LOW(cpu_regs.de, CPU_REG_GET_LOW(cpu_regs.ix));
            break;

          case 0x5e:      /* LD E,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_LOW(cpu_regs.de, GET_BYTE(adr));
            break;

          case 0x60:      /* LD IXH,B */
            CPU_REG_SET_HIGH(cpu_regs.ix, CPU_REG_GET_HIGH(cpu_regs.bc));
            break;

          case 0x61:      /* LD IXH,C */
            CPU_REG_SET_HIGH(cpu_regs.ix, CPU_REG_GET_LOW(cpu_regs.bc));
            break;

          case 0x62:      /* LD IXH,D */
            CPU_REG_SET_HIGH(cpu_regs.ix, CPU_REG_GET_HIGH(cpu_regs.de));
            break;

          case 0x63:      /* LD IXH,E */
            CPU_REG_SET_HIGH(cpu_regs.ix, CPU_REG_GET_LOW(cpu_regs.de));
            break;

          case 0x64:      /* LD IXH,IXH */
            break;

          case 0x65:      /* LD IXH,IXL */
            CPU_REG_SET_HIGH(cpu_regs.ix, CPU_REG_GET_LOW(cpu_regs.ix));
            break;

          case 0x66:      /* LD H,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.hl, GET_BYTE(adr));
            break;

          case 0x67:      /* LD IXH,A */
            CPU_REG_SET_HIGH(cpu_regs.ix, CPU_REG_GET_HIGH(cpu_regs.af));
            break;

          case 0x68:      /* LD IXL,B */
            CPU_REG_SET_LOW(cpu_regs.ix, CPU_REG_GET_HIGH(cpu_regs.bc));
            break;

          case 0x69:      /* LD IXL,C */
            CPU_REG_SET_LOW(cpu_regs.ix, CPU_REG_GET_LOW(cpu_regs.bc));
            break;

          case 0x6a:      /* LD IXL,D */
            CPU_REG_SET_LOW(cpu_regs.ix, CPU_REG_GET_HIGH(cpu_regs.de));
            break;

          case 0x6b:      /* LD IXL,E */
            CPU_REG_SET_LOW(cpu_regs.ix, CPU_REG_GET_LOW(cpu_regs.de));
            break;

          case 0x6c:      /* LD IXL,IXH */
            CPU_REG_SET_LOW(cpu_regs.ix, CPU_REG_GET_HIGH(cpu_regs.ix));
            break;

          case 0x6d:      /* LD IXL,IXL */
            break;

          case 0x6e:      /* LD L,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_LOW(cpu_regs.hl, GET_BYTE(adr));
            break;

          case 0x6f:      /* LD IXL,A */
            CPU_REG_SET_LOW(cpu_regs.ix, CPU_REG_GET_HIGH(cpu_regs.af));
            break;

          case 0x70:      /* LD (cpu_regs.ix+dd),B */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.bc));
            break;

          case 0x71:      /* LD (cpu_regs.ix+dd),C */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_LOW(cpu_regs.bc));
            break;

          case 0x72:      /* LD (cpu_regs.ix+dd),D */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.de));
            break;

          case 0x73:      /* LD (cpu_regs.ix+dd),E */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_LOW(cpu_regs.de));
            break;

          case 0x74:      /* LD (cpu_regs.ix+dd),H */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.hl));
            break;

          case 0x75:      /* LD (cpu_regs.ix+dd),L */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_LOW(cpu_regs.hl));
            break;

          case 0x77:      /* LD (cpu_regs.ix+dd),A */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.af));
            break;

          case 0x7c:      /* LD A,IXH */
            CPU_REG_SET_HIGH(cpu_regs.af, CPU_REG_GET_HIGH(cpu_regs.ix));
            break;

          case 0x7d:      /* LD A,IXL */
            CPU_REG_SET_HIGH(cpu_regs.af, CPU_REG_GET_LOW(cpu_regs.ix));
            break;

          case 0x7e:      /* LD A,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.af, GET_BYTE(adr));
            break;

          case 0x84:      /* ADD A,IXH */
            temp = CPU_REG_GET_HIGH(cpu_regs.ix);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp;
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x85:      /* ADD A,IXL */
            temp = CPU_REG_GET_LOW(cpu_regs.ix);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp;
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x86:      /* ADD A,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp;
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x8c:      /* ADC A,IXH */
            temp = CPU_REG_GET_HIGH(cpu_regs.ix);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp + TST_FLAG(C);
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x8d:      /* ADC A,IXL */
            temp = CPU_REG_GET_LOW(cpu_regs.ix);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp + TST_FLAG(C);
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x8e:      /* ADC A,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp + TST_FLAG(C);
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x96:      /* SUB (cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0x94:      /* SUB IXH */
            SET_FLAG(C, 0);/* fall through, a bit less efficient but smaller code */

          case 0x9c:      /* SBC A,IXH */
            temp = CPU_REG_GET_HIGH(cpu_regs.ix);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp - TST_FLAG(C);
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0x95:      /* SUB IXL */
            SET_FLAG(C, 0);/* fall through, a bit less efficient but smaller code */

          case 0x9d:      /* SBC A,IXL */
            temp = CPU_REG_GET_LOW(cpu_regs.ix);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp - TST_FLAG(C);
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0x9e:      /* SBC A,(cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp - TST_FLAG(C);
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xa4:      /* AND IXH */
            cpu_regs.af = andTable[((cpu_regs.af & cpu_regs.ix) >> 8) & 0xff];
            break;

          case 0xa5:      /* AND IXL */
            cpu_regs.af = andTable[((cpu_regs.af >> 8) & cpu_regs.ix) & 0xff];
            break;

          case 0xa6:      /* AND (cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            cpu_regs.af = andTable[((cpu_regs.af >> 8) & GET_BYTE(adr)) & 0xff];
            break;

          case 0xac:      /* XOR IXH */
            cpu_regs.af = xororTable[((cpu_regs.af ^ cpu_regs.ix) >> 8) & 0xff];
            break;

          case 0xad:      /* XOR IXL */
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ cpu_regs.ix) & 0xff];
            break;

          case 0xae:      /* XOR (cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ GET_BYTE(adr)) & 0xff];
            break;

          case 0xb4:      /* OR IXH */
            cpu_regs.af = xororTable[((cpu_regs.af | cpu_regs.ix) >> 8) & 0xff];
            break;

          case 0xb5:      /* OR IXL */
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) | cpu_regs.ix) & 0xff];
            break;

          case 0xb6:      /* OR (cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) | GET_BYTE(adr)) & 0xff];
            break;

          case 0xbc:      /* CP IXH */
            temp = CPU_REG_GET_HIGH(cpu_regs.ix);
            cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                          cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xbd:      /* CP IXL */
            temp = CPU_REG_GET_LOW(cpu_regs.ix);
            cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                          cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xbe:      /* CP (cpu_regs.ix+dd) */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                          cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xcb:      /* CB prefix */
            adr = cpu_regs.ix + (int8_t)RAM_PP(cpu_regs.pc);
            switch ((op = GET_BYTE(cpu_regs.pc)) & 7) {

              case 0:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.bc);
                break;

              case 1:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_LOW(cpu_regs.bc);
                break;

              case 2:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.de);
                break;

              case 3:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_LOW(cpu_regs.de);
                break;

              case 4:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.hl);
                break;

              case 5:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_LOW(cpu_regs.hl);
                break;

              case 6:
                ++cpu_regs.pc;
                acu = GET_BYTE(adr);
                break;

              case 7:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.af);
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
                    temp = (acu << 1) | TST_FLAG(C);
                    cbits = acu & 0x80;
                    goto cbshflg2;

                  case 0x18:/* RR */
                    temp = (acu >> 1) | (TST_FLAG(C) << 7);
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
                    cpu_regs.af = (cpu_regs.af & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
                }
                break;

              case 0x40:  /* BIT */
                if (acu & (1 << ((op >> 3) & 7)))
                  cpu_regs.af = (cpu_regs.af & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
                else
                  cpu_regs.af = (cpu_regs.af & ~0xfe) | 0x54;
                if ((op & 7) != 6)
                  cpu_regs.af |= (acu & 0x28);
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
                CPU_REG_SET_HIGH(cpu_regs.bc, temp);
                break;

              case 1:
                CPU_REG_SET_LOW(cpu_regs.bc, temp);
                break;

              case 2:
                CPU_REG_SET_HIGH(cpu_regs.de, temp);
                break;

              case 3:
                CPU_REG_SET_LOW(cpu_regs.de, temp);
                break;

              case 4:
                CPU_REG_SET_HIGH(cpu_regs.hl, temp);
                break;

              case 5:
                CPU_REG_SET_LOW(cpu_regs.hl, temp);
                break;

              case 6:
                PUT_BYTE(adr, temp);
                break;

              case 7:
                CPU_REG_SET_HIGH(cpu_regs.af, temp);
                break;
            }
            break;

          case 0xe1:      /* POP cpu_regs.ix */
            POP(cpu_regs.ix);
            break;

          case 0xe3:      /* EX (cpu_regs.sp),cpu_regs.ix */
            temp = cpu_regs.ix;
            POP(cpu_regs.ix);
            PUSH(temp);
            break;

          case 0xe5:      /* PUSH cpu_regs.ix */
            PUSH(cpu_regs.ix);
            break;

          case 0xe9:      /* JP (cpu_regs.ix) */
            cpu_regs.pc = cpu_regs.ix;
            break;

          case 0xf9:      /* LD cpu_regs.sp,cpu_regs.ix */
            cpu_regs.sp = cpu_regs.ix;
            break;

          default:                /* ignore DD */
            cpu_regs.pc--;
        }
        break;

      case 0xde:          /* SBC A,nn */
        temp = RAM_PP(cpu_regs.pc);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp - TST_FLAG(C);
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
        break;

      case 0xdf:      /* RST 18H */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 0x18;
        break;

      case 0xe0:      /* RET PO */
        if (!(TST_FLAG(P)))
          POP(cpu_regs.pc);
        break;

      case 0xe1:      /* POP cpu_regs.hl */
        POP(cpu_regs.hl);
        break;

      case 0xe2:      /* JP PO,nnnn */
        JPC(!TST_FLAG(P));
        break;

      case 0xe3:      /* EX (cpu_regs.sp),cpu_regs.hl */
        temp = cpu_regs.hl;
        POP(cpu_regs.hl);
        PUSH(temp);
        break;

      case 0xe4:      /* CALL PO,nnnn */
        CALLC(!TST_FLAG(P));
        break;

      case 0xe5:      /* PUSH cpu_regs.hl */
        PUSH(cpu_regs.hl);
        break;

      case 0xe6:      /* AND nn */
        cpu_regs.af = andTable[((cpu_regs.af >> 8) & RAM_PP(cpu_regs.pc)) & 0xff];
        break;

      case 0xe7:      /* RST 20H */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 0x20;
        break;

      case 0xe8:      /* RET PE */
        if (TST_FLAG(P))
          POP(cpu_regs.pc);
        break;

      case 0xe9:      /* JP (cpu_regs.hl) */
        cpu_regs.pc = cpu_regs.hl;
        break;

      case 0xea:      /* JP PE,nnnn */
        JPC(TST_FLAG(P));
        break;

      case 0xeb:      /* EX cpu_regs.de,cpu_regs.hl */
        temp = cpu_regs.hl;
        cpu_regs.hl = cpu_regs.de;
        cpu_regs.de = temp;
        break;

      case 0xec:      /* CALL PE,nnnn */
        CALLC(TST_FLAG(P));
        break;

      case 0xed:      /* ED prefix */
        switch (RAM_PP(cpu_regs.pc)) {

          case 0x40:      /* IN B,(C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_HIGH(cpu_regs.bc, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x41:      /* OUT (C),B */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), CPU_REG_GET_HIGH(cpu_regs.bc));
            break;

          case 0x42:      /* SBC cpu_regs.hl,cpu_regs.bc */
            cpu_regs.hl &= ADDRMASK;
            cpu_regs.bc &= ADDRMASK;
            sum = cpu_regs.hl - cpu_regs.bc - TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                          cbits2Z80Table[((cpu_regs.hl ^ cpu_regs.bc ^ sum) >> 8) & 0x1ff];
            cpu_regs.hl = sum;
            break;

          case 0x43:      /* LD (nnnn),cpu_regs.bc */
            temp = GET_WORD(cpu_regs.pc);
            PUT_WORD(temp, cpu_regs.bc);
            cpu_regs.pc += 2;
            break;

          case 0x44:      /* NEG */

          case 0x4C:      /* NEG, unofficial */

          case 0x54:      /* NEG, unofficial */

          case 0x5C:      /* NEG, unofficial */

          case 0x64:      /* NEG, unofficial */

          case 0x6C:      /* NEG, unofficial */

          case 0x74:      /* NEG, unofficial */

          case 0x7C:      /* NEG, unofficial */
            temp = CPU_REG_GET_HIGH(cpu_regs.af);
            cpu_regs.af = ((~(cpu_regs.af & 0xff00) + 1) & 0xff00); /* cpu_regs.af = (-(cpu_regs.af & 0xff00) & 0xff00); */
            cpu_regs.af |= ((cpu_regs.af >> 8) & 0xa8) | (((cpu_regs.af & 0xff00) == 0) << 6) | negTable[temp];
            break;

          case 0x45:      /* RETN */

          case 0x55:      /* RETN, unofficial */

          case 0x5D:      /* RETN, unofficial */

          case 0x65:      /* RETN, unofficial */

          case 0x6D:      /* RETN, unofficial */

          case 0x75:      /* RETN, unofficial */

          case 0x7D:      /* RETN, unofficial */
            cpu_regs.iff |= cpu_regs.iff >> 1;
            POP(cpu_regs.pc);
            break;

          case 0x46:      /* IM 0 */
            /* interrupt mode 0 */
            break;

          case 0x47:      /* LD I,A */
            cpu_regs.ir = (cpu_regs.ir & 0xff) | (cpu_regs.af & ~0xff);
            break;

          case 0x48:      /* IN C,(C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_LOW(cpu_regs.bc, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x49:      /* OUT (C),C */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), CPU_REG_GET_LOW(cpu_regs.bc));
            break;

          case 0x4a:      /* ADC cpu_regs.hl,cpu_regs.bc */
            cpu_regs.hl &= ADDRMASK;
            cpu_regs.bc &= ADDRMASK;
            sum = cpu_regs.hl + cpu_regs.bc + TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                          cbitsZ80Table[(cpu_regs.hl ^ cpu_regs.bc ^ sum) >> 8];
            cpu_regs.hl = sum;
            break;

          case 0x4b:      /* LD cpu_regs.bc,(nnnn) */
            temp = GET_WORD(cpu_regs.pc);
            cpu_regs.bc = GET_WORD(temp);
            cpu_regs.pc += 2;
            break;

          case 0x4d:      /* RETI */
            cpu_regs.iff |= cpu_regs.iff >> 1;
            POP(cpu_regs.pc);
            break;

          case 0x4f:      /* LD R,A */
            cpu_regs.ir = (cpu_regs.ir & ~0xff) | ((cpu_regs.af >> 8) & 0xff);
            break;

          case 0x50:      /* IN D,(C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_HIGH(cpu_regs.de, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x51:      /* OUT (C),D */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), CPU_REG_GET_HIGH(cpu_regs.de));
            break;

          case 0x52:      /* SBC cpu_regs.hl,cpu_regs.de */
            cpu_regs.hl &= ADDRMASK;
            cpu_regs.de &= ADDRMASK;
            sum = cpu_regs.hl - cpu_regs.de - TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                          cbits2Z80Table[((cpu_regs.hl ^ cpu_regs.de ^ sum) >> 8) & 0x1ff];
            cpu_regs.hl = sum;
            break;

          case 0x53:      /* LD (nnnn),cpu_regs.de */
            temp = GET_WORD(cpu_regs.pc);
            PUT_WORD(temp, cpu_regs.de);
            cpu_regs.pc += 2;
            break;

          case 0x56:      /* IM 1 */
            /* interrupt mode 1 */
            break;

          case 0x57:      /* LD A,I */
            cpu_regs.af = (cpu_regs.af & 0x29) | (cpu_regs.ir & ~0xff) | ((cpu_regs.ir >> 8) & 0x80) | (((cpu_regs.ir & ~0xff) == 0) << 6) | ((cpu_regs.iff & 2) << 1);
            break;

          case 0x58:      /* IN E,(C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_LOW(cpu_regs.de, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x59:      /* OUT (C),E */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), CPU_REG_GET_LOW(cpu_regs.de));
            break;

          case 0x5a:      /* ADC cpu_regs.hl,cpu_regs.de */
            cpu_regs.hl &= ADDRMASK;
            cpu_regs.de &= ADDRMASK;
            sum = cpu_regs.hl + cpu_regs.de + TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                          cbitsZ80Table[(cpu_regs.hl ^ cpu_regs.de ^ sum) >> 8];
            cpu_regs.hl = sum;
            break;

          case 0x5b:      /* LD cpu_regs.de,(nnnn) */
            temp = GET_WORD(cpu_regs.pc);
            cpu_regs.de = GET_WORD(temp);
            cpu_regs.pc += 2;
            break;

          case 0x5e:      /* IM 2 */
            /* interrupt mode 2 */
            break;

          case 0x5f:      /* LD A,R */
            cpu_regs.af = (cpu_regs.af & 0x29) | ((cpu_regs.ir & 0xff) << 8) | (cpu_regs.ir & 0x80) |
                          (((cpu_regs.ir & 0xff) == 0) << 6) | ((cpu_regs.iff & 2) << 1);
            break;

          case 0x60:      /* IN H,(C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_HIGH(cpu_regs.hl, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x61:      /* OUT (C),H */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), CPU_REG_GET_HIGH(cpu_regs.hl));
            break;

          case 0x62:      /* SBC cpu_regs.hl,cpu_regs.hl */
            cpu_regs.hl &= ADDRMASK;
            sum = cpu_regs.hl - cpu_regs.hl - TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | (((sum & ADDRMASK) == 0) << 6) |
                          cbits2Z80DupTable[(sum >> 8) & 0x1ff];
            cpu_regs.hl = sum;
            break;

          case 0x63:      /* LD (nnnn),cpu_regs.hl */
            temp = GET_WORD(cpu_regs.pc);
            PUT_WORD(temp, cpu_regs.hl);
            cpu_regs.pc += 2;
            break;

          case 0x67:      /* RRD */
            temp = GET_BYTE(cpu_regs.hl);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            PUT_BYTE(cpu_regs.hl, CPU_HIGH_DIGIT(temp) | (CPU_LOW_DIGIT(acu) << 4));
            cpu_regs.af = rrdrldTable[(acu & 0xf0) | CPU_LOW_DIGIT(temp)] | (cpu_regs.af & 1);
            break;

          case 0x68:      /* IN L,(C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_LOW(cpu_regs.hl, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x69:      /* OUT (C),L */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), CPU_REG_GET_LOW(cpu_regs.hl));
            break;

          case 0x6a:      /* ADC cpu_regs.hl,cpu_regs.hl */
            cpu_regs.hl &= ADDRMASK;
            sum = cpu_regs.hl + cpu_regs.hl + TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | (((sum & ADDRMASK) == 0) << 6) |
                          cbitsZ80DupTable[sum >> 8];
            cpu_regs.hl = sum;
            break;

          case 0x6b:      /* LD cpu_regs.hl,(nnnn) */
            temp = GET_WORD(cpu_regs.pc);
            cpu_regs.hl = GET_WORD(temp);
            cpu_regs.pc += 2;
            break;

          case 0x6f:      /* RLD */
            temp = GET_BYTE(cpu_regs.hl);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            PUT_BYTE(cpu_regs.hl, (CPU_LOW_DIGIT(temp) << 4) | CPU_LOW_DIGIT(acu));
            cpu_regs.af = rrdrldTable[(acu & 0xf0) | CPU_HIGH_DIGIT(temp)] | (cpu_regs.af & 1);
            break;

          case 0x70:      /* IN (C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_LOW(temp, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x71:      /* OUT (C),0 */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), 0);
            break;

          case 0x72:      /* SBC cpu_regs.hl,cpu_regs.sp */
            cpu_regs.hl &= ADDRMASK;
            cpu_regs.sp &= ADDRMASK;
            sum = cpu_regs.hl - cpu_regs.sp - TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                          cbits2Z80Table[((cpu_regs.hl ^ cpu_regs.sp ^ sum) >> 8) & 0x1ff];
            cpu_regs.hl = sum;
            break;

          case 0x73:      /* LD (nnnn),cpu_regs.sp */
            temp = GET_WORD(cpu_regs.pc);
            PUT_WORD(temp, cpu_regs.sp);
            cpu_regs.pc += 2;
            break;

          case 0x78:      /* IN A,(C) */
            temp = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            CPU_REG_SET_HIGH(cpu_regs.af, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | rotateShiftTable[temp & 0xff];
            break;

          case 0x79:      /* OUT (C),A */
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), CPU_REG_GET_HIGH(cpu_regs.af));
            break;

          case 0x7a:      /* ADC cpu_regs.hl,cpu_regs.sp */
            cpu_regs.hl &= ADDRMASK;
            cpu_regs.sp &= ADDRMASK;
            sum = cpu_regs.hl + cpu_regs.sp + TST_FLAG(C);
            cpu_regs.af = (cpu_regs.af & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                          cbitsZ80Table[(cpu_regs.hl ^ cpu_regs.sp ^ sum) >> 8];
            cpu_regs.hl = sum;
            break;

          case 0x7b:      /* LD cpu_regs.sp,(nnnn) */
            temp = GET_WORD(cpu_regs.pc);
            cpu_regs.sp = GET_WORD(temp);
            cpu_regs.pc += 2;
            break;

          case 0xa0:      /* LDI */
            acu = RAM_PP(cpu_regs.hl);
            PUT_BYTE_PP(cpu_regs.de, acu);
            acu += CPU_REG_GET_HIGH(cpu_regs.af);
            cpu_regs.af = (cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
                          (((--cpu_regs.bc & ADDRMASK) != 0) << 2);
            break;

          case 0xa1:      /* CPI */
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            temp = RAM_PP(cpu_regs.hl);
            sum = acu - temp;
            cbits = acu ^ temp ^ sum;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                          (((sum - ((cbits & 16) >> 4)) & 2) << 4) | (cbits & 16) |
                          ((sum - ((cbits >> 4) & 1)) & 8) |
                          ((--cpu_regs.bc & ADDRMASK) != 0) << 2 | 2;
            if ((sum & 15) == 8 && (cbits & 16) != 0)
              cpu_regs.af &= ~8;
            break;

          /*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
            NF flag A is copy of bit 7 of the value read from or written to an I/O port.
            INI/INIR/IND/INDR use the C flag in stead of the L register. There is a
            catch though, because not the value of C is used, but C + 1 if it's INI/INIR or
            C - 1 if it's IND/INDR. So, first of all INI/INIR:
            HF and CF Both set if ((cpu_regs.hl) + ((C + 1) & 255) > 255)
            PF The parity of (((cpu_regs.hl) + ((C + 1) & 255)) & 7) xor B)                      */
          case 0xa2:      /* INI */
            acu = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            PUT_BYTE(cpu_regs.hl, acu);
            ++cpu_regs.hl;
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            cpu_regs.bc -= 0x100;
            INOUTFLAGS_NONZERO((CPU_REG_GET_LOW(cpu_regs.bc) + 1) & 0xff);
            break;

          /*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
            NF flag A is copy of bit 7 of the value read from or written to an I/O port.
            And now the for OUTI/OTIR/OUTD/OTDR instructions. Take state of the L
            after the increment or decrement of cpu_regs.hl; add the value written to the I/O port
            to; call that k for now. If k > 255, then the CF and HF flags are set. The PF
            flags is set like the parity of k bitwise and'ed with 7, bitwise xor'ed with B.
            HF and CF Both set if ((cpu_regs.hl) + L > 255)
            PF The parity of ((((cpu_regs.hl) + L) & 7) xor B)                                       */
          case 0xa3:      /* OUTI */
            acu = GET_BYTE(cpu_regs.hl);
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), acu);
            ++cpu_regs.hl;
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            cpu_regs.bc -= 0x100;
            INOUTFLAGS_NONZERO(CPU_REG_GET_LOW(cpu_regs.hl));
            break;

          case 0xa8:      /* LDD */
            acu = RAM_MM(cpu_regs.hl);
            PUT_BYTE_MM(cpu_regs.de, acu);
            acu += CPU_REG_GET_HIGH(cpu_regs.af);
            cpu_regs.af = (cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
                          (((--cpu_regs.bc & ADDRMASK) != 0) << 2);
            break;

          case 0xa9:      /* CPD */
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            temp = RAM_MM(cpu_regs.hl);
            sum = acu - temp;
            cbits = acu ^ temp ^ sum;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                          (((sum - ((cbits & 16) >> 4)) & 2) << 4) | (cbits & 16) |
                          ((sum - ((cbits >> 4) & 1)) & 8) |
                          ((--cpu_regs.bc & ADDRMASK) != 0) << 2 | 2;
            if ((sum & 15) == 8 && (cbits & 16) != 0)
              cpu_regs.af &= ~8;
            break;

          /*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
            NF flag A is copy of bit 7 of the value read from or written to an I/O port.
            INI/INIR/IND/INDR use the C flag in stead of the L register. There is a
            catch though, because not the value of C is used, but C + 1 if it's INI/INIR or
            C - 1 if it's IND/INDR. And last IND/INDR:
            HF and CF Both set if ((cpu_regs.hl) + ((C - 1) & 255) > 255)
            PF The parity of (((cpu_regs.hl) + ((C - 1) & 255)) & 7) xor B)                      */
          case 0xaa:      /* IND */
            acu = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
            PUT_BYTE(cpu_regs.hl, acu);
            --cpu_regs.hl;
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            cpu_regs.bc -= 0x100;
            INOUTFLAGS_NONZERO((CPU_REG_GET_LOW(cpu_regs.bc) - 1) & 0xff);
            break;

          case 0xab:      /* OUTD */
            acu = GET_BYTE(cpu_regs.hl);
            cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), acu);
            --cpu_regs.hl;
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            cpu_regs.bc -= 0x100;
            INOUTFLAGS_NONZERO(CPU_REG_GET_LOW(cpu_regs.hl));
            break;

          case 0xb0:      /* LDIR */
            cpu_regs.bc &= ADDRMASK;
            if (cpu_regs.bc == 0)
              cpu_regs.bc = 0x10000;
            do {
              acu = RAM_PP(cpu_regs.hl);
              PUT_BYTE_PP(cpu_regs.de, acu);
            } while (--cpu_regs.bc);
            acu += CPU_REG_GET_HIGH(cpu_regs.af);
            cpu_regs.af = (cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
            break;

          case 0xb1:      /* CPIR */
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            cpu_regs.bc &= ADDRMASK;
            if (cpu_regs.bc == 0)
              cpu_regs.bc = 0x10000;
            do {
              temp = RAM_PP(cpu_regs.hl);
              op = --cpu_regs.bc != 0;
              sum = acu - temp;
            } while (op && sum != 0);
            cbits = acu ^ temp ^ sum;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                          (((sum - ((cbits & 16) >> 4)) & 2) << 4) |
                          (cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
                          op << 2 | 2;
            if ((sum & 15) == 8 && (cbits & 16) != 0)
              cpu_regs.af &= ~8;
            break;

          case 0xb2:      /* INIR */
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            if (temp == 0)
              temp = 0x100;
            do {
              acu = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
              PUT_BYTE(cpu_regs.hl, acu);
              ++cpu_regs.hl;
            } while (--temp);
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            CPU_REG_SET_HIGH(cpu_regs.bc, 0);
            INOUTFLAGS_ZERO((CPU_REG_GET_LOW(cpu_regs.bc) + 1) & 0xff);
            break;

          case 0xb3:      /* OTIR */
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            if (temp == 0)
              temp = 0x100;
            do {
              acu = GET_BYTE(cpu_regs.hl);
              cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), acu);
              ++cpu_regs.hl;
            } while (--temp);
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            CPU_REG_SET_HIGH(cpu_regs.bc, 0);
            INOUTFLAGS_ZERO(CPU_REG_GET_LOW(cpu_regs.hl));
            break;

          case 0xb8:      /* LDDR */
            cpu_regs.bc &= ADDRMASK;
            if (cpu_regs.bc == 0)
              cpu_regs.bc = 0x10000;
            do {
              acu = RAM_MM(cpu_regs.hl);
              PUT_BYTE_MM(cpu_regs.de, acu);
            } while (--cpu_regs.bc);
            acu += CPU_REG_GET_HIGH(cpu_regs.af);
            cpu_regs.af = (cpu_regs.af & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
            break;

          case 0xb9:      /* CPDR */
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            cpu_regs.bc &= ADDRMASK;
            if (cpu_regs.bc == 0)
              cpu_regs.bc = 0x10000;
            do {
              temp = RAM_MM(cpu_regs.hl);
              op = --cpu_regs.bc != 0;
              sum = acu - temp;
            } while (op && sum != 0);
            cbits = acu ^ temp ^ sum;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                          (((sum - ((cbits & 16) >> 4)) & 2) << 4) |
                          (cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
                          op << 2 | 2;
            if ((sum & 15) == 8 && (cbits & 16) != 0)
              cpu_regs.af &= ~8;
            break;

          case 0xba:      /* INDR */
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            if (temp == 0)
              temp = 0x100;
            do {
              acu = cpu_in(CPU_REG_GET_LOW(cpu_regs.bc));
              PUT_BYTE(cpu_regs.hl, acu);
              --cpu_regs.hl;
            } while (--temp);
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            CPU_REG_SET_HIGH(cpu_regs.bc, 0);
            INOUTFLAGS_ZERO((CPU_REG_GET_LOW(cpu_regs.bc) - 1) & 0xff);
            break;

          case 0xbb:      /* OTDR */
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            if (temp == 0)
              temp = 0x100;
            do {
              acu = GET_BYTE(cpu_regs.hl);
              cpu_out(CPU_REG_GET_LOW(cpu_regs.bc), acu);
              --cpu_regs.hl;
            } while (--temp);
            temp = CPU_REG_GET_HIGH(cpu_regs.bc);
            CPU_REG_SET_HIGH(cpu_regs.bc, 0);
            INOUTFLAGS_ZERO(CPU_REG_GET_LOW(cpu_regs.hl));
            break;

          default:    /* ignore ED and following byte */
            break;
        }
        break;

      case 0xee:      /* XOR nn */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ RAM_PP(cpu_regs.pc)) & 0xff];
        break;

      case 0xef:      /* RST 28H */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 0x28;
        break;

      case 0xf0:      /* RET P */
        if (!(TST_FLAG(S)))
          POP(cpu_regs.pc);
        break;

      case 0xf1:      /* POP cpu_regs.af */
        POP(cpu_regs.af);
        break;

      case 0xf2:      /* JP P,nnnn */
        JPC(!TST_FLAG(S));
        break;

      case 0xf3:      /* DI */
        cpu_regs.iff = 0;
        break;

      case 0xf4:      /* CALL P,nnnn */
        CALLC(!TST_FLAG(S));
        break;

      case 0xf5:      /* PUSH cpu_regs.af */
        PUSH(cpu_regs.af);
        break;

      case 0xf6:      /* OR nn */
        cpu_regs.af = xororTable[((cpu_regs.af >> 8) | RAM_PP(cpu_regs.pc)) & 0xff];
        break;

      case 0xf7:      /* RST 30H */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 0x30;
        break;

      case 0xf8:      /* RET M */
        if (TST_FLAG(S))
          POP(cpu_regs.pc);
        break;

      case 0xf9:      /* LD cpu_regs.sp,cpu_regs.hl */
        cpu_regs.sp = cpu_regs.hl;
        break;

      case 0xfa:      /* JP M,nnnn */
        JPC(TST_FLAG(S));
        break;

      case 0xfb:      /* EI */
        cpu_regs.iff = 3;
        break;

      case 0xfc:      /* CALL M,nnnn */
        CALLC(TST_FLAG(S));
        break;

      case 0xfd:      /* FD prefix */
        switch (RAM_PP(cpu_regs.pc)) {

          case 0x09:      /* ADD cpu_regs.iy,cpu_regs.bc */
            cpu_regs.iy &= ADDRMASK;
            cpu_regs.bc &= ADDRMASK;
            sum = cpu_regs.iy + cpu_regs.bc;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.iy ^ cpu_regs.bc ^ sum) >> 8];
            cpu_regs.iy = sum;
            break;

          case 0x19:      /* ADD cpu_regs.iy,cpu_regs.de */
            cpu_regs.iy &= ADDRMASK;
            cpu_regs.de &= ADDRMASK;
            sum = cpu_regs.iy + cpu_regs.de;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.iy ^ cpu_regs.de ^ sum) >> 8];
            cpu_regs.iy = sum;
            break;

          case 0x21:      /* LD cpu_regs.iy,nnnn */
            cpu_regs.iy = GET_WORD(cpu_regs.pc);
            cpu_regs.pc += 2;
            break;

          case 0x22:      /* LD (nnnn),cpu_regs.iy */
            temp = GET_WORD(cpu_regs.pc);
            PUT_WORD(temp, cpu_regs.iy);
            cpu_regs.pc += 2;
            break;

          case 0x23:      /* INC cpu_regs.iy */
            ++cpu_regs.iy;
            break;

          case 0x24:      /* INC IYH */
            cpu_regs.iy += 0x100;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | incZ80Table[CPU_REG_GET_HIGH(cpu_regs.iy)];
            break;

          case 0x25:      /* DEC IYH */
            cpu_regs.iy -= 0x100;
            cpu_regs.af = (cpu_regs.af & ~0xfe) | decZ80Table[CPU_REG_GET_HIGH(cpu_regs.iy)];
            break;

          case 0x26:      /* LD IYH,nn */
            CPU_REG_SET_HIGH(cpu_regs.iy, RAM_PP(cpu_regs.pc));
            break;

          case 0x29:      /* ADD cpu_regs.iy,cpu_regs.iy */
            cpu_regs.iy &= ADDRMASK;
            sum = cpu_regs.iy + cpu_regs.iy;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | cbitsDup16Table[sum >> 8];
            cpu_regs.iy = sum;
            break;

          case 0x2a:      /* LD cpu_regs.iy,(nnnn) */
            temp = GET_WORD(cpu_regs.pc);
            cpu_regs.iy = GET_WORD(temp);
            cpu_regs.pc += 2;
            break;

          case 0x2b:      /* DEC cpu_regs.iy */
            --cpu_regs.iy;
            break;

          case 0x2c:      /* INC IYL */
            temp = CPU_REG_GET_LOW(cpu_regs.iy) + 1;
            CPU_REG_SET_LOW(cpu_regs.iy, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | incZ80Table[temp];
            break;

          case 0x2d:      /* DEC IYL */
            temp = CPU_REG_GET_LOW(cpu_regs.iy) - 1;
            CPU_REG_SET_LOW(cpu_regs.iy, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
            break;

          case 0x2e:      /* LD IYL,nn */
            CPU_REG_SET_LOW(cpu_regs.iy, RAM_PP(cpu_regs.pc));
            break;

          case 0x34:      /* INC (cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr) + 1;
            PUT_BYTE(adr, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | incZ80Table[temp];
            break;

          case 0x35:      /* DEC (cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr) - 1;
            PUT_BYTE(adr, temp);
            cpu_regs.af = (cpu_regs.af & ~0xfe) | decZ80Table[temp & 0xff];
            break;

          case 0x36:      /* LD (cpu_regs.iy+dd),nn */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, RAM_PP(cpu_regs.pc));
            break;

          case 0x39:      /* ADD cpu_regs.iy,cpu_regs.sp */
            cpu_regs.iy &= ADDRMASK;
            cpu_regs.sp &= ADDRMASK;
            sum = cpu_regs.iy + cpu_regs.sp;
            cpu_regs.af = (cpu_regs.af & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(cpu_regs.iy ^ cpu_regs.sp ^ sum) >> 8];
            cpu_regs.iy = sum;
            break;

          case 0x44:      /* LD B,IYH */
            CPU_REG_SET_HIGH(cpu_regs.bc, CPU_REG_GET_HIGH(cpu_regs.iy));
            break;

          case 0x45:      /* LD B,IYL */
            CPU_REG_SET_HIGH(cpu_regs.bc, CPU_REG_GET_LOW(cpu_regs.iy));
            break;

          case 0x46:      /* LD B,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.bc, GET_BYTE(adr));
            break;

          case 0x4c:      /* LD C,IYH */
            CPU_REG_SET_LOW(cpu_regs.bc, CPU_REG_GET_HIGH(cpu_regs.iy));
            break;

          case 0x4d:      /* LD C,IYL */
            CPU_REG_SET_LOW(cpu_regs.bc, CPU_REG_GET_LOW(cpu_regs.iy));
            break;

          case 0x4e:      /* LD C,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_LOW(cpu_regs.bc, GET_BYTE(adr));
            break;

          case 0x54:      /* LD D,IYH */
            CPU_REG_SET_HIGH(cpu_regs.de, CPU_REG_GET_HIGH(cpu_regs.iy));
            break;

          case 0x55:      /* LD D,IYL */
            CPU_REG_SET_HIGH(cpu_regs.de, CPU_REG_GET_LOW(cpu_regs.iy));
            break;

          case 0x56:      /* LD D,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.de, GET_BYTE(adr));
            break;

          case 0x5c:      /* LD E,IYH */
            CPU_REG_SET_LOW(cpu_regs.de, CPU_REG_GET_HIGH(cpu_regs.iy));
            break;

          case 0x5d:      /* LD E,IYL */
            CPU_REG_SET_LOW(cpu_regs.de, CPU_REG_GET_LOW(cpu_regs.iy));
            break;

          case 0x5e:      /* LD E,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_LOW(cpu_regs.de, GET_BYTE(adr));
            break;

          case 0x60:      /* LD IYH,B */
            CPU_REG_SET_HIGH(cpu_regs.iy, CPU_REG_GET_HIGH(cpu_regs.bc));
            break;

          case 0x61:      /* LD IYH,C */
            CPU_REG_SET_HIGH(cpu_regs.iy, CPU_REG_GET_LOW(cpu_regs.bc));
            break;

          case 0x62:      /* LD IYH,D */
            CPU_REG_SET_HIGH(cpu_regs.iy, CPU_REG_GET_HIGH(cpu_regs.de));
            break;

          case 0x63:      /* LD IYH,E */
            CPU_REG_SET_HIGH(cpu_regs.iy, CPU_REG_GET_LOW(cpu_regs.de));
            break;

          case 0x64:      /* LD IYH,IYH */
            break;

          case 0x65:      /* LD IYH,IYL */
            CPU_REG_SET_HIGH(cpu_regs.iy, CPU_REG_GET_LOW(cpu_regs.iy));
            break;

          case 0x66:      /* LD H,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.hl, GET_BYTE(adr));
            break;

          case 0x67:      /* LD IYH,A */
            CPU_REG_SET_HIGH(cpu_regs.iy, CPU_REG_GET_HIGH(cpu_regs.af));
            break;

          case 0x68:      /* LD IYL,B */
            CPU_REG_SET_LOW(cpu_regs.iy, CPU_REG_GET_HIGH(cpu_regs.bc));
            break;

          case 0x69:      /* LD IYL,C */
            CPU_REG_SET_LOW(cpu_regs.iy, CPU_REG_GET_LOW(cpu_regs.bc));
            break;

          case 0x6a:      /* LD IYL,D */
            CPU_REG_SET_LOW(cpu_regs.iy, CPU_REG_GET_HIGH(cpu_regs.de));
            break;

          case 0x6b:      /* LD IYL,E */
            CPU_REG_SET_LOW(cpu_regs.iy, CPU_REG_GET_LOW(cpu_regs.de));
            break;

          case 0x6c:      /* LD IYL,IYH */
            CPU_REG_SET_LOW(cpu_regs.iy, CPU_REG_GET_HIGH(cpu_regs.iy));
            break;

          case 0x6d:      /* LD IYL,IYL */
            break;

          case 0x6e:      /* LD L,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_LOW(cpu_regs.hl, GET_BYTE(adr));
            break;

          case 0x6f:      /* LD IYL,A */
            CPU_REG_SET_LOW(cpu_regs.iy, CPU_REG_GET_HIGH(cpu_regs.af));
            break;

          case 0x70:      /* LD (cpu_regs.iy+dd),B */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.bc));
            break;

          case 0x71:      /* LD (cpu_regs.iy+dd),C */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_LOW(cpu_regs.bc));
            break;

          case 0x72:      /* LD (cpu_regs.iy+dd),D */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.de));
            break;

          case 0x73:      /* LD (cpu_regs.iy+dd),E */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_LOW(cpu_regs.de));
            break;

          case 0x74:      /* LD (cpu_regs.iy+dd),H */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.hl));
            break;

          case 0x75:      /* LD (cpu_regs.iy+dd),L */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_LOW(cpu_regs.hl));
            break;

          case 0x77:      /* LD (cpu_regs.iy+dd),A */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            PUT_BYTE(adr, CPU_REG_GET_HIGH(cpu_regs.af));
            break;

          case 0x7c:      /* LD A,IYH */
            CPU_REG_SET_HIGH(cpu_regs.af, CPU_REG_GET_HIGH(cpu_regs.iy));
            break;

          case 0x7d:      /* LD A,IYL */
            CPU_REG_SET_HIGH(cpu_regs.af, CPU_REG_GET_LOW(cpu_regs.iy));
            break;

          case 0x7e:      /* LD A,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            CPU_REG_SET_HIGH(cpu_regs.af, GET_BYTE(adr));
            break;

          case 0x84:      /* ADD A,IYH */
            temp = CPU_REG_GET_HIGH(cpu_regs.iy);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp;
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x85:      /* ADD A,IYL */
            temp = CPU_REG_GET_LOW(cpu_regs.iy);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp;
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x86:      /* ADD A,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp;
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x8c:      /* ADC A,IYH */
            temp = CPU_REG_GET_HIGH(cpu_regs.iy);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp + TST_FLAG(C);
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x8d:      /* ADC A,IYL */
            temp = CPU_REG_GET_LOW(cpu_regs.iy);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp + TST_FLAG(C);
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x8e:      /* ADC A,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu + temp + TST_FLAG(C);
            cpu_regs.af = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
            break;

          case 0x96:      /* SUB (cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0x94:      /* SUB IYH */
            SET_FLAG(C, 0);/* fall through, a bit less efficient but smaller code */

          case 0x9c:      /* SBC A,IYH */
            temp = CPU_REG_GET_HIGH(cpu_regs.iy);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp - TST_FLAG(C);
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0x95:      /* SUB IYL */
            SET_FLAG(C, 0);/* fall through, a bit less efficient but smaller code */

          case 0x9d:      /* SBC A,IYL */
            temp = CPU_REG_GET_LOW(cpu_regs.iy);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp - TST_FLAG(C);
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0x9e:      /* SBC A,(cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp - TST_FLAG(C);
            cpu_regs.af = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xa4:      /* AND IYH */
            cpu_regs.af = andTable[((cpu_regs.af & cpu_regs.iy) >> 8) & 0xff];
            break;

          case 0xa5:      /* AND IYL */
            cpu_regs.af = andTable[((cpu_regs.af >> 8) & cpu_regs.iy) & 0xff];
            break;

          case 0xa6:      /* AND (cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            cpu_regs.af = andTable[((cpu_regs.af >> 8) & GET_BYTE(adr)) & 0xff];
            break;

          case 0xac:      /* XOR IYH */
            cpu_regs.af = xororTable[((cpu_regs.af ^ cpu_regs.iy) >> 8) & 0xff];
            break;

          case 0xad:      /* XOR IYL */
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ cpu_regs.iy) & 0xff];
            break;

          case 0xae:      /* XOR (cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) ^ GET_BYTE(adr)) & 0xff];
            break;

          case 0xb4:      /* OR IYH */
            cpu_regs.af = xororTable[((cpu_regs.af | cpu_regs.iy) >> 8) & 0xff];
            break;

          case 0xb5:      /* OR IYL */
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) | cpu_regs.iy) & 0xff];
            break;

          case 0xb6:      /* OR (cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            cpu_regs.af = xororTable[((cpu_regs.af >> 8) | GET_BYTE(adr)) & 0xff];
            break;

          case 0xbc:      /* CP IYH */
            temp = CPU_REG_GET_HIGH(cpu_regs.iy);
            cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                          cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xbd:      /* CP IYL */
            temp = CPU_REG_GET_LOW(cpu_regs.iy);
            cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                          cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xbe:      /* CP (cpu_regs.iy+dd) */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            temp = GET_BYTE(adr);
            cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
            acu = CPU_REG_GET_HIGH(cpu_regs.af);
            sum = acu - temp;
            cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                          cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
            break;

          case 0xcb:      /* CB prefix */
            adr = cpu_regs.iy + (int8_t)RAM_PP(cpu_regs.pc);
            switch ((op = GET_BYTE(cpu_regs.pc)) & 7) {

              case 0:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.bc);
                break;

              case 1:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_LOW(cpu_regs.bc);
                break;

              case 2:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.de);
                break;

              case 3:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_LOW(cpu_regs.de);
                break;

              case 4:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.hl);
                break;

              case 5:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_LOW(cpu_regs.hl);
                break;

              case 6:
                ++cpu_regs.pc;
                acu = GET_BYTE(adr);
                break;

              case 7:
                ++cpu_regs.pc;
                acu = CPU_REG_GET_HIGH(cpu_regs.af);
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
                    temp = (acu << 1) | TST_FLAG(C);
                    cbits = acu & 0x80;
                    goto cbshflg3;

                  case 0x18:/* RR */
                    temp = (acu >> 1) | (TST_FLAG(C) << 7);
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
                    cpu_regs.af = (cpu_regs.af & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
                }
                break;

              case 0x40:  /* BIT */
                if (acu & (1 << ((op >> 3) & 7)))
                  cpu_regs.af = (cpu_regs.af & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
                else
                  cpu_regs.af = (cpu_regs.af & ~0xfe) | 0x54;
                if ((op & 7) != 6)
                  cpu_regs.af |= (acu & 0x28);
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
                CPU_REG_SET_HIGH(cpu_regs.bc, temp);
                break;

              case 1:
                CPU_REG_SET_LOW(cpu_regs.bc, temp);
                break;

              case 2:
                CPU_REG_SET_HIGH(cpu_regs.de, temp);
                break;

              case 3:
                CPU_REG_SET_LOW(cpu_regs.de, temp);
                break;

              case 4:
                CPU_REG_SET_HIGH(cpu_regs.hl, temp);
                break;

              case 5:
                CPU_REG_SET_LOW(cpu_regs.hl, temp);
                break;

              case 6:
                PUT_BYTE(adr, temp);
                break;

              case 7:
                CPU_REG_SET_HIGH(cpu_regs.af, temp);
                break;
            }
            break;

          case 0xe1:      /* POP cpu_regs.iy */
            POP(cpu_regs.iy);
            break;

          case 0xe3:      /* EX (cpu_regs.sp),cpu_regs.iy */
            temp = cpu_regs.iy;
            POP(cpu_regs.iy);
            PUSH(temp);
            break;

          case 0xe5:      /* PUSH cpu_regs.iy */
            PUSH(cpu_regs.iy);
            break;

          case 0xe9:      /* JP (cpu_regs.iy) */
            cpu_regs.pc = cpu_regs.iy;
            break;

          case 0xf9:      /* LD cpu_regs.sp,cpu_regs.iy */
            cpu_regs.sp = cpu_regs.iy;
            break;

          default:            /* ignore FD */
            cpu_regs.pc--;
        }
        break;

      case 0xfe:      /* CP nn */
        temp = RAM_PP(cpu_regs.pc);
        cpu_regs.af = (cpu_regs.af & ~0x28) | (temp & 0x28);
        acu = CPU_REG_GET_HIGH(cpu_regs.af);
        sum = acu - temp;
        cbits = acu ^ temp ^ sum;
        cpu_regs.af = (cpu_regs.af & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                      (SET_PV) | cbits2Table[cbits & 0x1ff];
        break;

      case 0xff:      /* RST 38H */
        PUSH(cpu_regs.pc);
        cpu_regs.pc = 0x38;
    }
  }
end_decode:
  ;
}
