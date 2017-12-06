#ifndef _PAL_H
#define _PAL_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif
    extern void pal_console_init(void);
    extern void pal_console_reset(void);
    extern uint8_t pal_getch(void);
    extern uint8_t pal_getche(void);
    extern void pal_clrscr(void);
    extern void pal_puts(const char *str);
    void pal_putch(uint8_t ch);
    extern void pal_put_con(uint8_t ch);
    extern void pal_put_hex8(uint8_t c);
    extern void pal_put_hex16(uint16_t c);
    extern uint8_t pal_file_exists(uint8_t *filename);
    extern FILE* pal_fopen_r(uint8_t *filename);
    extern FILE* pal_fopen_w(uint8_t *filename);
    extern FILE* pal_fopen_rw(uint8_t *filename);
    extern long pal_file_size(uint8_t *filename);
    extern int pal_open_file(uint8_t *filename);
    extern int pal_make_file(uint8_t *filename);
    extern int pal_select(uint8_t *disk);
    extern uint8_t pal_write_seq(uint8_t *filename, long fpos);
    extern uint8_t pal_read_seq(uint8_t *filename, long fpos);
    extern uint8_t pal_read_rand(uint8_t *filename, long fpos);
    extern uint8_t pal_write_rand(uint8_t *filename, long fpos);
    extern int pal_rename_file(uint8_t *filename, uint8_t *newname);
    extern int pal_delete_file(uint8_t *filename);
    extern uint8_t pal_truncate(char *fn, uint8_t rc);
    extern uint8_t pal_ram_load(uint8_t *filename, uint16_t address);
    extern void pal_log_buffer(uint8_t *buffer);
    extern int pal_kbhit(void);
    extern uint8_t pal_chready(void);
    extern uint8_t pal_getch_nb(void);
    extern void pal_make_user_dir();
    extern uint8_t pal_file_match(uint8_t *fcbname, uint8_t *pattern);
    extern uint8_t pal_find_next(uint8_t isdir);
    extern uint8_t pal_find_first(uint8_t isdir);
#ifdef __cplusplus
}
#endif

#endif
