#ifndef OLLAMA_CE_UTF8_H
#define OLLAMA_CE_UTF8_H

/* UTF-16 (Windows CE WCHAR) <-> UTF-8. CP_UTF8 is not reliable on CE 3.0. */

int utf8_from_utf16(const unsigned short *src, char *dst, int dst_cap);
int utf16_from_utf8(const char *src, int src_len, unsigned short *dst, int dst_cap);

#endif
