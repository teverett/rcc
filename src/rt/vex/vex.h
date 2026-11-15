#ifndef RCC_RT_VEX_H
#define RCC_RT_VEX_H

// Runtime for rcc's vex backend (--target=vex), covering every reference device
// docs/SPEC.md section 5 of the sibling vex project (/Users/tom/projects/vex) defines: a minimal
// console (ports 0x10/0x11 in, 0x18/0x19 out) and the halt port (0x00). Every other port reads 0
// and ignores writes, so there is nothing else this hardware offers to wrap.

// Writes one byte to standard output (port 0x18).
void vex_putc(unsigned char c);
// Writes one byte to standard error (port 0x19).
void vex_eputc(unsigned char c);
// Writes a NUL-terminated string to standard output, one character at a time via vex_putc().
void vex_puts(char *s);
// Writes a NUL-terminated string to standard error, one character at a time via vex_eputc().
void vex_eputs(char *s);

// Reads the next byte of standard input (port 0x10); 0 at end of input.
unsigned char vex_getc();
// 1 once standard input is exhausted, else 0 (port 0x11).
unsigned char vex_eof();

// Halts the machine with `code` as the process exit status (port 0x00) - never returns.
void vex_exit(int code);

#endif
