#ifndef _PAL_H
#define _PAL_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif
    extern void _console_init(void);
    extern void _console_reset(void);
    extern uint8_t _getch(void);
    extern uint8_t _getche(void);
    extern void _clrscr(void);
    extern void _pal_puts(const char *str);
    void _putch(uint8_t ch);
    extern void _putcon(uint8_t ch);
    extern void _puthex8(uint8_t c);
    extern void _puthex16(uint16_t c);
    extern uint8_t _sys_exists(uint8_t *filename);
    extern FILE* _sys_fopen_r(uint8_t *filename);
    extern FILE* _sys_fopen_w(uint8_t *filename);
    extern FILE* _sys_fopen_rw(uint8_t *filename);
    extern long _sys_filesize(uint8_t *filename);
    extern int _pal_open_file(uint8_t *filename);
    extern int _pal_make_file(uint8_t *filename);
    extern int _sys_select(uint8_t *disk);
    extern uint8_t _sys_writeseq(uint8_t *filename, long fpos);
    extern uint8_t _sys_readseq(uint8_t *filename, long fpos);
    extern uint8_t _sys_readrand(uint8_t *filename, long fpos);
    extern uint8_t _sys_writerand(uint8_t *filename, long fpos);
    extern int _sys_renamefile(uint8_t *filename, uint8_t *newname);
    extern int _sys_deletefile(uint8_t *filename);
    extern uint8_t _sys_truncate(char *fn, uint8_t rc);
    extern uint8_t _pal_ram_load(uint8_t *filename, uint16_t address);
    extern void _sys_logbuffer(uint8_t *buffer);
    extern int _kbhit(void);
    extern uint8_t _chready(void);
    extern uint8_t _getchNB(void);
    extern void _sys_make_userdir();
    extern uint8_t _pal_file_match(uint8_t *fcbname, uint8_t *pattern);
    extern uint8_t _dir_findnext(uint8_t isdir);
    extern uint8_t _dir_findfirst(uint8_t isdir);
#ifdef __cplusplus
}
#endif

#endif
