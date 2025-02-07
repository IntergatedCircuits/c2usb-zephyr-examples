#ifndef __IOLIB_H__
#define __IOLIB_H__
#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C"
{
#endif

    int iolib_set_led(uint8_t idx, bool on);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __IOLIB_H__