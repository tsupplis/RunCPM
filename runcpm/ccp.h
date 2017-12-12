#ifndef _CCP_H
#define _CCP_H

#ifdef EMULATOR_CCP_INTERNAL
extern unsigned char ccp_bin[];
extern unsigned int ccp_len;
#endif

#ifdef EMULATOR_CCP_EMULATED

#ifdef __cplusplus
extern "C"
{
#endif
extern void ccp(void);
#ifdef __cplusplus
}
#endif

#endif

#endif
