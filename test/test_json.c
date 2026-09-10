#include "jsonutil.h"
#include "http.h"
#include "ollama.h"
#include "pretty.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    char out[1024];
    char names[512];
    http_url_t url;
    unsigned short wbuf[64];
    char u8[64];
    int done;

    if (json_escape("say \"hi\"\n", out, (int)sizeof(out)) < 0) {
        return fail("escape");
    }
    if (strcmp(out, "say \\\"hi\\\"\\n") != 0) {
        fprintf(stderr, "got [%s]\n", out);
        return fail("escape value");
    }
    if (json_unescape("say \\\"hi\\\"\\n", -1, out, (int)sizeof(out)) < 0) {
        return fail("unescape");
    }
    if (strcmp(out, "say \"hi\"\n") != 0) {
        fprintf(stderr, "got [%s]\n", out);
        return fail("unescape value");
    }

    if (!json_get_string("{\"response\":\"Hello\\nworld\"}", "response", out, (int)sizeof(out))) {
        return fail("get_string");
    }
    if (strcmp(out, "Hello\nworld") != 0) {
        return fail("get_string value");
    }

    if (!json_get_bool("{\"done\":true}", "done", &done) || !done) {
        return fail("get_bool");
    }

    if (json_collect_model_names(
            "{\"models\":[{\"name\":\"llama3.2:latest\",\"model\":\"llama3.2:latest\"},"
            "{\"name\":\"phi3:mini\"}]}",
            names, (int)sizeof(names)) <= 0) {
        return fail("collect");
    }
    if (!strstr(names, "llama3.2:latest") || !strstr(names, "phi3:mini")) {
        fprintf(stderr, "names=[%s]\n", names);
        return fail("collect values");
    }

    if (http_parse_url("http://192.168.1.10:11434", &url) != 0) {
        return fail("parse url");
    }
    if (strcmp(url.host, "192.168.1.10") != 0 || url.port != 11434) {
        return fail("parse url fields");
    }
    if (http_parse_url("https://example.com", &url) != -2) {
        return fail("reject https");
    }

    {
        struct {
            const char *in;
            const char *want;
        } cases[] = {
            { "192.168.1.4",                 "http://192.168.1.4:11434" },
            { "  192.168.1.4  ",             "http://192.168.1.4:11434" },
            { "http://192.168.1.4",          "http://192.168.1.4:11434" },
            { "http://192.168.1.4/",         "http://192.168.1.4:11434" },
            { "http://192.168.1.4:11434",    "http://192.168.1.4:11434" },
            { "192.168.1.4:11434",           "http://192.168.1.4:11434" },
            { "http://ollama.lan:11434/",    "http://ollama.lan:11434" }
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char buf[256];

            strcpy(buf, cases[i].in);
            ollama_normalize_url(buf, (int)sizeof(buf));
            if (strcmp(buf, cases[i].want) != 0) {
                fprintf(stderr, "normalize(%s) = [%s], want [%s]\n",
                        cases[i].in, buf, cases[i].want);
                return fail("normalize url");
            }
        }
    }

    if (http_parse_url("http://192.168.1.4:11434", &url) != 0 || url.port != 11434) {
        return fail("parse normalized url");
    }

    wbuf[0] = 'o';
    wbuf[1] = 'k';
    wbuf[2] = 0x65E5; /* 日 */
    wbuf[3] = 0;
    if (utf8_from_utf16(wbuf, u8, (int)sizeof(u8)) < 0 || strcmp(u8, "ok\xE6\x97\xA5") != 0) {
        return fail("utf8");
    }
    if (utf16_from_utf8(u8, -1, wbuf, 64) < 0 || wbuf[0] != 'o' || wbuf[2] != 0x65E5) {
        return fail("utf16");
    }

    {
        pretty_t pr;
        char out[2048];
        int n;

        pretty_init(&pr);
        {
            const char *md =
                "## Title\n\nHello **world** and *italics*.\n"
                "- one\n- two\n\n"
                "See [docs](http://example.com).\n"
                "Math 2 * 3 is fine.\n"
                "<think>hidden</think>\n"
                "```\ncode\n```\nVisible";
            char acc[2048];
            int i;

            acc[0] = 0;
            for (i = 0; md[i]; i++) {
                char ch[2];
                int w;

                ch[0] = md[i];
                ch[1] = 0;
                w = pretty_feed(&pr, ch, 1, out, (int)sizeof(out));
                if (w < 0) {
                    return fail("pretty stream");
                }
                if (w > 0) {
                    if (strlen(acc) + (size_t)w >= sizeof(acc)) {
                        return fail("pretty acc");
                    }
                    strcat(acc, out);
                }
            }
            n = pretty_flush(&pr, out, (int)sizeof(out));
            if (n > 0) {
                strcat(acc, out);
            }
            if (!strstr(acc, "Title") || strstr(acc, "##") || strstr(acc, "**")) {
                fprintf(stderr, "pretty=[%s]\n", acc);
                return fail("pretty headers/bold");
            }
            if (!strstr(acc, "  - one") || !strstr(acc, "docs") ||
                strstr(acc, "http://example.com") || strstr(acc, "hidden") ||
                !strstr(acc, "Visible") || !strstr(acc, "2 * 3") ||
                !strstr(acc, "  code\n")) {
                fprintf(stderr, "pretty=[%s]\n", acc);
                return fail("pretty lists/links/think/code");
            }
        }
    }

    puts("ok");
    return 0;
}
