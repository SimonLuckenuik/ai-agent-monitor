// Unpacks a .bin.gz the way the ESP8266 bootloader does (the core's bootloaders/eboot/eboot.c,
// copy_raw): uzlib from the core, built with eboot's -DRUNTIME_BITS_TABLES, a 32 KB
// dictionary, the file read 4096 bytes at a time and the image written 4096 bytes at a
// time. Compares the result with the plain image (spec 036: the update is compressed by
// zopfli, which every clock's bootloader must unpack). Run by tests/run_safety_tests.py.
//     eboot_inflate <image.bin.gz> <image.bin>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uzlib.h"

static FILE* in;
static unsigned char inbuf[4096];
static int read_cb(struct uzlib_uncomp* m) {
    size_t n = fread(inbuf, 1, sizeof inbuf, in);
    if (n == 0) return -1;
    memset(inbuf + n, 0xff, sizeof inbuf - n);   // flash past the file reads as erased
    m->source = inbuf;
    m->source_limit = inbuf + sizeof inbuf;
    return *(m->source++);
}

static unsigned char dict[32768], out[4096];

int main(int argc, char** argv) {
    in = fopen(argv[1], "rb");
    FILE* plain = fopen(argv[2], "rb");
    if (!in || !plain) { puts("cannot open"); return 2; }
    fseek(plain, 0, SEEK_END); long size = ftell(plain); fseek(plain, 0, SEEK_SET);
    unsigned char* want = malloc(size);
    fread(want, 1, size, plain);
    struct uzlib_uncomp m;
    uzlib_init();
    m.source = NULL; m.source_limit = NULL; m.source_read_cb = read_cb;
    uzlib_uncompress_init(&m, dict, sizeof dict);
    if (uzlib_gzip_parse_header(&m) != TINF_OK) { puts("FAIL header"); return 1; }
    long done = 0, left = (size + 4095) & ~4095;
    while (left > 0) {
        int to_read = left > 4096 ? 4096 : (int)left;
        m.dest_start = out; m.dest = out; m.dest_limit = out + to_read;
        int res = uzlib_uncompress(&m);
        if (res != TINF_DONE && res != TINF_OK) { printf("FAIL uncompress %d at %ld\n", res, done); return 1; }
        long cmp = size - done < to_read ? size - done : to_read;
        if (cmp > 0 && memcmp(out, want + done, cmp)) { printf("FAIL differs in the sector at %ld\n", done); return 1; }
        done += to_read; left -= to_read;
    }
    printf("PASS %ld bytes unpacked as eboot does, identical\n", size);
    return 0;
}
