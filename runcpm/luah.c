#include "defaults.h"

#ifdef EMULATOR_HAS_LUA

#include "globals.h"
#include "cpu.h"
#include "cpm.h"
#include "disk.h"
#include "pal.h"
#include "ram.h"


#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "lua.h"

static lua_State *L;

// Lua "Trampoline" functions
static int luah_bdos_call(lua_State *L) {
	uint8_t function = (uint8_t)luaL_checkinteger(L, 1);
	uint16_t de = (uint16_t)luaL_checkinteger(L, 2);

	CPU_REG_SET_LOW(cpu_regs.bc, function);
	cpu_regs.de = de;
	cpm_bdos();
	uint16_t result = cpu_regs.hl & 0xffff;

	lua_pushinteger(L, result);
	return(1);
}

static int lua_ram_read(lua_State *L) {
	uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);

	uint8_t result = ram_read(addr);
	lua_pushinteger(L, result);
	return(1);
}

static int lua_ram_write(lua_State *L) {
	uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
	uint8_t value = (uint8_t)luaL_checkinteger(L, 2);

	ram_write(addr, value);
	return(0);
}

static int lua_ram_read16(lua_State *L) {
	uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);

	uint16_t result = ram_read16(addr);
	lua_pushinteger(L, result);
	return(1);
}

static int lua_ram_write16(lua_State *L) {
	uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
	uint16_t value = (uint8_t)luaL_checkinteger(L, 2);

	ram_write16(addr, value);
	return(0);
}

static int lua_read_reg(lua_State *L) {
	uint8_t reg = (uint8_t)luaL_checkinteger(L, 1);
	uint16_t result;

	switch (reg) {
	case 0:
		result = cpu_regs.pcx & 0xffff;	break;	/* external view of PC                          */
	case 1:
		result = cpu_regs.af & 0xffff;	break;	/* AF register                                  */
	case 2:
		result = cpu_regs.bc & 0xffff;	break;	/* BC register                                  */
	case 3:
		result = cpu_regs.de & 0xffff;	break;	/* DE register                                  */
	case 4:
		result = cpu_regs.hl & 0xffff;	break;	/* HL register                                  */
	case 5:
		result = cpu_regs.ix & 0xffff;	break;	/* IX register                                  */
	case 6:
		result = cpu_regs.iy & 0xffff;	break;	/* IY register                                  */
	case 7:
		result = cpu_regs.pc & 0xffff;	break;	/* program counter                              */
	case 8:
		result = cpu_regs.sp & 0xffff;	break;	/* SP register                                  */
	case 9:
		result = cpu_regs.af1 & 0xffff;	break;	/* alternate AF register                        */
	case 10:
		result = cpu_regs.bc1 & 0xffff;	break;	/* alternate BC register                        */
	case 11:
		result = cpu_regs.de1 & 0xffff;	break;	/* alternate DE register                        */
	case 12:
		result = cpu_regs.hl1 & 0xffff;	break;	/* alternate HL register                        */
	case 13:
		result = cpu_regs.iff & 0xffff;	break;	/* Interrupt Flip Flop                          */
	case 14:
		result = cpu_regs.ir & 0xffff;	break;	/* Interrupt (upper) / Refresh (lower) register */
	default:
		result = -1;	break;
	}

	lua_pushinteger(L, result);
	return(1);
}

static int lua_write_reg(lua_State *L) {
	uint8_t reg = (uint8_t)luaL_checkinteger(L, 1);
	uint16_t value = (uint8_t)luaL_checkinteger(L, 2);

	switch (reg) {
	case 0:
		cpu_regs.pcx = value;	break;	/* external view of PC                          */
	case 1:
		cpu_regs.af = value;		break;	/* AF register                                  */
	case 2:
		cpu_regs.bc = value;		break;	/* BC register                                  */
	case 3:
		cpu_regs.de = value;		break;	/* DE register                                  */
	case 4:
		cpu_regs.hl = value;		break;	/* HL register                                  */
	case 5:
		cpu_regs.ix = value;		break;	/* IX register                                  */
	case 6:
		cpu_regs.iy = value;		break;	/* IY register                                  */
	case 7:
		cpu_regs.pc = value;		break;	/* program counter                              */
	case 8:
		cpu_regs.sp = value;		break;	/* SP register                                  */
	case 9:
		cpu_regs.af1 = value;	break;	/* alternate AF register                        */
	case 10:
		cpu_regs.bc1 = value;	break;	/* alternate BC register                        */
	case 11:
		cpu_regs.de1 = value;	break;	/* alternate DE register                        */
	case 12:
		cpu_regs.hl1 = value;	break;	/* alternate HL register                        */
	case 13:
		cpu_regs.iff = value;	break;	/* Interrupt Flip Flop                          */
	case 14:
		cpu_regs.ir = value;		break;	/* Interrupt (upper) / Refresh (lower) register */
	default:
		break;
	}

	return(0);
}


static uint8_t luah_run_script(char *filename) {

	L = luaL_newstate();
	luaL_openlibs(L);

	// Register Lua functions
	lua_register(L, "BdosCall", luah_bdos_call);
	lua_register(L, "RamRead", lua_ram_read);
	lua_register(L, "RamWrite", lua_ram_write);
	lua_register(L, "RamRead16", lua_ram_read16);
	lua_register(L, "RamWrite16", lua_ram_write16);
	lua_register(L, "ReadReg", lua_read_reg);
	lua_register(L, "WriteReg", lua_write_reg);

	int result = luaL_loadfile(L, filename);
	if (result) {
		pal_puts(lua_tostring(L, -1));
	} else {
		result = lua_pcall(L, 0, LUA_MULTRET, 0);
		if (result)
			pal_puts(lua_tostring(L, -1));
	}

	lua_close(L);

	return(result);
}


uint8_t luah_run(uint16_t fcbaddr) {
	uint8_t luascript[17];
	uint8_t result = 0xff;

	if (fcb_to_hostname(fcbaddr, &luascript[0])) {	// Script name must be unique
		if (!disk_search_first(fcbaddr, 0)) {			// and must exist
			result = luah_run_script((char*)&luascript[0]);
		}
	}

	return(result);
}

#endif
