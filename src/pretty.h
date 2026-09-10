#ifndef OLLAMA_CE_PRETTY_H
#define OLLAMA_CE_PRETTY_H

/*
 * Turn typical Ollama/LLM markdown into readable plain text for a WinCE
 * edit control. Line-oriented so it can run on a token stream.
 */

typedef struct {
    char hold[1024];
    int hold_len;
    int in_fence;
    int in_think;
    int blank_run;
} pretty_t;

void pretty_init(pretty_t *p);

/* Append UTF-8 tokens. Completed pretty text is written to out (0-terminated).
 * Returns bytes written, or -1 if out is too small (nothing consumed). */
int pretty_feed(pretty_t *p, const char *in, int in_len, char *out, int out_cap);

/* Flush a partial last line. */
int pretty_flush(pretty_t *p, char *out, int out_cap);

#endif
