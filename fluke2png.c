/*
 * Console tool to convert Fluke Thermal Imaging RAW files (.IS2) to .PNG
 * 
 * Ingo Albrecht
 * Bjoern Heller
 * Matthias Bock
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <png.h>
#include <arpa/inet.h>
#include <math.h>

struct header
{
    // the meaning of the individual header bytes is unknown
};

// struct to save minimal and maximal pixel value
struct extrema
{
    uint16_t min;
    uint16_t max;
};

/**
 * Print data as hexdump,
 * just like the console utility hexdump does
 */
static void hexdump(struct header *data, unsigned int len)
{
    const uint8_t *bufptr = data;
    const uint8_t const *endptr = bufptr + len;
    int hexchr, m, n, o;

    // line by line
    for (n=0; n < len; n+=32, bufptr += 32)
    {
        // 8 blocks of 4 bytes per line
        // = 32 bytes, except end of buffer is reached
        hexchr = 0;
        for (m = 0; m < 32 && bufptr < endptr; m++, bufptr++)
        {
            // after every 4*2 bytes insert 1 space
            if ((m) && !(m%4))
            {
                putchar(' ');
                hexchr++;
            }
            // print current buffer position as hex  
            printf("%02x", *bufptr);
            hexchr+=2;
        }
        
        // if end of buffer, pad to line end with spaces
        if (bufptr >= endptr)
            for (o = m*2+floor(m/4); o < 71; o++)
                printf(" ");

        // go back to current line's start position
        bufptr -= m;

        // put at least one space between hex and printable bytes
        putchar(' ');

        // print printable chars, else dots
        for (m = 0; m < 32 && bufptr < endptr; m++, bufptr++)
        {
            if (isgraph(*bufptr))
            {
                putchar(*bufptr);
            } else {
                putchar('.');
            }
        }
        bufptr -= m;

        putchar('\n');
    }
}

/**
 * Allocate size bytes of memory and fill with size bytes from file
 */
void *readblob(FILE *in, size_t size)
{
    void *res = malloc(size);
    size_t rd;

    rd = fread(res, 1, size, in);
    if (rd != size)
    {
        if (ferror(in))
        {
            puts("Read failed. Insufficient permissions?");
        } else if (feof(in))
        {
            puts("Read failed: File to short");
        }
        exit(1);
    }
    
    return res;
}

/**
 * Find minimal and maximal pixel values in image data
 */
void findMinMax(struct extrema *res, uint16_t *data, size_t width, size_t height)
{
    // initialize min and max values
    uint16_t min = UINT16_MAX;
    uint16_t max = 0;

    // iterate through all pixels
    int x, y;
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            // get pixel value at coordinate (x,y)
            uint16_t px = data[(y * width) + x];
            uint16_t vl = ntohs(px);

            // adjust min/max
            if (vl < min)
            {
                min = vl;
            }
            if (vl > max)
            {
                max = vl;
            }
        }
    }

    // store determined min/max pixel values in struct
    res->min = min;
    res->max = max;
}

/**
 * Adjust the value of all pixels to the maximum 16-bit color depth:
 * subtract offset, scale to full depth 
 */
void adjustMinMax(struct extrema *res, uint16_t *data, size_t width, size_t height)
{
    // value range
    float range = (res->max - res->min);

    // scale factor for scaling to 16-bit
    float scale = ((float)UINT16_MAX) / range;

    // iterate through all pixels
    int x, y, pos;
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            pos = (y * width) + x;
            
            // get pixel value at (x,y)
            uint16_t px = data[pos];

            // convert value
            uint16_t vl = ntohs(px);

            // subtract offset from value
            vl -= res->min;
            
            // scale to 16-bit
            vl *= scale;

            // convert back and set pixel value
            data[pos] = htons(vl);
        }
    }
}

/**
 * Save data as PNG image file
 */
int writepng(char *fname, uint16_t *data, size_t width, size_t height)
{
    FILE *fp;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_byte **row_pointers = NULL;

    // open file for writing
    fp = fopen(fname, "wb");
    if (!fp)
        return -1;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        fclose(fp);
        return -1;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return -1;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
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

    for (int i = 0; i < height; i++)
    {
        uint8_t *row = png_malloc(png_ptr, width * 2);
        row_pointers[i] = (png_byte *)row;
        memcpy(row_pointers[i], &data[i * width], width * 2);
    }

    png_init_io(png_ptr, fp);
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    // free memory
    for (int i = 0; i < height; i++)
    {
        png_free(png_ptr, row_pointers[i]);
    }
    png_free(png_ptr, row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    fclose(fp);

    return 0;
}

// main program 
int main(int argc, char **argv)
{
    FILE *in;

    struct header *imghead;
    uint16_t *imgdata;

    struct extrema ar;

    size_t want;

    // check console argument count
    if (argc != 3)
    {
        printf("Usage: %s <input filename.is2> <output filename.png>\n", argv[0]);
        exit(1);
    }

    // open input file
    in = fopen(argv[1], "rb");
    if (!in)
    {
        perror("Fatal: Could not open input file");
        exit(1);
    }

    // read IS2 header: 366 bytes
    puts("Reading header ...");
    want = 366;
    imghead = readblob(in, want);
    hexdump(imghead, want);

    // read image data: 160x120 pixel, color depth: 16-bit
    puts("Reading image data ...");
    want = 160 * 120 * 2;
    imgdata = readblob(in, want);
//    hexdump(imgdata, want);

    findMinMax(&ar, imgdata, 160, 120);
    printf("Minimal pixel value: 0x%04x\nMaximal pixel value: 0x%04x\n", ar.min, ar.max);

    puts("Adjusting value range ...");
    adjustMinMax(&ar, imgdata, 160, 120);

    // export image as black and white PNG
    puts("Exporting PNG ..."); 
    writepng(argv[2], imgdata, 160, 120);

    return 0;
}
