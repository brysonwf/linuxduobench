// SPDX-License-Identifier: GPL-2.0-or-later
/* Minimal devmem for the Duo workbench initramfs (busybox lacks the applet; read() of MMIO via /dev/mem
 * returns EFAULT on arm64, so registers must be mmap'd).
 * usage: devmem <addr> [count]            read count 32-bit words
 *        devmem <addr> w <value>          write one 32-bit word
 * Build (WSL): aarch64-linux-gnu-gcc -static -O2 -o devmem devmem.c */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: devmem addr [count] | addr w value\n"); return 1; }
  uint64_t a = strtoull(argv[1], 0, 0), pg = a & ~0xfffULL;
  int wr = argc > 3 && !strcmp(argv[2], "w");
  unsigned n = (!wr && argc > 2) ? strtoul(argv[2], 0, 0) : 1;
  size_t len = ((a - pg) + n * 4 + 0xfff) & ~0xfffULL;
  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) { perror("open /dev/mem"); return 1; }
  volatile uint32_t *m = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pg);
  if (m == MAP_FAILED) { perror("mmap"); return 1; }
  volatile uint32_t *p = m + (a - pg) / 4;
  if (wr) { *p = strtoul(argv[3], 0, 0); printf("0x%08llx <= 0x%08x\n", (unsigned long long)a, *p); }
  else for (unsigned i = 0; i < n; i++) printf("0x%08llx: 0x%08x\n", (unsigned long long)(a + 4 * i), p[i]);
  return 0;
}
