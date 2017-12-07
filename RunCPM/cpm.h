#ifndef CPM_H
#define CPM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
extern uint8_t cpm_init(void);
extern void cpm_bdos(void);
extern void cpm_bios(void);
extern void cpm_loop(void);
#ifdef __cplusplus
}
#endif

#endif
