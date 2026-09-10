#include "pretty.h"

#include <string.h>

void pretty_init(pretty_t *p)
{
    if (!p) {
        return;
    }
    memset(p, 0, sizeof(*p));
}

static int is_ws(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int starts_with(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s++ != *pfx++) {
            return 0;
        }
    }
    return 1;
}

static const char *skip_ws(const char *s)
{
    while (is_ws((unsigned char)*s)) {
        s++;
    }
    return s;
}

static int emit(char *out, int o, int cap, const char *s, int n)
{
    if (n < 0) {
        n = (int)strlen(s);
    }
    if (o + n >= cap) {
        return -1;
    }
    memcpy(out + o, s, (size_t)n);
    return o + n;
}

static int emit_char(char *out, int o, int cap, char c)
{
    if (o + 1 >= cap) {
        return -1;
    }
    out[o++] = c;
    return o;
}

/* Copy inline markdown to plain text. */
static int strip_inline(const char *in, char *out, int cap)
{
    int i;
    int o;

    o = 0;
    i = 0;
    while (in[i]) {
        unsigned char c;

        c = (unsigned char)in[i];
        if (c == '*' || c == '_') {
            unsigned char nxt;
            unsigned char prev;

            if (in[i + 1] == (char)c) {
                i += 2;
                continue;
            }
            nxt = (unsigned char)in[i + 1];
            prev = i > 0 ? (unsigned char)in[i - 1] : ' ';
            /* Drop *italic* / _italic_, keep "2 * 3". */
            if (!is_ws(nxt) || !is_ws(prev)) {
                i++;
                continue;
            }
        }
        if (c == '`') {
            i++;
            continue;
        }
        if (c == '[') {
            const char *end;
            const char *paren;

            end = strchr(in + i, ']');
            if (end && end[1] == '(') {
                paren = strchr(end + 2, ')');
                if (paren) {
                    int n;

                    n = (int)(end - (in + i + 1));
                    if (o + n >= cap) {
                        return -1;
                    }
                    memcpy(out + o, in + i + 1, (size_t)n);
                    o += n;
                    i = (int)(paren - in) + 1;
                    continue;
                }
            }
        }
        if (o + 1 >= cap) {
            return -1;
        }
        out[o++] = (char)c;
        i++;
    }
    out[o] = 0;
    return o;
}

static int looks_like_rule(const char *s)
{
    int n;
    unsigned char c;

    s = skip_ws(s);
    c = (unsigned char)*s;
    if (c != '-' && c != '*' && c != '_') {
        return 0;
    }
    n = 0;
    while (*s == '-' || *s == '*' || *s == '_' || *s == ' ') {
        if (*s != ' ') {
            n++;
        }
        s++;
    }
    return n >= 3 && *s == 0;
}

static int heading_level(const char *s)
{
    int n;

    n = 0;
    while (s[n] == '#' && n < 6) {
        n++;
    }
    if (n > 0 && (s[n] == ' ' || s[n] == '\t')) {
        return n;
    }
    return 0;
}

static int ordered_marker(const char *s, const char **rest)
{
    const char *p;

    p = s;
    if (*p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        p++;
    }
    if (p[0] == '.' && (p[1] == ' ' || p[1] == '\t')) {
        *rest = skip_ws(p + 2);
        return 1;
    }
    if (p[0] == ')' && (p[1] == ' ' || p[1] == '\t')) {
        *rest = skip_ws(p + 2);
        return 1;
    }
    return 0;
}

static int process_line(pretty_t *p, const char *line, char *out, int cap)
{
    const char *body;
    const char *rest;
    char plain[1024];
    int o;
    int hl;
    int empty;

    o = 0;
    body = line;
    while (*body == '\r') {
        body++;
    }

    /* Drop reasoning blocks some models wrap in <think>. */
    if (p->in_think) {
        if (strstr(body, "</think>")) {
            p->in_think = 0;
        }
        return 0;
    }
    if (starts_with(skip_ws(body), "<think>")) {
        if (!strstr(body, "</think>")) {
            p->in_think = 1;
        }
        return 0;
    }

    if (starts_with(skip_ws(body), "```")) {
        p->in_fence = !p->in_fence;
        o = emit(out, o, cap, p->in_fence ? "----\n" : "----\n", 5);
        if (o < 0) {
            return -1;
        }
        p->blank_run = 0;
        out[o] = 0;
        return o;
    }

    if (p->in_fence) {
        o = emit(out, o, cap, "  ", 2);
        if (o < 0) {
            return -1;
        }
        o = emit(out, o, cap, body, -1);
        if (o < 0) {
            return -1;
        }
        o = emit_char(out, o, cap, '\n');
        if (o < 0) {
            return -1;
        }
        p->blank_run = 0;
        out[o] = 0;
        return o;
    }

    empty = (*skip_ws(body) == 0);
    if (empty) {
        if (p->blank_run) {
            out[0] = 0;
            return 0;
        }
        p->blank_run = 1;
        o = emit_char(out, o, cap, '\n');
        if (o < 0) {
            return -1;
        }
        out[o] = 0;
        return o;
    }
    p->blank_run = 0;

    if (looks_like_rule(body)) {
        o = emit(out, o, cap, "--------\n", 9);
        if (o < 0) {
            return -1;
        }
        out[o] = 0;
        return o;
    }

    body = skip_ws(body);

    if (*body == '>') {
        body = skip_ws(body + 1);
        o = emit(out, o, cap, "  | ", 4);
        if (o < 0) {
            return -1;
        }
    }

    hl = heading_level(body);
    if (hl) {
        body = skip_ws(body + hl);
        if (o == 0) {
            o = emit_char(out, o, cap, '\n');
            if (o < 0) {
                return -1;
            }
        }
    } else if ((body[0] == '-' || body[0] == '*' || body[0] == '+') &&
               (body[1] == ' ' || body[1] == '\t')) {
        body = skip_ws(body + 2);
        o = emit(out, o, cap, "  - ", 4);
        if (o < 0) {
            return -1;
        }
    } else if (ordered_marker(body, &rest)) {
        body = rest;
        o = emit(out, o, cap, "  - ", 4);
        if (o < 0) {
            return -1;
        }
    }

    if (strip_inline(body, plain, (int)sizeof(plain)) < 0) {
        return -1;
    }
    o = emit(out, o, cap, plain, -1);
    if (o < 0) {
        return -1;
    }
    o = emit_char(out, o, cap, '\n');
    if (o < 0) {
        return -1;
    }
    out[o] = 0;
    return o;
}

static int feed_hold_lines(pretty_t *p, char *out, int out_cap)
{
    int o;
    char *nl;

    o = 0;
    out[0] = 0;
    while (p->hold_len > 0) {
        char line[1024];
        int n;
        int wrote;
        char chunk[1200];

        nl = (char *)memchr(p->hold, '\n', (size_t)p->hold_len);
        if (!nl) {
            break;
        }
        n = (int)(nl - p->hold);
        if (n >= (int)sizeof(line)) {
            n = (int)sizeof(line) - 1;
        }
        memcpy(line, p->hold, (size_t)n);
        line[n] = 0;
        memmove(p->hold, nl + 1, (size_t)(p->hold_len - n - 1));
        p->hold_len -= n + 1;
        p->hold[p->hold_len] = 0;

        wrote = process_line(p, line, chunk, (int)sizeof(chunk));
        if (wrote < 0) {
            return -1;
        }
        if (wrote > 0) {
            if (o + wrote >= out_cap) {
                return -1;
            }
            memcpy(out + o, chunk, (size_t)wrote);
            o += wrote;
            out[o] = 0;
        }
    }
    return o;
}

int pretty_feed(pretty_t *p, const char *in, int in_len, char *out, int out_cap)
{
    if (!p || !out || out_cap < 2) {
        return -1;
    }
    out[0] = 0;
    if (!in || in_len <= 0) {
        return 0;
    }
    if (p->hold_len + in_len >= (int)sizeof(p->hold)) {
        /* Force a line break so we cannot stall forever on a giant line. */
        if (p->hold_len + 1 < (int)sizeof(p->hold)) {
            p->hold[p->hold_len++] = '\n';
        } else {
            p->hold_len = 0;
        }
    }
    if (p->hold_len + in_len >= (int)sizeof(p->hold)) {
        in_len = (int)sizeof(p->hold) - p->hold_len - 1;
        if (in_len < 0) {
            in_len = 0;
        }
    }
    memcpy(p->hold + p->hold_len, in, (size_t)in_len);
    p->hold_len += in_len;
    p->hold[p->hold_len] = 0;
    return feed_hold_lines(p, out, out_cap);
}

int pretty_flush(pretty_t *p, char *out, int out_cap)
{
    int wrote;
    char chunk[1200];

    if (!p || !out || out_cap < 2) {
        return -1;
    }
    out[0] = 0;
    if (p->hold_len <= 0) {
        p->in_think = 0;
        p->in_fence = 0;
        return 0;
    }
    p->hold[p->hold_len] = 0;
    wrote = process_line(p, p->hold, chunk, (int)sizeof(chunk));
    p->hold_len = 0;
    p->hold[0] = 0;
    p->in_think = 0;
    p->in_fence = 0;
    if (wrote < 0) {
        return -1;
    }
    if (wrote >= out_cap) {
        return -1;
    }
    memcpy(out, chunk, (size_t)wrote);
    out[wrote] = 0;
    return wrote;
}
