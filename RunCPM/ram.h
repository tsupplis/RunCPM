#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
   extern void _ram_init();
   extern void _ram_write(uint16_t address, uint8_t value);
   extern uint8_t _ram_read(uint16_t address);
   extern void _ram_write16(uint16_t address, uint16_t value);
   extern uint16_t _ram_read16(uint16_t address);
   extern void _ram_fill(uint16_t address, int size, uint8_t value);
#ifdef __cplusplus
}
#endif
