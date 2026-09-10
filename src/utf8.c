#include "utf8.h"

int utf8_from_utf16(const unsigned short *src, char *dst, int dst_cap)
{
    int i;
    int o;

    if (!src || !dst || dst_cap < 1) {
        return -1;
    }

    o = 0;
    for (i = 0; src[i]; i++) {
        unsigned int cp;
        int need;

        cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (src[i + 1] - 0xDC00u);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }

        if (cp < 0x80) {
            need = 1;
        } else if (cp < 0x800) {
            need = 2;
        } else if (cp < 0x10000) {
            need = 3;
        } else {
            need = 4;
        }

        if (o + need >= dst_cap) {
            dst[o] = 0;
            return -1;
        }

        if (need == 1) {
            dst[o++] = (char)cp;
        } else if (need == 2) {
            dst[o++] = (char)(0xC0 | (cp >> 6));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (need == 3) {
            dst[o++] = (char)(0xE0 | (cp >> 12));
            dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            dst[o++] = (char)(0xF0 | (cp >> 18));
            dst[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    dst[o] = 0;
    return o;
}

int utf16_from_utf8(const char *src, int src_len, unsigned short *dst, int dst_cap)
{
    int i;
    int o;

    if (!src || !dst || dst_cap < 1) {
        return -1;
    }
    if (src_len < 0) {
        src_len = 0;
        while (src[src_len]) {
            src_len++;
        }
    }

    o = 0;
    i = 0;
    while (i < src_len) {
        unsigned int cp;
        unsigned char c;

        c = (unsigned char)src[i++];
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i < src_len) {
            cp = ((c & 0x1F) << 6) | ((unsigned char)src[i++] & 0x3F);
        } else if ((c & 0xF0) == 0xE0 && i + 1 < src_len) {
            cp = ((c & 0x0F) << 12)
               | (((unsigned char)src[i] & 0x3F) << 6)
               | ((unsigned char)src[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF8) == 0xF0 && i + 2 < src_len) {
            cp = ((c & 0x07) << 18)
               | (((unsigned char)src[i] & 0x3F) << 12)
               | (((unsigned char)src[i + 1] & 0x3F) << 6)
               | ((unsigned char)src[i + 2] & 0x3F);
            i += 3;
        } else {
            cp = 0xFFFD;
        }

        if (cp >= 0x10000) {
            if (o + 2 >= dst_cap) {
                dst[o] = 0;
                return -1;
            }
            cp -= 0x10000;
            dst[o++] = (unsigned short)(0xD800 + (cp >> 10));
            dst[o++] = (unsigned short)(0xDC00 + (cp & 0x3FF));
        } else {
            if (o + 1 >= dst_cap) {
                dst[o] = 0;
                return -1;
            }
            dst[o++] = (unsigned short)cp;
        }
    }

    dst[o] = 0;
    return o;
}
