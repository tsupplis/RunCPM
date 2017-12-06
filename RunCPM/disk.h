#ifndef _DISK_H
#define _DISK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
extern uint8_t disk_get_file_size(uint16_t fcbaddr);
extern void disk_set_user(uint8_t user);
extern uint8_t disk_set_random(uint16_t fcbaddr);
extern uint8_t disk_write_rand(uint16_t fcbaddr);
extern uint8_t disk_read_rand(uint16_t fcbaddr);
extern uint8_t disk_delete_file(uint16_t fcbaddr);
extern uint8_t disk_make_file(uint16_t fcbaddr);
extern uint8_t disk_read_seq(uint16_t fcbaddr);
extern uint8_t disk_write_seq(uint16_t fcbaddr);
extern uint8_t disk_rename_file(uint16_t fcbaddr);
extern uint8_t disk_search_first(uint16_t fcbaddr, uint8_t isdir);
extern uint8_t disk_search_next(uint16_t fcbaddr, uint8_t isdir);
extern uint8_t disk_close_file(uint16_t fcbaddr);
extern uint8_t disk_open_file(uint16_t fcbaddr);
extern int disk_select_disk(uint8_t dr);
extern uint8_t disk_check_sub(void);
extern void fcb_hostname_to_fcb(uint16_t fcbaddr, uint8_t *filename);
extern void fcb_hostname_to_fcbname(uint8_t *from, uint8_t *to);
#ifdef __cplusplus
}
#endif

#endif
