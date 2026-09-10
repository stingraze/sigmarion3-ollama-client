#include "config.h"
#include "utf8.h"

#include <stdio.h>
#include <string.h>

#ifdef HOST_BUILD
#define CONFIG_PATH_MAX 512
#else
#include <windows.h>
#endif

void config_default(app_config_t *c)
{
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
    strncpy(c->url, OLLAMA_CE_DEFAULT_URL, sizeof(c->url) - 1);
    c->model[0] = 0;
}

static void trim_inplace(char *s)
{
    char *e;
    char *p;

    p = s;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t')) {
        *--e = 0;
    }
}

#ifdef HOST_BUILD

static void config_path_host(char *out, int cap)
{
    strncpy(out, "ollama-ce.ini", (size_t)cap - 1);
    out[cap - 1] = 0;
}

int config_load(app_config_t *c)
{
    FILE *f;
    char path[CONFIG_PATH_MAX];
    char line[512];

    config_default(c);
    config_path_host(path, (int)sizeof(path));
    f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    while (fgets(line, (int)sizeof(line), f)) {
        char *eq;

        trim_inplace(line);
        if (line[0] == 0 || line[0] == '#') {
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = 0;
        trim_inplace(line);
        trim_inplace(eq + 1);
        if (strcmp(line, "url") == 0) {
            strncpy(c->url, eq + 1, sizeof(c->url) - 1);
        } else if (strcmp(line, "model") == 0) {
            strncpy(c->model, eq + 1, sizeof(c->model) - 1);
        }
    }
    fclose(f);
    return 1;
}

int config_save(const app_config_t *c)
{
    FILE *f;
    char path[CONFIG_PATH_MAX];

    if (!c) {
        return 0;
    }
    config_path_host(path, (int)sizeof(path));
    f = fopen(path, "w");
    if (!f) {
        return 0;
    }
    fprintf(f, "url=%s\n", c->url);
    fprintf(f, "model=%s\n", c->model);
    fclose(f);
    return 1;
}

#else /* Windows CE */

static void config_path_w(WCHAR *out, int cap)
{
    int n;
    WCHAR *slash;
    const WCHAR *name;
    int i;

    n = (int)GetModuleFileName(NULL, out, (DWORD)cap);
    if (n <= 0) {
        lstrcpy(out, TEXT("ollama-ce.ini"));
        return;
    }
    slash = out + n;
    while (slash > out && *slash != L'\\' && *slash != L'/') {
        slash--;
    }
    if (*slash == L'\\' || *slash == L'/') {
        slash++;
    }
    name = TEXT("ollama-ce.ini");
    i = 0;
    while (name[i] && (slash - out + i) < cap - 1) {
        slash[i] = name[i];
        i++;
    }
    slash[i] = 0;
}

static void apply_line(app_config_t *c, char *line)
{
    char *eq;

    trim_inplace(line);
    if (line[0] == 0 || line[0] == '#') {
        return;
    }
    eq = strchr(line, '=');
    if (!eq) {
        return;
    }
    *eq = 0;
    trim_inplace(line);
    trim_inplace(eq + 1);
    if (strcmp(line, "url") == 0) {
        strncpy(c->url, eq + 1, sizeof(c->url) - 1);
    } else if (strcmp(line, "model") == 0) {
        strncpy(c->model, eq + 1, sizeof(c->model) - 1);
    }
}

int config_load(app_config_t *c)
{
    WCHAR wpath[MAX_PATH];
    HANDLE h;
    char buf[1024];
    DWORD got;
    char line[512];
    int li;
    DWORD i;

    config_default(c);
    config_path_w(wpath, MAX_PATH);
    h = CreateFile(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    li = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, NULL) && got > 0) {
        for (i = 0; i < got; i++) {
            if (buf[i] == '\n' || li >= (int)sizeof(line) - 1) {
                line[li] = 0;
                apply_line(c, line);
                li = 0;
            } else if (buf[i] != '\r') {
                line[li++] = buf[i];
            }
        }
    }
    if (li > 0) {
        line[li] = 0;
        apply_line(c, line);
    }
    CloseHandle(h);
    return 1;
}

int config_save(const app_config_t *c)
{
    WCHAR wpath[MAX_PATH];
    HANDLE h;
    char text[OLLAMA_CE_MAX_URL + OLLAMA_CE_MAX_MODEL + 32];
    DWORD n;
    DWORD written;

    if (!c) {
        return 0;
    }
    config_path_w(wpath, MAX_PATH);
    h = CreateFile(wpath, GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    n = (DWORD)sprintf(text, "url=%s\r\nmodel=%s\r\n", c->url, c->model);
    WriteFile(h, text, n, &written, NULL);
    CloseHandle(h);
    return written == n;
}

#endif
