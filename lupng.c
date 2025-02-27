/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Jan Solanti
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifndef LUPNG_USE_ZLIB
#include <miniz.h>
#else
#include <zlib.h>
#endif

#include "lupng.h"

#define PNG_NONE 0x00
#define PNG_IHDR 0x01
#define PNG_PLTE 0x02
#define PNG_IDAT 0x04
#define PNG_IEND 0x08

#define PNG_GRAYSCALE 0
#define PNG_TRUECOLOR 2
/* 24bpp RGB palette */
#define PNG_PALETTED 3
#define PNG_GRAYSCALE_ALPHA 4
#define PNG_TRUECOLOR_ALPHA 6

#define PNG_FILTER_NONE 0
#define PNG_FILTER_SUB 1
#define PNG_FILTER_UP 2
#define PNG_FILTER_AVERAGE 3
#define PNG_FILTER_PAETH 4

#define PNG_SIG_SIZE 8

#define BUF_SIZE 8192
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#if defined(_MSC_VER)
#define LU_INLINE __inline /* MS-specific inline */
#else
#define LU_INLINE inline /* rest of the world... */
#endif

#define SIZE_T_MAX_POSITIVE ( ((size_t)-1) >> 1 )

/********************************************************
 * CRC computation as per PNG spec
 ********************************************************/

/* Precomputed table of CRCs of all 8-bit messages
 using the polynomial from the PNG spec, 0xEDB88320L. */
static const uint32_t crcTable[] =
{
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
 should be initialized to all 1's, and the transmitted value
 is the 1's complement of the final running CRC. */
static uint32_t crc(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFL;

    for (size_t n = 0; n < len; n++)
        crc = crcTable[(crc ^ buf[n]) & 0xFF] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFFL;
}



/********************************************************
 * Helper structs
 ********************************************************/

typedef struct
{
    uint32_t length;
    uint8_t *type;
    uint8_t *data;
    uint32_t crc;
} PngChunk;

typedef struct
{
    const LuUserContext *userCtx;
    uint32_t chunksFound;

    /* IHDR info */
    int32_t width;
    int32_t height;
    uint8_t depth;
    uint8_t colorType;
    uint8_t channels;
    uint8_t compression;
    uint8_t filter;
    uint8_t interlace;

    /* PLTE info */
    uint8_t palette[256][3];
    size_t  paletteItems;

    /* fields used for (de)compression & (de-)filtering */
    z_stream stream;
    int32_t scanlineBytes;
    int32_t bytesPerPixel;
    int32_t currentByte;
    int32_t currentCol;
    int32_t currentRow;
    int32_t currentElem;
    uint8_t *currentScanline;
    uint8_t *previousScanline;
    uint8_t currentFilter;
    uint8_t interlacePass;
    size_t compressedBytes;

    /* used for constructing 16 bit deep pixels */
    int tmpCount;
    uint8_t tmpBytes[2];

    /* the output image */
    union {
        LuImage *img;
        const LuImage *cimg;
    };
} PngInfoStruct;

/* helper macro to output warning via user context of the info struct */
#define LUPNG_WARN_UC(uc,...) do { if ((uc)->warnProc) { (uc)->warnProc((uc)->warnProcUserPtr, "LuPng: " __VA_ARGS__); }} while(0)
#define LUPNG_WARN(info,...) LUPNG_WARN_UC((info)->userCtx, __VA_ARGS__)

/* PNG header: */
/*                    P     N     G     \r    \n    SUB   \n      */
#define PNG_SIG_BYTES 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
static const uint8_t PNG_SIG[] = {PNG_SIG_BYTES};

static const int8_t startingRow[] =  { 0, 0, 0, 4, 0, 2, 0, 1 };
static const int8_t startingCol[] =  { 0, 0, 4, 0, 2, 0, 1, 0 };
static const int8_t rowIncrement[] = { 1, 8, 8, 8, 4, 4, 2, 2 };
static const int8_t colIncrement[] = { 1, 8, 8, 4, 4, 2, 2, 1 };



/********************************************************
 * Helper functions
 ********************************************************/

static LU_INLINE uint32_t swap32(uint32_t n)
{
    union {
        unsigned char np[4];
        uint32_t i;
    } u;
    u.i = n;

    return ((uint32_t)u.np[0] << 24)  |
    ((uint32_t)u.np[1] << 16) |
    ((uint32_t)u.np[2] << 8)  |
    (uint32_t)u.np[3];
}

static LU_INLINE uint16_t swap16(uint16_t n)
{
    union {
        unsigned char np[2];
        uint16_t i;
    } u;
    u.i = n;

    return ((uint16_t)u.np[0] << 8) | (uint16_t)u.np[1];
}

static int bytesEqual(const uint8_t *a, const uint8_t *b, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (*(a+i) != *(b+i))
            return 0;
    }

    return 1;
}

static void* internalMalloc(size_t size, void *userPtr)
{
    (void)userPtr; /* not used */
    return malloc(size);
}

static void internalFree(void *ptr, void *userPtr)
{
    (void)userPtr; /* not used */
    free(ptr);
}

static void internalPrintf(void *userPtr, const char *fmt, ...)
{
    FILE *outStream = (FILE*)userPtr;
    va_list args;

    va_start(args, fmt);
    vfprintf(outStream, fmt, args);
    va_end(args);
    fputc('\n', outStream);
}

static size_t internalFread(void *ptr, size_t size, size_t count, void *userPtr)
{
    return fread(ptr, size, count, (FILE *)userPtr);
}

static size_t internalFwrite(const void *ptr, size_t size, size_t count, void *userPtr)
{
    return fwrite(ptr, size, count, (FILE *)userPtr);
}

/********************************************************
 * Png filter functions
 ********************************************************/
static LU_INLINE int absi(int val)
{
    return val > 0 ? val : -val;
}

static LU_INLINE uint8_t raw(PngInfoStruct *info, size_t col)
{
    if (col > SIZE_T_MAX_POSITIVE)
        return 0;
    return info->currentScanline[col];
}

static LU_INLINE uint8_t prior(PngInfoStruct *info, size_t col)
{
    if (info->currentRow <= startingRow[info->interlacePass] || col > SIZE_T_MAX_POSITIVE)
        return 0;
    return info->previousScanline[col];
}

static LU_INLINE uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c)
{
    unsigned int A = a, B = b, C = c;
    int p = (int)A + (int)B - (int)C;
    int pa = absi(p - (int)A);
    int pb = absi(p - (int)B);
    int pc = absi(p - (int)C);

    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

static LU_INLINE uint8_t deSub(PngInfoStruct *info, uint8_t filtered)
{
    return filtered + raw(info, info->currentByte-info->bytesPerPixel);
}

static LU_INLINE uint8_t deUp(PngInfoStruct *info, uint8_t filtered)
{
    return filtered + prior(info, info->currentByte);
}

static LU_INLINE uint8_t deAverage(PngInfoStruct *info, uint8_t filtered)
{
    uint16_t avg = (uint16_t)(raw(info, info->currentByte-info->bytesPerPixel)
                                + prior(info, info->currentByte));
    avg >>= 1;
    return filtered + avg;
}

static LU_INLINE uint8_t dePaeth(PngInfoStruct *info, uint8_t filtered)
{
    return filtered + paethPredictor(
                        raw(info, info->currentByte-info->bytesPerPixel),
                        prior(info, info->currentByte),
                        prior(info, info->currentByte-info->bytesPerPixel));
}

static LU_INLINE uint8_t none(PngInfoStruct *info)
{
    return raw(info, info->currentByte);
}

static LU_INLINE uint8_t sub(PngInfoStruct *info)
{
    return raw(info, info->currentByte) - raw(info, info->currentByte-info->bytesPerPixel);
}

static LU_INLINE uint8_t up(PngInfoStruct *info)
{
    return raw(info, info->currentByte) - prior(info, info->currentByte);
}

static LU_INLINE uint8_t average(PngInfoStruct *info)
{
    uint16_t avg = (uint16_t)(raw(info, info->currentByte-info->bytesPerPixel)
                              + prior(info, info->currentByte));
    avg >>= 1;
    return raw(info, info->currentByte) - avg;
}

static LU_INLINE uint8_t paeth(PngInfoStruct *info)
{
    return raw(info, info->currentByte) - paethPredictor(
                    raw(info, info->currentByte-info->bytesPerPixel),
                    prior(info, info->currentByte),
                    prior(info, info->currentByte-info->bytesPerPixel));
}



/********************************************************
 * Actual implementation
 ********************************************************/
static LU_INLINE int parseIhdr(PngInfoStruct *info, PngChunk *chunk)
{
    if (info->chunksFound)
    {
        LUPNG_WARN(info, "malformed PNG file!");
        return PNG_ERROR;
    }

    info->chunksFound |= PNG_IHDR;
    info->width = swap32(*(uint32_t *)chunk->data);
    info->height = swap32(*((uint32_t *)chunk->data + 1));
    info->depth = *(chunk->data + 8);
    info->colorType = *(chunk->data + 9);
    info->compression = *(chunk->data + 10);
    info->filter = *(chunk->data + 11);
    info->interlace = *(chunk->data + 12);

    switch (info->colorType)
    {
        case PNG_GRAYSCALE:
            info->channels = 1;
            break;
        case PNG_TRUECOLOR:
            info->channels = 3;
            break;
        case PNG_PALETTED:
            info->channels = 3;
            break;
        case PNG_GRAYSCALE_ALPHA:
            info->channels = 2;
            break;
        case PNG_TRUECOLOR_ALPHA:
            info->channels = 4;
            break;
        default:
            LUPNG_WARN(info, "illegal color type: %u", info->colorType);
            return PNG_ERROR;
    }

    if (info->width <= 0 || info->height <= 0)
    {
        LUPNG_WARN(info, "illegal dimensions");
        return PNG_ERROR;
    }

    if ((info->colorType != PNG_GRAYSCALE && info->colorType != PNG_PALETTED && info->depth < 8)
        || (info->colorType == PNG_PALETTED && info->depth == 16) || info->depth > 16)
    {
        LUPNG_WARN(info, "illegal bit depth for color type");
        return PNG_ERROR;
    }

    if (info->compression)
    {
        LUPNG_WARN(info, "unknown compression method: %u", info->compression);
        return PNG_ERROR;
    }

    if (info->filter)
    {
        LUPNG_WARN(info, "unknown filter scheme: %u", info->filter);
        return PNG_ERROR;
    }

    memset(&(info->stream), 0, sizeof(info->stream));
    if(inflateInit(&(info->stream)) != Z_OK)
    {
        LUPNG_WARN(info, "PNG: inflateInit failed!");
        return PNG_ERROR;
    }
    info->img = luImageCreate(info->width, info->height,
                              info->channels, info->depth < 16 ? 8 : 16, NULL, info->userCtx);
    info->bytesPerPixel = MAX((info->channels * info->depth) >> 3, 1);
    info->scanlineBytes = info->width * info->bytesPerPixel;
    info->currentScanline = (uint8_t *)info->userCtx->allocProc(info->scanlineBytes, info->userCtx->allocProcUserPtr);
    info->previousScanline = (uint8_t *)info->userCtx->allocProc(info->scanlineBytes, info->userCtx->allocProcUserPtr);
    info->currentCol = -1;
    info->interlacePass = info->interlace ? 1 : 0;

    if (!info->img || !info->currentScanline || !info->previousScanline)
    {
        LUPNG_WARN(info, "memory allocation failed!");
        return PNG_ERROR;
    }

    return PNG_OK;
}

static LU_INLINE int parsePlte(PngInfoStruct *info, PngChunk *chunk)
{
    if (info->chunksFound & PNG_PLTE)
    {
        LUPNG_WARN(info, "too many palette chunks in file!");
        return PNG_ERROR;
    }
    info->chunksFound |= PNG_PLTE;

    if ((info->chunksFound & PNG_IDAT) || !(info->chunksFound & PNG_IHDR))
    {
        LUPNG_WARN(info, "malformed PNG file!");
        return PNG_ERROR;
    }

    if (info->colorType == PNG_GRAYSCALE || info->colorType == PNG_GRAYSCALE_ALPHA)
    {
        LUPNG_WARN(info, "palettes are not allowed in grayscale images!");
        return PNG_ERROR;
    }

    if (chunk->length < 1 || chunk->length > 768 || chunk->length % 3)
    {
        LUPNG_WARN(info, "invalid palette size!");
        return PNG_ERROR;
    }

    memcpy(info->palette, chunk->data, chunk->length);
    info->paletteItems = chunk->length / 3;

    return PNG_OK;
}

static LU_INLINE void stretchBits(uint8_t inByte, uint8_t outBytes[8], int depth)
{
    switch (depth)
    {
        case 1:
            for (int i = 0; i < 8; ++i)
                outBytes[i] = (inByte >> (7-i)) & 0x01;
            break;

        case 2:
            outBytes[0] = (inByte >> 6) & 0x03;
            outBytes[1] = (inByte >> 4) & 0x03;
            outBytes[2] = (inByte >> 2) & 0x03;
            outBytes[3] = inByte & 0x03;
            break;

        case 4:
            outBytes[0] = (inByte >> 4) & 0x0F;
            outBytes[1] = inByte & 0x0F;
            break;

        default:
            break;
    }
}

/* returns: 1 if at end of scanline, 0 otherwise */
static LU_INLINE int insertByte(PngInfoStruct *info, uint8_t byte)
{
    const uint8_t scale[] = {0x00, 0xFF, 0x55, 0x00, 0x11, 0x00, 0x00, 0x00};
    int advance = 0;

    /* for paletted images currentElem will always be 0 */
    size_t idx = info->currentRow * info->width * info->channels
                    + info->currentCol * info->channels
                    + info->currentElem;

    if (info->colorType != PNG_PALETTED)
    {
        if (info->depth == 8)
            info->cimg->data[idx] = byte;

        else if (info->depth < 8)
            info->cimg->data[idx] = byte * scale[info->depth];

        else /* depth == 16 */
        {
            info->tmpBytes[info->tmpCount] = byte;
            if (info->tmpCount) /* just inserted 2nd byte */
            {
                uint16_t val = *(uint16_t *)info->tmpBytes;
                val = swap16(val);
                info->tmpCount = 0;

                ((uint16_t *)(info->cimg->data))[idx] = val;
            }
            else
            {
                ++info->tmpCount;
                return 0;
            }
        }

        ++info->currentElem;
        if (info->currentElem >= info->channels)
        {
            advance = 1;
            info->currentElem = 0;
        }
    }
    else
    {
        /* The spec limits palette size to 256 entries */
        if (byte < info->paletteItems)
        {
            info->img->data[idx  ] = info->palette[byte][0];
            info->img->data[idx+1] = info->palette[byte][1];
            info->img->data[idx+2] = info->palette[byte][2];
        }
        else
        {
            LUPNG_WARN(info, "invalid palette index encountered!");
        }
        advance = 1;
    }

    if (advance)
    {
        /* advance to next pixel */
        info->currentCol += colIncrement[info->interlacePass];

        if (info->currentCol >= info->width)
        {
            uint8_t *tmp = info->currentScanline;
            info->currentScanline = info->previousScanline;
            info->previousScanline = tmp;

            info->currentCol = -1;
            info->currentByte = 0;

            info->currentRow += rowIncrement[info->interlacePass];
            if (info->currentRow >= info->height && info->interlace)
            {
                ++info->interlacePass;
                while (startingCol[info->interlacePass] >= info->width ||
                       startingRow[info->interlacePass] >= info->height)
                    ++info->interlacePass;
                info->currentRow = startingRow[info->interlacePass];
            }
            return 1;
        }
    }

    return 0;
}

static LU_INLINE int parseIdat(PngInfoStruct *info, PngChunk *chunk)
{
    unsigned char filtered[BUF_SIZE];
    int status = Z_OK;

    if (!(info->chunksFound & PNG_IHDR))
    {
        LUPNG_WARN(info, "malformed PNG file!");
        return PNG_ERROR;
    }

    if (info->colorType == PNG_PALETTED && !(info->chunksFound & PNG_PLTE))
    {
        LUPNG_WARN(info, "palette required but missing!");
        return PNG_ERROR;
    }

    info->chunksFound |= PNG_IDAT;
    info->stream.next_in = (unsigned char *)chunk->data;
    info->stream.avail_in = chunk->length;
    do
    {
        size_t decompressed;
        size_t i;

        info->stream.next_out = filtered;
        info->stream.avail_out = BUF_SIZE;
        status = inflate(&(info->stream), Z_NO_FLUSH);
        decompressed = BUF_SIZE - info->stream.avail_out;

        if (status != Z_OK &&
            status != Z_STREAM_END &&
            status != Z_BUF_ERROR &&
            status != Z_NEED_DICT)
        {
            LUPNG_WARN(info, "inflate error (%d)!", status);
            return PNG_ERROR;
        }

        for (i = 0;
            i < decompressed && info->currentCol < info->width && info->currentRow < info->height;
            ++i)
        {
            if (info->currentCol < 0)
            {
                info->currentCol = startingCol[info->interlacePass];
                info->currentFilter = filtered[i];
            }
            else
            {
                uint8_t rawByte = 0;
                uint8_t fullBytes[8] = {0};
                switch (info->currentFilter)
                {
                    case PNG_FILTER_NONE:
                        rawByte = filtered[i];
                        break;
                    case PNG_FILTER_SUB:
                        rawByte = deSub(info, filtered[i]);
                        break;
                    case PNG_FILTER_UP:
                        rawByte = deUp(info, filtered[i]);
                        break;
                    case PNG_FILTER_AVERAGE:
                        rawByte = deAverage(info, filtered[i]);
                        break;
                    case PNG_FILTER_PAETH:
                        rawByte = dePaeth(info, filtered[i]);
                        break;
                    default:
                        break;
                }

                info->currentScanline[info->currentByte] = rawByte;
                ++info->currentByte;

                if (info->depth < 8)
                {
                    stretchBits(rawByte, fullBytes, info->depth);
                    for (int j = 0; j < 8/info->depth; ++j)
                        if (insertByte(info, fullBytes[j]))
                            break;
                }
                else
                    insertByte(info, rawByte);
            }
        }
    } while ((info->stream.avail_in > 0 || info->stream.avail_out == 0)
            && info->currentCol < info->width && info->currentRow < info->height);

    return PNG_OK;
}

static LU_INLINE int readChunk(PngInfoStruct *info, PngChunk *chunk)
{
    uint32_t data_len, chunk_crc;
    uint8_t *buffer = NULL;

    if (info->userCtx->readProc(&data_len, 4, 1, info->userCtx->readProcUserPtr) != 1)
    {
        LUPNG_WARN(info, "read error");
        return PNG_ERROR;
    }

    data_len = swap32(data_len);
    if (data_len + 8 < data_len)
    {
        LUPNG_WARN(info, "chunk claims to be absurdly large");
        return PNG_ERROR;
    }

    // Store chunk type and contents and crc in the same buffer for convenience
    buffer = info->userCtx->allocProc(data_len + 8, info->userCtx->allocProcUserPtr);
    if (!buffer)
    {
        LUPNG_WARN(info, "memory allocation failed!");
        return PNG_ERROR;
    }

    if (!info->userCtx->readProc(buffer, data_len + 8, 1, info->userCtx->readProcUserPtr))
    {
        LUPNG_WARN(info, "chunk data read error");
        info->userCtx->freeProc(buffer, info->userCtx->freeProcUserPtr);
        return PNG_ERROR;
    }

    if (!isalpha(buffer[0]) || !isalpha(buffer[1]) || !isalpha(buffer[2]) || !isalpha(buffer[3]))
    {
        LUPNG_WARN(info, "invalid chunk name, possibly unprintable");
        info->userCtx->freeProc(buffer, info->userCtx->freeProcUserPtr);
        return PNG_ERROR;
    }

    chunk_crc = swap32(*((uint32_t*)(buffer + data_len + 4)));
    if (crc(buffer, data_len + 4) != chunk->crc)
    {
        LUPNG_WARN(info, "CRC mismatch in chunk '%.4s'", (char *)buffer);
        info->userCtx->freeProc(buffer, info->userCtx->freeProcUserPtr);
        return PNG_ERROR;
    }

    chunk->type = buffer;
    chunk->data = buffer + 4;
    chunk->length = data_len;
    chunk->crc = chunk_crc;

    return PNG_OK;
}

static LU_INLINE int handleChunk(PngInfoStruct *info, PngChunk *chunk)
{
    /* critical chunk */
    if (!(chunk->type[0] & 0x20))
    {
        if (bytesEqual(chunk->type, (const uint8_t *)"IHDR", 4))
            return parseIhdr(info, chunk);
        if (bytesEqual(chunk->type, (const uint8_t *)"PLTE", 4))
            return parsePlte(info, chunk);
        if (bytesEqual(chunk->type, (const uint8_t *)"IDAT", 4))
            return parseIdat(info, chunk);
        if (bytesEqual(chunk->type, (const uint8_t *)"IEND", 4))
        {
            info->chunksFound |= PNG_IEND;
            if (!(info->chunksFound & PNG_IDAT))
            {
                LUPNG_WARN(info, "no IDAT chunk found");
                return PNG_ERROR;
            }
            return PNG_DONE;
        }
    }
    /* ignore ancillary chunks for now */

    return PNG_OK;
}

LuImage *luPngReadUC(const LuUserContext *userCtx)
{
    uint8_t signature[PNG_SIG_SIZE];
    PngInfoStruct info = {0};
    PngChunk chunk = {0};
    int status = PNG_OK;

    info.userCtx = userCtx;

    if (!userCtx->skipSig)
    {
        if (!userCtx->readProc((void*)signature, PNG_SIG_SIZE, 1, userCtx->readProcUserPtr)
            || !bytesEqual(signature, PNG_SIG, PNG_SIG_SIZE))
        {
            LUPNG_WARN_UC(userCtx, "invalid signature");
            return NULL;
        }
    }

    while (readChunk(&info, &chunk) == PNG_OK)
    {
        status = handleChunk(&info, &chunk);
        userCtx->freeProc(chunk.type, userCtx->freeProcUserPtr);

        if (status != PNG_OK)
            break;
    }

    userCtx->freeProc(info.currentScanline, userCtx->freeProcUserPtr);
    userCtx->freeProc(info.previousScanline, userCtx->freeProcUserPtr);
    inflateEnd(&info.stream);

    if (status == PNG_DONE)
        return info.img;

    if (info.img)
        luImageRelease(info.img, info.userCtx);

    return NULL;
}

LuImage *luPngRead(PngReadProc readProc, void *userPtr, int skipSig)
{
    LuUserContext userCtx;

    luUserContextInitDefault(&userCtx);
    userCtx.readProc = readProc;
    userCtx.readProcUserPtr = userPtr;
    userCtx.skipSig = skipSig;
    return luPngReadUC(&userCtx);
}

LuImage *luPngReadFile(const char *filename)
{
    LuUserContext userCtx;
    LuImage *img;
    FILE *f = fopen(filename,"rb");

    luUserContextInitDefault(&userCtx);
    if (f) {
        userCtx.readProc = internalFread;
        userCtx.readProcUserPtr = f;
        img = luPngReadUC(&userCtx);
        fclose(f);
    } else {
        LUPNG_WARN_UC(&userCtx, "failed to open '%s'", filename);
        img = NULL;
    }

    return img;
}

static LU_INLINE int writeHeader(PngInfoStruct *info)
{
   const uint8_t colorType[] = {
            PNG_GRAYSCALE,
            PNG_GRAYSCALE_ALPHA,
            PNG_TRUECOLOR,
            PNG_TRUECOLOR_ALPHA
    };

    if (info->cimg->channels > 4)
    {
        LUPNG_WARN(info, "write error in chunk '%.4s'", (char*)buf);
        return PNG_ERROR;
    }

    uint8_t buffer[] = {
        PNG_SIG_BYTES,      // PNG signature
        0, 0, 0, 13,        // Length
        'I', 'H', 'D', 'R', // Type
        0, 0, 0, 0,         // Width,
        0, 0, 0, 0,         // Height,
        info->cimg->depth,  // Depth,
        colorType[info->cimg->channels-1], // Color Type,
        0,                  // Compression
        0,                  // Filter
        0,                  // Interlacing
        0, 0, 0, 0          // crc
    };

    *((uint32_t *)&buffer[8])  = swap32((uint32_t)info->cimg->width);
    *((uint32_t *)&buffer[12]) = swap32((uint32_t)info->cimg->height);
    *((uint32_t *)&buffer[29]) = swap32(crc((void*)buffer + 12, 17));

    if (!info->userCtx->writeProc((void *)buffer, sizeof(buffer), 1, info->userCtx->writeProcUserPtr))
    {
        return PNG_ERROR;
    }

    return PNG_OK;
}

static LU_INLINE void advanceBytep(PngInfoStruct *info, int is16bit)
{
    if (is16bit)
    {
        if (info->currentByte%2)
            --info->currentByte;
        else
            info->currentByte+=3;
    }
    else
        ++info->currentByte;
}

static LU_INLINE size_t filterScanline(PngInfoStruct *info,
                                    uint8_t(*f)(PngInfoStruct *info),
                                    uint8_t filter,
                                    uint8_t *filterCandidate,
                                    int is16bit)
{
    size_t curSum = 0;
    size_t fc;

    filterCandidate[0] = filter;
    for (info->currentByte = is16bit ? 1 : 0, fc = 1;
        info->currentByte < info->scanlineBytes; ++fc, advanceBytep(info, is16bit) )
    {
        uint8_t val = f(info);
        filterCandidate[fc] = val;
        curSum += val;
    }

    return curSum;
}

/*
 * Processes the input image and calls writeIdat for every BUF_SIZE compressed
 * bytes.
 */
static LU_INLINE int processPixels(PngInfoStruct *info)
{
    uint8_t *filterCandidate = (uint8_t *)info->userCtx->allocProc(info->scanlineBytes+1, info->userCtx->allocProcUserPtr);
    uint8_t *bestCandidate = (uint8_t *)info->userCtx->allocProc(info->scanlineBytes+1, info->userCtx->allocProcUserPtr);
    size_t minSum = (size_t)-1, curSum = 0;
    int status = Z_OK;
    int is16bit = info->cimg->depth == 16;

    struct __attribute__((packed)) {
        uint32_t length;
        uint8_t type[4];
        uint8_t data[BUF_SIZE];
        uint32_t crc;
    } chunk = {0, {'I', 'D', 'A', 'T'}, {0}, 0};

    if (!filterCandidate || !bestCandidate)
    {
        LUPNG_WARN(info, "PNG: memory allocation failed!");
        return PNG_ERROR;
    }

    memset(&info->stream, 0, sizeof(info->stream));
    if (deflateInit(&info->stream, info->userCtx->compressionLevel) != Z_OK)
    {
        LUPNG_WARN(info, "PNG: deflateInit failed!");
        info->userCtx->freeProc(filterCandidate, info->userCtx->freeProcUserPtr);
        info->userCtx->freeProc(bestCandidate, info->userCtx->freeProcUserPtr);
        return PNG_ERROR;
    }

    info->stream.avail_out = BUF_SIZE;
    info->stream.next_out = &chunk.data;

    for (info->currentRow = 0; info->currentRow < info->cimg->height; ++info->currentRow)
    {
        int flush = (info->currentRow < info->cimg->height-1) ? Z_NO_FLUSH : Z_FINISH;
        minSum = (size_t)-1;

        /*
         * 1st time it doesn't matter, the filters never look at the previous
         * scanline when processing row 0. And next time it'll be valid.
         */
        info->previousScanline = info->currentScanline;
        info->currentScanline = info->cimg->data + (info->currentRow*info->scanlineBytes);

        /*
         * Try to choose the best filter for each scanline.
         * Breaks in case of overflow, but hey it's just a heuristic.
         */
        for (info->currentFilter = PNG_FILTER_NONE; info->currentFilter <= PNG_FILTER_PAETH; ++info->currentFilter)
        {
            switch (info->currentFilter)
            {
                case PNG_FILTER_NONE:
                    curSum = filterScanline(info, none, PNG_FILTER_NONE, filterCandidate, is16bit);
                    break;

                case PNG_FILTER_SUB:
                    curSum = filterScanline(info, sub, PNG_FILTER_SUB, filterCandidate, is16bit);
                    break;

                case PNG_FILTER_UP:
                    curSum = filterScanline(info, up, PNG_FILTER_UP, filterCandidate, is16bit);
                    break;

                case PNG_FILTER_AVERAGE:
                    curSum = filterScanline(info, average, PNG_FILTER_AVERAGE, filterCandidate, is16bit);
                    break;

                case PNG_FILTER_PAETH:
                    curSum = filterScanline(info, paeth, PNG_FILTER_PAETH, filterCandidate, is16bit);
                    break;

                default:
                    break;
            }

            if (curSum < minSum || !info->currentFilter)
            {
                uint8_t *tmp = bestCandidate;
                bestCandidate = filterCandidate;
                filterCandidate = tmp;
                minSum = curSum;
            }
        }

        info->stream.avail_in = (unsigned int)info->scanlineBytes+1;
        info->stream.next_in = bestCandidate;

        /* compress bestCandidate */
        do
        {
            status = deflate(&info->stream, flush);

            if (info->stream.avail_out < BUF_SIZE)
            {
                uint32_t data_len = BUF_SIZE - info->stream.avail_out;

                chunk.length = swap32(data_len);
                chunk.crc = swap32(crc((void*)&chunk.type, data_len + 4));

                if (data_len != BUF_SIZE) // Move crc to proper place
                    memmove(&chunk.data[data_len], &chunk.crc, 4);

                if (!info->userCtx->writeProc((void *)&chunk, data_len + 12, 1, info->userCtx->writeProcUserPtr))
                {
                    LUPNG_WARN(info, "PNG: write error");
                    // return PNG_ERROR;
                }

                info->stream.next_out = &chunk.data;
                info->stream.avail_out = BUF_SIZE;
            }
        } while ((flush == Z_FINISH && status != Z_STREAM_END)
                    || (flush == Z_NO_FLUSH && info->stream.avail_in));
    }

    info->userCtx->freeProc(filterCandidate, info->userCtx->freeProcUserPtr);
    info->userCtx->freeProc(bestCandidate, info->userCtx->freeProcUserPtr);

    return PNG_OK;
}

static LU_INLINE int writeIend(PngInfoStruct *info)
{
    uint8_t buffer[] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0, 0, 0, 0};

    ((uint32_t*)buffer)[2] = swap32(crc(buffer + 4, 4));

    if (!info->userCtx->writeProc((void *)&buffer, 12, 1, info->userCtx->writeProcUserPtr))
    {
        LUPNG_WARN(info, "PNG: write error");
        return PNG_ERROR;
    }

    return PNG_OK;
}

int luPngWriteUC(const LuUserContext *userCtx, const LuImage *img)
{
    PngInfoStruct info = {0};
    info.userCtx = userCtx;
    info.cimg = img;
    info.bytesPerPixel = (info.cimg->channels * info.cimg->depth) >> 3;
    info.scanlineBytes = info.bytesPerPixel * info.cimg->width;

    if (writeHeader(&info) != PNG_OK)
        return PNG_ERROR;

    if (processPixels(&info) != PNG_OK)
    {
        deflateEnd(&(info.stream));
        return PNG_ERROR;
    }

    deflateEnd(&(info.stream));
    return writeIend(&info);
}

int luPngWrite(PngWriteProc writeProc, void *userPtr, const LuImage *img)
{
    LuUserContext userCtx;
    luUserContextInitDefault(&userCtx);
    userCtx.writeProc = writeProc;
    userCtx.writeProcUserPtr = userPtr;
    return luPngWriteUC(&userCtx, img);
}

int luPngWriteFile(const char *filename, const LuImage *img)
{
    LuUserContext userCtx;
    luUserContextInitDefault(&userCtx);

    if (!img || !filename)
        return PNG_ERROR;

    FILE *f = fopen(filename,"wb");
    if (f)
    {
        userCtx.writeProc = internalFwrite;
        userCtx.writeProcUserPtr = f;
        luPngWriteUC(&userCtx, img);
        fclose(f);
    }
    else
    {
        LUPNG_WARN_UC(&userCtx, "failed to open '%s'", filename);
        return PNG_ERROR;
    }

    return PNG_OK;
}

void luImageRelease(LuImage *img, const LuUserContext *userCtx)
{
    LuUserContext ucDefault;

    if (userCtx == NULL)
    {
        luUserContextInitDefault(&ucDefault);
        userCtx = &ucDefault;
    }

    if (!img->isUserData)
        userCtx->freeProc(img->data, userCtx->freeProcUserPtr);
    if (userCtx->overrideImage != img)
        userCtx->freeProc(img, userCtx->freeProcUserPtr);
}

LuImage *luImageCreate(size_t width, size_t height, uint8_t channels, uint8_t depth,
                       uint8_t *buffer, const LuUserContext *userCtx)
{
    LuImage *img;
    LuUserContext ucDefault;

    if (userCtx == NULL)
    {
        luUserContextInitDefault(&ucDefault);
        userCtx = &ucDefault;
    }

    if (depth != 8 && depth != 16)
    {
        LUPNG_WARN_UC(userCtx, "only bit depths 8 and 16 are supported!");
        return NULL;
    }

    if (width > 0x7FFFFFFF || height > 0x7FFFFFFF)
    {
        LUPNG_WARN_UC(userCtx, "only 32 bit signed image dimensions are supported!");
        return NULL;
    }

    if (userCtx->overrideImage)
        img = userCtx->overrideImage;
    else
        img = (LuImage *)userCtx->allocProc(sizeof(LuImage), userCtx->allocProcUserPtr);

    if (!img)
        return NULL;

    img->width = (int32_t)width;
    img->height = (int32_t)height;
    img->channels = channels;
    img->depth = depth;
    img->pitch = width * channels * (depth >> 3);
    img->dataSize = img->pitch * height;

    if (buffer)
    {
        img->data = buffer;
        img->isUserData = 1;
    }
    else
    {
        img->data = (uint8_t *)userCtx->allocProc(img->dataSize, userCtx->allocProcUserPtr);
        img->isUserData = 0;
    }

    if (img->data == NULL)
    {
        LUPNG_WARN_UC(userCtx, "Image->data: Out of memory!");
        luImageRelease(img, userCtx);
        return NULL;
    }

    return img;
}

uint8_t *luImageExtractBufAndRelease(LuImage *img, const LuUserContext *userCtx)
{
    LuUserContext ucDefault;

    if (!img)
        return NULL;

    if (userCtx == NULL)
    {
        luUserContextInitDefault(&ucDefault);
        userCtx = &ucDefault;
    }

    uint8_t *data = img->data;
    img->data = NULL;
    luImageRelease(img, userCtx);

    return data;
}

void luUserContextInitDefault(LuUserContext *userCtx)
{
    userCtx->readProc=NULL;
    userCtx->readProcUserPtr=NULL;
    userCtx->skipSig = 0;

    userCtx->writeProc=NULL;
    userCtx->writeProcUserPtr=NULL;
    userCtx->compressionLevel=Z_DEFAULT_COMPRESSION;

    userCtx->allocProc=internalMalloc;
    userCtx->allocProcUserPtr=NULL;
    userCtx->freeProc=internalFree;
    userCtx->freeProcUserPtr=NULL;

    userCtx->warnProc=internalPrintf;
    userCtx->warnProcUserPtr=(void*)stderr;

    userCtx->overrideImage=NULL;
}
