#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
   extern void ram_init();
   extern void ram_write(uint16_t address, uint8_t value);
   extern uint8_t ram_read(uint16_t address);
   extern void ram_write16(uint16_t address, uint16_t value);
   extern uint16_t ram_read16(uint16_t address);
   extern void ram_fill(uint16_t address, int size, uint8_t value);
#ifdef __cplusplus
}
#endif
