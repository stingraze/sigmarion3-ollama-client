#include "jsonutil.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int utf8_append(char *out, int o, int cap, unsigned int cp)
{
    int need;

    if (cp < 0x80) {
        need = 1;
    } else if (cp < 0x800) {
        need = 2;
    } else if (cp < 0x10000) {
        need = 3;
    } else {
        need = 4;
    }
    if (o + need >= cap) {
        return -1;
    }
    if (need == 1) {
        out[o++] = (char)cp;
    } else if (need == 2) {
        out[o++] = (char)(0xC0 | (cp >> 6));
        out[o++] = (char)(0x80 | (cp & 0x3F));
    } else if (need == 3) {
        out[o++] = (char)(0xE0 | (cp >> 12));
        out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[o++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[o++] = (char)(0xF0 | (cp >> 18));
        out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[o++] = (char)(0x80 | (cp & 0x3F));
    }
    return o;
}

int json_escape(const char *in, char *out, int out_cap)
{
    int i;
    int o;

    if (!in || !out || out_cap < 1) {
        return -1;
    }

    o = 0;
    for (i = 0; in[i]; i++) {
        unsigned char c;
        const char *esc;
        int n;

        c = (unsigned char)in[i];
        esc = NULL;
        switch (c) {
        case '\"': esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\b': esc = "\\b"; break;
        case '\f': esc = "\\f"; break;
        case '\n': esc = "\\n"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        default:
            break;
        }

        if (esc) {
            n = (int)strlen(esc);
            if (o + n >= out_cap) {
                out[0] = 0;
                return -1;
            }
            memcpy(out + o, esc, (size_t)n);
            o += n;
        } else if (c < 0x20) {
            if (o + 6 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            sprintf(out + o, "\\u%04x", c);
            o += 6;
        } else {
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = (char)c;
        }
    }

    out[o] = 0;
    return o;
}

int json_unescape(const char *in, int in_len, char *out, int out_cap)
{
    int i;
    int o;

    if (!in || !out || out_cap < 1) {
        return -1;
    }
    if (in_len < 0) {
        in_len = (int)strlen(in);
    }

    o = 0;
    i = 0;
    while (i < in_len) {
        unsigned char c;

        c = (unsigned char)in[i++];
        if (c != '\\') {
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = (char)c;
            continue;
        }
        if (i >= in_len) {
            break;
        }
        c = (unsigned char)in[i++];
        switch (c) {
        case '"':
        case '\\':
        case '/':
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = (char)c;
            break;
        case 'b':
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = '\b';
            break;
        case 'f':
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = '\f';
            break;
        case 'n':
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = '\n';
            break;
        case 'r':
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = '\r';
            break;
        case 't':
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = '\t';
            break;
        case 'u': {
            unsigned int cp;
            int n0, n1, n2, n3;

            if (i + 4 > in_len) {
                break;
            }
            n0 = hex_nibble((unsigned char)in[i]);
            n1 = hex_nibble((unsigned char)in[i + 1]);
            n2 = hex_nibble((unsigned char)in[i + 2]);
            n3 = hex_nibble((unsigned char)in[i + 3]);
            i += 4;
            if (n0 < 0 || n1 < 0 || n2 < 0 || n3 < 0) {
                break;
            }
            cp = (unsigned int)((n0 << 12) | (n1 << 8) | (n2 << 4) | n3);
            o = utf8_append(out, o, out_cap, cp);
            if (o < 0) {
                out[0] = 0;
                return -1;
            }
            break;
        }
        default:
            if (o + 1 >= out_cap) {
                out[0] = 0;
                return -1;
            }
            out[o++] = (char)c;
            break;
        }
    }

    out[o] = 0;
    return o;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

static const char *find_key(const char *json, const char *key)
{
    char pat[128];
    const char *p;
    size_t klen;

    if (!json || !key) {
        return NULL;
    }
    klen = strlen(key);
    if (klen + 3 >= sizeof(pat)) {
        return NULL;
    }
    pat[0] = '"';
    memcpy(pat + 1, key, klen);
    pat[1 + klen] = '"';
    pat[2 + klen] = 0;

    p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *after;

        if (p > json && p[-1] == '\\') {
            p += (int)klen + 2;
            continue;
        }
        after = skip_ws(p + (int)klen + 2);
        if (*after == ':') {
            return skip_ws(after + 1);
        }
        p += (int)klen + 2;
    }
    return NULL;
}

static int copy_json_string(const char *p, char *out, int out_cap)
{
    const char *start;
    int raw_len;
    int in_escape;

    p = skip_ws(p);
    if (*p != '"') {
        return 0;
    }
    p++;
    start = p;
    in_escape = 0;
    while (*p) {
        if (in_escape) {
            in_escape = 0;
        } else if (*p == '\\') {
            in_escape = 1;
        } else if (*p == '"') {
            break;
        }
        p++;
    }
    if (*p != '"') {
        return 0;
    }
    raw_len = (int)(p - start);
    if (json_unescape(start, raw_len, out, out_cap) < 0) {
        return 0;
    }
    return 1;
}

int json_get_string(const char *json, const char *key, char *out, int out_cap)
{
    const char *p;

    if (!out || out_cap < 1) {
        return 0;
    }
    out[0] = 0;
    p = find_key(json, key);
    if (!p) {
        return 0;
    }
    return copy_json_string(p, out, out_cap);
}

int json_get_bool(const char *json, const char *key, int *out)
{
    const char *p;

    if (!out) {
        return 0;
    }
    p = find_key(json, key);
    if (!p) {
        return 0;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int already_listed(const char *list, const char *name)
{
    const char *p;
    int nlen;

    nlen = (int)strlen(name);
    p = list;
    while (*p) {
        const char *nl;
        int seglen;

        nl = strchr(p, '\n');
        seglen = nl ? (int)(nl - p) : (int)strlen(p);
        if (seglen == nlen && strncmp(p, name, (size_t)nlen) == 0) {
            return 1;
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

int json_collect_model_names(const char *json, char *out, int out_cap)
{
    const char *p;
    int o;
    char name[256];

    if (!json || !out || out_cap < 1) {
        return -1;
    }
    out[0] = 0;
    o = 0;

    p = strstr(json, "\"models\"");
    if (!p) {
        p = json;
    }

    while ((p = strstr(p, "\"name\"")) != NULL) {
        const char *val;

        val = skip_ws(p + 6);
        if (*val == ':') {
            val = skip_ws(val + 1);
            if (copy_json_string(val, name, (int)sizeof(name)) && name[0]) {
                int nlen;

                if (!already_listed(out, name)) {
                    nlen = (int)strlen(name);
                    if (o + nlen + 2 >= out_cap) {
                        break;
                    }
                    memcpy(out + o, name, (size_t)nlen);
                    o += nlen;
                    out[o++] = '\n';
                    out[o] = 0;
                }
            }
        }
        p += 6;
    }

    if (o > 0 && out[o - 1] == '\n') {
        out[--o] = 0;
    }
    return o;
}
