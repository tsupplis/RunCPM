#ifndef _CPU_H
#define _CPU_H

#include <stdint.h>

typedef struct _cpu_regs_t {
    int32_t de;
    int32_t bc;
    int32_t af;
    int32_t hl;
    int32_t pcx;
    int32_t ix;
    int32_t iy;
    int32_t pc;
    int32_t sp;
    int32_t af1;
    int32_t bc1;
    int32_t de1;
    int32_t hl1;
    int32_t iff;
    int32_t ir;
} cpu_regs_t;

extern cpu_regs_t _cpu_regs;

extern int32_t _cpu_status; /* Status of the CPU 0=running 1=end request 2=back to CCP */
extern int32_t _cpu_debug;
extern int32_t _cpu_break;
extern int32_t _cpu_step;

#define CPU_LOW_DIGIT(x)            ((x) & 0xf)
#define CPU_HIGH_DIGIT(x)           (((x) >> 4) & 0xf)
#define CPU_REG_GET_LOW(x)         (uint16_t)((x) & 0xff)
#define CPU_REG_GET_HIGH(x)        (uint16_t)(((x) >> 8) & 0xff)

#define CPU_REG_SET_LOW(x, v)  x = (((x) & 0xff00) | ((v) & 0xff))
#define CPU_REG_SET_HIGH(x, v) x = (((x) & 0xff) | (((v) & 0xff) << 8))

#define CPU_WORD16(x)	((uint16_t)((x) & 0xffff))

#ifdef __cplusplus
extern "C"
{
#endif
	extern void _cpu_reset(void);
	extern void _cpu_run(void);
#ifdef __cplusplus
}
#endif

#endif
