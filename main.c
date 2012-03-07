
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>

#include <png.h>

#include <arpa/inet.h>

struct meta {
};

struct ares {
    uint16_t min;
    uint16_t max;
};

static void hexdump(const uint8_t *data, unsigned int len)
{
    const uint8_t *bufptr = data;
    const uint8_t const *endptr = bufptr + len;
    unsigned int n, m, o, p;
    int hexchr;

    for (n=0; n < len; n+=32, bufptr += 32) {
        hexchr = 0;
        for(m = 0; m < 32 && bufptr < endptr; m++, bufptr++) {
            if((m) && !(m%4)) {
                putchar(' ');
                hexchr++;
            }
            printf("%02x", *bufptr);
            hexchr+=2;
        }
        bufptr -= m;
        p = 71 - hexchr;
        for(o = 0; p < p; p++) {
            putchar(' ');
        }

        putchar(' ');

        for(m = 0; m < 32 && bufptr < endptr; m++, bufptr++) {
            if(isgraph(*bufptr)) {
                putchar(*bufptr);
            } else {
                putchar('.');
            }
        }
        bufptr -= m;

        putchar('\n');
    }
}

void *
readblob(FILE *in, size_t size)
{
    void *res = malloc(size);
    size_t rd;

    printf("readblob %d\n", size);

    rd = fread(res, 1, size, in);
    if(rd != size) {
        if(ferror(in)) {
            puts("Error in fread.");
        } else if (feof(in)) {
            puts("File to short: could not read header");
        }
        exit(1);
    }

    puts("done.");

    return res;
}

void
analyze(struct ares *res, uint16_t *data, size_t width, size_t height)
{
    int x, y;

    uint16_t min = UINT16_MAX;
    uint16_t max = 0;

    for(y = 0; y < height; y++) {
        for(x = 0; x < width; x++) {
            uint16_t px = data[(y * width) + x];
            uint16_t vl = ntohs(px);

            if(vl < min) {
                min = vl;
            }
            if(vl > max) {
                max = vl;
            }
        }
    }

    res->min = min;
    res->max = max;
}

void
fumble(struct ares *res, uint16_t *data, size_t width, size_t height)
{
    int x, y;
    float range = (res->max - res->min);
    float scale = ((float)UINT16_MAX) / range;

    for(y = 0; y < height; y++) {
        for(x = 0; x < width; x++) {
            int pos = (y * width) + x;
            uint16_t px = data[pos];
            uint16_t vl = ntohs(px);

            vl -= res->min;
            vl *= scale;

            data[pos] = htons(vl);
        }
    }
}

int
writepng(char *fname, uint16_t *data, size_t width, size_t height)
{
    int i;
    FILE *fp;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_byte **row_pointers = NULL;

    fp = fopen(fname, "wb");
    if(!fp) {
        return -1;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!png_ptr) {
        fclose(fp);
        return -1;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return -1;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return -1;
    }

    png_set_IHDR(png_ptr,
                 info_ptr,
                 width,
                 height,
                 16,
                 PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    row_pointers = png_malloc(png_ptr, height * sizeof(png_byte *));

    for (i = 0; i < height; i++) {
        uint8_t *row = png_malloc(png_ptr, width * 2);
        row_pointers[i] = (png_byte *)row;
        memcpy(row_pointers[i], &data[i * width], width * 2);
    }

    png_init_io(png_ptr, fp);
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    for (i = 0; i < height; i++) {
        png_free(png_ptr, row_pointers[i]);
    }
    png_free(png_ptr, row_pointers);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return 0;
}

int
main(int argc, char **argv)
{
    FILE *in;

    struct meta *imghead;
    uint16_t *imgdata;

    struct ares ar;

    size_t want;

    if(argc != 3) {
        printf("Usage: %s <is2-input> <png-output>\n", argv[0]);
        exit(1);
    }

    in = fopen(argv[1], "rb");
    if(!in) {
        perror("fopen");
        exit(1);
    }

    want = 366;
    imghead = readblob(in, want);
    puts("Header m1");
    hexdump(imghead, want);


    want = 160 * 120 * 2;
    imgdata = readblob(in, want);
    puts("Image");
    hexdump(imgdata, want);


    puts("Analyzing...");
    analyze(&ar, imgdata, 160, 120);

    fumble(&ar, imgdata, 160, 120);

    printf("min %x max %x\n", ar.min, ar.max);

    writepng(argv[2], imgdata, 160, 120);

    return 0;
}
