#ifndef CPM_H
#define CPM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
	extern void _cpm_bdos(void);
    extern void _cpm_bios(void);
    extern void _cpm_patch(void);
#ifdef __cplusplus
}
#endif

#endif
