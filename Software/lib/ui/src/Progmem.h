#ifndef UI_PROGMEM_H
#define UI_PROGMEM_H

#if defined(NATIVE_TEST) || !defined(ARDUINO)
// Native test builds or non-Arduino: provide stubs only if not already defined
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(const void **)(addr))
#endif
#else
#include <pgmspace.h>
#endif

#endif  // UI_PROGMEM_H
