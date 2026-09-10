#ifndef OLLAMA_CE_HTTP_H
#define OLLAMA_CE_HTTP_H

typedef struct {
    char host[256];
    unsigned short port;
    char path[256];
} http_url_t;

typedef int (*http_line_cb)(const char *line, int len, void *user);

int http_startup(void);
void http_cleanup(void);
void http_abort(void);

int http_parse_url(const char *url, http_url_t *out);
int http_join_url(const char *base_url, const char *path, http_url_t *out);

int http_get(const char *base_url, const char *path,
             char **out_body, int *out_len,
             volatile int *cancel, char *err, int err_cap);

int http_post_lines(const char *base_url, const char *path,
                    const char *body, int body_len,
                    http_line_cb on_line, void *user,
                    volatile int *cancel, char *err, int err_cap);

void http_free(void *p);

#endif
