#include "http.h"
#include "compat.h"
#include "winsock2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_IO_TIMEOUT_MS 20000
#define HTTP_MAX_HEADER 8192

static volatile SOCKET g_active_sock = INVALID_SOCKET;

#ifndef MAKEWORD
#define MAKEWORD(a, b) ((unsigned short)(((a) & 0xff) | (((b) & 0xff) << 8)))
#endif

static int cancelled(volatile int *cancel)
{
    return cancel && *cancel;
}

static void set_err(char *err, int err_cap, const char *msg)
{
    if (err && err_cap > 0) {
        strncpy(err, msg, (size_t)err_cap - 1);
        err[err_cap - 1] = 0;
    }
}

int http_startup(void)
{
    WSADATA wsa;
    return WSAStartup(MAKEWORD(1, 1), &wsa);
}

void http_cleanup(void)
{
    http_abort();
    WSACleanup();
}

void http_abort(void)
{
    SOCKET s;

    s = g_active_sock;
    if (s != INVALID_SOCKET) {
        shutdown(s, SD_BOTH);
        closesocket(s);
    }
}

static void bind_active(SOCKET s)
{
    g_active_sock = s;
}

static void unbind_active(SOCKET s)
{
    if (g_active_sock == s) {
        g_active_sock = INVALID_SOCKET;
    }
}

static void close_sock(SOCKET s)
{
    unbind_active(s);
    if (s != INVALID_SOCKET) {
        shutdown(s, SD_BOTH);
        closesocket(s);
    }
}

void http_free(void *p)
{
    free(p);
}

int http_parse_url(const char *url, http_url_t *out)
{
    const char *p;
    const char *slash;
    const char *colon;
    char hostport[256];
    int hp_len;

    if (!url || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->port = 80;
    strcpy(out->path, "/");

    p = url;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        return -2;
    }

    slash = strchr(p, '/');
    if (!slash) {
        hp_len = (int)strlen(p);
        if (hp_len <= 0 || hp_len >= (int)sizeof(hostport)) {
            return -1;
        }
        memcpy(hostport, p, (size_t)hp_len);
        hostport[hp_len] = 0;
        strcpy(out->path, "/");
    } else {
        hp_len = (int)(slash - p);
        if (hp_len <= 0 || hp_len >= (int)sizeof(hostport)) {
            return -1;
        }
        memcpy(hostport, p, (size_t)hp_len);
        hostport[hp_len] = 0;
        strncpy(out->path, slash, sizeof(out->path) - 1);
    }

    while (hp_len > 0 && (hostport[hp_len - 1] == ' ' || hostport[hp_len - 1] == '\t')) {
        hostport[--hp_len] = 0;
    }

    colon = strchr(hostport, ':');
    if (colon) {
        int hlen;

        hlen = (int)(colon - hostport);
        if (hlen <= 0 || hlen >= (int)sizeof(out->host)) {
            return -1;
        }
        memcpy(out->host, hostport, (size_t)hlen);
        out->host[hlen] = 0;
        out->port = (unsigned short)atoi(colon + 1);
        if (out->port == 0) {
            out->port = 80;
        }
    } else {
        strncpy(out->host, hostport, sizeof(out->host) - 1);
    }

    if (!out->host[0]) {
        return -1;
    }
    return 0;
}

int http_join_url(const char *base_url, const char *path, http_url_t *out)
{
    int rc;

    rc = http_parse_url(base_url, out);
    if (rc != 0) {
        return rc;
    }
    if (path && path[0]) {
        strncpy(out->path, path, sizeof(out->path) - 1);
        out->path[sizeof(out->path) - 1] = 0;
    }
    return 0;
}

/*
 * Windows CE 3.0 gethostbyname() often fails for dotted-quad literals when no
 * DNS server is configured, so numeric addresses skip the resolver entirely.
 */
static int resolve_host(const char *host, struct in_addr *out)
{
    unsigned long ip;
    struct hostent *he;

    ip = inet_addr(host);
    if (ip != INADDR_NONE) {
        out->s_addr = ip;
        return 0;
    }

    he = gethostbyname(host);
    if (!he || !he->h_addr) {
        return -1;
    }
    memcpy(out, he->h_addr, sizeof(struct in_addr));
    return 0;
}

static SOCKET tcp_connect(const char *host, unsigned short port,
                          volatile int *cancel, char *err, int err_cap)
{
    SOCKET s;
    struct sockaddr_in addr;
    struct in_addr ip;
    char msg[256];
    int rc;

    if (cancelled(cancel)) {
        set_err(err, err_cap, "Cancelled");
        return INVALID_SOCKET;
    }

    if (resolve_host(host, &ip) != 0) {
        sprintf(msg, "Cannot resolve %.80s (err %d)", host, sock_errno());
        set_err(err, err_cap, msg);
        return INVALID_SOCKET;
    }

    /*
     * Blocking sockets, same pattern as Lynx-CE on Sigmarion III.
     * Do not use select()/FIONBIO: CE 3.0 Winsock often ignores select
     * timeouts and hangs forever. SO_SNDTIMEO may bound connect(); Stop
     * also closes the live socket from the UI thread.
     */
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    if (s == INVALID_SOCKET) {
        sprintf(msg, "socket() failed (err %d)", sock_errno());
        set_err(err, err_cap, msg);
        return INVALID_SOCKET;
    }

    sock_set_timeouts(s, HTTP_IO_TIMEOUT_MS);
    bind_active(s);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = ip;

    rc = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0) {
        int e;

        e = sock_errno();
        close_sock(s);
        if (cancelled(cancel)) {
            set_err(err, err_cap, "Cancelled");
        } else {
            sprintf(msg, "Cannot reach %.60s:%u (err %d)", host,
                    (unsigned int)port, e);
            set_err(err, err_cap, msg);
        }
        return INVALID_SOCKET;
    }
    return s;
}

static int send_all(SOCKET s, const char *data, int len, volatile int *cancel)
{
    int sent;

    sent = 0;
    while (sent < len) {
        int n;

        if (cancelled(cancel)) {
            return -2;
        }
        n = send(s, data + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += n;
    }
    return 0;
}

typedef struct {
    char *data;
    int len;
    int cap;
} growbuf_t;

static int growbuf_append(growbuf_t *b, const char *p, int n)
{
    char *nd;

    if (n <= 0) {
        return 0;
    }
    if (b->len + n + 1 > b->cap) {
        int ncap;

        ncap = b->cap ? b->cap * 2 : 4096;
        while (ncap < b->len + n + 1) {
            ncap *= 2;
            if (ncap > 4 * 1024 * 1024) {
                return -1;
            }
        }
        nd = (char *)realloc(b->data, (size_t)ncap);
        if (!nd) {
            return -1;
        }
        b->data = nd;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, p, (size_t)n);
    b->len += n;
    b->data[b->len] = 0;
    return 0;
}

static void growbuf_consume(growbuf_t *b, int n)
{
    if (n <= 0) {
        return;
    }
    if (n >= b->len) {
        b->len = 0;
        if (b->data) {
            b->data[0] = 0;
        }
        return;
    }
    memmove(b->data, b->data + n, (size_t)(b->len - n));
    b->len -= n;
    b->data[b->len] = 0;
}

static int parse_status(const char *headers)
{
    const char *p;

    p = strchr(headers, ' ');
    if (!p) {
        return 0;
    }
    return atoi(p + 1);
}

static int flush_lines(growbuf_t *pending, http_line_cb on_line, void *user, int last)
{
    int i;

    i = 0;
    while (i < pending->len) {
        int j;

        j = i;
        while (j < pending->len && pending->data[j] != '\n') {
            j++;
        }
        if (j >= pending->len && !last) {
            break;
        }
        {
            int seglen;
            char save;
            int rc;

            seglen = j - i;
            if (seglen > 0 && pending->data[i + seglen - 1] == '\r') {
                seglen--;
            }
            if (seglen > 0) {
                save = pending->data[i + seglen];
                pending->data[i + seglen] = 0;
                rc = on_line(pending->data + i, seglen, user);
                pending->data[i + seglen] = save;
                if (rc != 0) {
                    return -2;
                }
            }
        }
        if (j >= pending->len) {
            i = pending->len;
            break;
        }
        i = j + 1;
    }
    growbuf_consume(pending, i);
    return 0;
}

static int read_response(SOCKET s, growbuf_t *body, http_line_cb on_line, void *user,
                         volatile int *cancel, char *err, int err_cap)
{
    char chunk[1024];
    growbuf_t pending;
    int headers_done;
    int status;
    int n;

    memset(&pending, 0, sizeof(pending));
    headers_done = 0;
    status = 0;

    for (;;) {
        if (cancelled(cancel)) {
            set_err(err, err_cap, "Cancelled");
            free(pending.data);
            return -2;
        }

        n = recv(s, chunk, (int)sizeof(chunk), 0);
        if (n <= 0) {
            if (cancelled(cancel)) {
                set_err(err, err_cap, "Cancelled");
                free(pending.data);
                return -2;
            }
            if (!headers_done && sock_errno() != 0) {
                sprintf(chunk, "No reply from Ollama (err %d)", sock_errno());
                set_err(err, err_cap, chunk);
                free(pending.data);
                return -1;
            }
            break;
        }
        if (growbuf_append(&pending, chunk, n) != 0) {
            set_err(err, err_cap, "Out of memory");
            free(pending.data);
            return -1;
        }

        if (!headers_done) {
            char *sep;
            int seplen;
            int header_len;

            sep = strstr(pending.data, "\r\n\r\n");
            seplen = 4;
            if (!sep) {
                sep = strstr(pending.data, "\n\n");
                seplen = 2;
            }
            if (!sep) {
                if (pending.len >= HTTP_MAX_HEADER) {
                    set_err(err, err_cap, "HTTP headers too large");
                    free(pending.data);
                    return -1;
                }
                continue;
            }
            header_len = (int)(sep - pending.data);
            *sep = 0;
            status = parse_status(pending.data);
            if (status != 0 && (status < 200 || status >= 300)) {
                char tmp[80];

                if (status == 403) {
                    strcpy(tmp, "HTTP 403: set OLLAMA_HOST=0.0.0.0 and restart Ollama");
                } else {
                    sprintf(tmp, "HTTP %d from Ollama", status);
                }
                set_err(err, err_cap, tmp);
            }
            growbuf_consume(&pending, header_len + seplen);
            headers_done = 1;
        }

        if (headers_done && on_line) {
            if (flush_lines(&pending, on_line, user, 0) != 0) {
                free(pending.data);
                return -2;
            }
        } else if (headers_done && body) {
            if (growbuf_append(body, pending.data, pending.len) != 0) {
                set_err(err, err_cap, "Out of memory");
                free(pending.data);
                return -1;
            }
            pending.len = 0;
            if (pending.data) {
                pending.data[0] = 0;
            }
        }
    }

    if (!headers_done) {
        set_err(err, err_cap, "Server closed connection with no reply");
        free(pending.data);
        return -1;
    }
    if (on_line) {
        flush_lines(&pending, on_line, user, 1);
    } else if (body && pending.len > 0) {
        growbuf_append(body, pending.data, pending.len);
    }
    free(pending.data);
    if (status != 0 && (status < 200 || status >= 300)) {
        return -1;
    }
    return 0;
}

static int http_exchange(const char *method, const char *base_url, const char *path,
                         const char *body, int body_len,
                         growbuf_t *out_body, http_line_cb on_line, void *user,
                         volatile int *cancel, char *err, int err_cap)
{
    http_url_t url;
    SOCKET s;
    char host_hdr[288];
    char req[1024];
    int rc;
    int req_len;

    rc = http_join_url(base_url, path, &url);
    if (rc == -2) {
        set_err(err, err_cap, "HTTPS is not supported; use http://host:11434");
        return -1;
    }
    if (rc != 0) {
        set_err(err, err_cap, "Invalid server URL (use http://192.168.1.4:11434)");
        return -1;
    }

    s = tcp_connect(url.host, url.port, cancel, err, err_cap);
    if (s == INVALID_SOCKET) {
        return -1;
    }

    if (url.port == 80) {
        sprintf(host_hdr, "%s", url.host);
    } else {
        sprintf(host_hdr, "%s:%u", url.host, (unsigned int)url.port);
    }

    if (!body) {
        body = "";
        body_len = 0;
    }

    if (body_len > 0) {
        req_len = snprintf(req, sizeof(req),
                           "%s %s HTTP/1.0\r\n"
                           "Host: %s\r\n"
                           "User-Agent: OllamaCE/0.1\r\n"
                           "Accept: application/json\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           method, url.path, host_hdr, body_len);
    } else {
        req_len = snprintf(req, sizeof(req),
                           "%s %s HTTP/1.0\r\n"
                           "Host: %s\r\n"
                           "User-Agent: OllamaCE/0.1\r\n"
                           "Accept: application/json\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           method, url.path, host_hdr);
    }
    if (req_len <= 0 || req_len >= (int)sizeof(req)) {
        close_sock(s);
        set_err(err, err_cap, "Request header too large");
        return -1;
    }

    rc = send_all(s, req, req_len, cancel);
    if (rc == 0 && body_len > 0) {
        rc = send_all(s, body, body_len, cancel);
    }
    if (rc != 0) {
        int e;

        e = sock_errno();
        close_sock(s);
        if (cancelled(cancel)) {
            set_err(err, err_cap, "Cancelled");
        } else {
            sprintf(req, "send() failed (err %d)", e);
            set_err(err, err_cap, req);
        }
        return -1;
    }

    rc = read_response(s, out_body, on_line, user, cancel, err, err_cap);
    close_sock(s);
    return rc;
}

int http_get(const char *base_url, const char *path,
             char **out_body, int *out_len,
             volatile int *cancel, char *err, int err_cap)
{
    growbuf_t body;
    int rc;

    memset(&body, 0, sizeof(body));
    rc = http_exchange("GET", base_url, path, NULL, 0, &body, NULL, NULL,
                       cancel, err, err_cap);
    if (rc != 0) {
        free(body.data);
        return rc;
    }
    if (out_body) {
        *out_body = body.data ? body.data : (char *)calloc(1, 1);
    } else {
        free(body.data);
    }
    if (out_len) {
        *out_len = body.len;
    }
    return 0;
}

int http_post_lines(const char *base_url, const char *path,
                    const char *body, int body_len,
                    http_line_cb on_line, void *user,
                    volatile int *cancel, char *err, int err_cap)
{
    return http_exchange("POST", base_url, path, body, body_len,
                         NULL, on_line, user, cancel, err, err_cap);
}
