#ifndef CPM_H
#define CPM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
extern void cpm_bdos(void);
extern void cpm_bios(void);
extern void cpm_patch(void);
extern void cpm_banner(void);
#ifdef __cplusplus
}
#endif

#endif
