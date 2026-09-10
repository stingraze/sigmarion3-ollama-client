#include "compat.h"
#include "config.h"
#include "http.h"
#include "ollama.h"
#include "pretty.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pretty_t g_pretty;

static int print_token(const char *text, int len, void *user)
{
    char out[1600];
    int n;

    (void)user;
    n = pretty_feed(&g_pretty, text, len, out, (int)sizeof(out));
    if (n > 0) {
        fwrite(out, 1, (size_t)n, stdout);
        fflush(stdout);
    }
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "Ollama CE host helper %s\n"
            "Usage:\n"
            "  ollama-ce-host [--url http://host:11434] --list\n"
            "  ollama-ce-host [--url http://host:11434] --model NAME --prompt TEXT\n",
            OLLAMA_CE_VERSION);
}

int main(int argc, char **argv)
{
    app_config_t cfg;
    int do_list;
    int i;
    const char *prompt;
    char err[256];
    int cancel;

    config_load(&cfg);
    do_list = 0;
    prompt = NULL;
    cancel = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            strncpy(cfg.url, argv[++i], sizeof(cfg.url) - 1);
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            strncpy(cfg.model, argv[++i], sizeof(cfg.model) - 1);
        } else if (strcmp(argv[i], "--prompt") == 0 && i + 1 < argc) {
            prompt = argv[++i];
        } else if (strcmp(argv[i], "--list") == 0) {
            do_list = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else {
            usage();
            return 1;
        }
    }

    if (http_startup() != 0) {
        fprintf(stderr, "socket init failed\n");
        return 1;
    }

    if (do_list) {
        char names[8192];

        if (ollama_list_models(cfg.url, names, (int)sizeof(names), &cancel, err, (int)sizeof(err)) != 0) {
            fprintf(stderr, "%s\n", err);
            http_cleanup();
            return 1;
        }
        puts(names);
        http_cleanup();
        return 0;
    }

    if (prompt) {
        char tail[1600];
        int n;

        pretty_init(&g_pretty);
        if (ollama_generate(cfg.url, cfg.model, prompt, print_token, NULL,
                            &cancel, err, (int)sizeof(err)) != 0) {
            fprintf(stderr, "\n%s\n", err);
            http_cleanup();
            return 1;
        }
        n = pretty_flush(&g_pretty, tail, (int)sizeof(tail));
        if (n > 0) {
            fwrite(tail, 1, (size_t)n, stdout);
        }
        fputc('\n', stdout);
        http_cleanup();
        return 0;
    }

    usage();
    http_cleanup();
    return 1;
}
