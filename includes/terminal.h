#ifndef TERMINAL_H
#define TERMINAL_H

#include <zephyr/kernel.h>

void terminal_init(void);
void term_print(const char *fmt, ...);

#endif
