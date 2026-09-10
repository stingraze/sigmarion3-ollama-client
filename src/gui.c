/*
 * Ollama CE — native Windows CE GUI client (CeGCC / mingw32ce).
 * Aimed at HPC 2000 devices such as the NEC Sigmarion III (640x480).
 */

#include "compat.h"
#include "config.h"
#include "http.h"
#include "ollama.h"
#include "pretty.h"
#include "utf8.h"

#include <windows.h>
#include <commctrl.h>

#ifndef WS_EX_CLIENTEDGE
#define WS_EX_CLIENTEDGE 0x00000200
#endif
#ifndef WS_EX_CONTROLPARENT
#define WS_EX_CONTROLPARENT 0x00010000
#endif
#ifndef WS_THICKFRAME
#define WS_THICKFRAME WS_SIZEBOX
#endif
#ifndef CB_FINDSTRINGEXACT
#define CB_FINDSTRINGEXACT 0x0158
#endif
#ifndef EM_LIMITTEXT
#define EM_LIMITTEXT 0x00C5
#endif
#ifndef EM_SCROLLCARET
#define EM_SCROLLCARET 0x00B7
#endif

#define TRANSCRIPT_MAX  16384
#define TRANSCRIPT_KEEP 12288

#define IDC_LBL_URL     1001
#define IDC_URL         1002
#define IDC_REFRESH     1003
#define IDC_SAVE        1004
#define IDC_LBL_MODEL   1005
#define IDC_MODEL       1006
#define IDC_LBL_PROMPT  1007
#define IDC_PROMPT      1008
#define IDC_SEND        1009
#define IDC_STOP        1010
#define IDC_CLEAR       1011
#define IDC_LBL_OUT     1012
#define IDC_OUTPUT      1013
#define IDC_STATUS      1014

#define WM_WORKER_STATUS (WM_APP + 1)
#define WM_WORKER_APPEND (WM_APP + 2)
#define WM_WORKER_MODELS (WM_APP + 3)
#define WM_WORKER_DONE   (WM_APP + 4)

#define WORK_LIST 1
#define WORK_GEN  2

typedef struct {
    int kind;
    char url[OLLAMA_CE_MAX_URL];
    char model[OLLAMA_CE_MAX_MODEL];
    char *prompt;
    HWND hwnd;
} work_t;

typedef struct {
    HWND hwnd;
    pretty_t pretty;
} stream_t;

static const WCHAR kClassName[] = TEXT("OllamaCEMain");

static HINSTANCE g_inst;
static HWND g_hwnd;
static HWND g_url;
static HWND g_model;
static HWND g_prompt;
static HWND g_output;
static HWND g_status;
static HWND g_refresh;
static HWND g_save;
static HWND g_send;
static HWND g_stop;
static HWND g_clear;
static HWND g_lbl_url;
static HWND g_lbl_model;
static HWND g_lbl_prompt;
static HWND g_lbl_out;
static HFONT g_font;
static volatile int g_cancel;
static volatile int g_busy;
static volatile int g_closing;
static app_config_t g_cfg;

static WCHAR *utf8_dup_wide(const char *s)
{
    int cap;
    unsigned short *w;
    unsigned short *nw;

    if (!s) {
        s = "";
    }
    cap = (int)strlen(s) + 4;
    w = (unsigned short *)malloc((size_t)cap * sizeof(unsigned short));
    if (!w) {
        return NULL;
    }
    while (utf16_from_utf8(s, -1, w, cap) < 0) {
        cap *= 2;
        nw = (unsigned short *)realloc(w, (size_t)cap * sizeof(unsigned short));
        if (!nw) {
            free(w);
            return NULL;
        }
        w = nw;
        if (cap > 1024 * 1024) {
            free(w);
            return NULL;
        }
    }
    return (WCHAR *)w;
}

static int wide_to_utf8_hwnd(HWND hwnd, char *out, int cap)
{
    int n;
    WCHAR *w;
    int rc;

    n = GetWindowTextLength(hwnd);
    if (n < 0) {
        n = 0;
    }
    w = (WCHAR *)malloc(((size_t)n + 1) * sizeof(WCHAR));
    if (!w) {
        if (out && cap > 0) {
            out[0] = 0;
        }
        return -1;
    }
    GetWindowText(hwnd, w, n + 1);
    rc = utf8_from_utf16((const unsigned short *)w, out, cap);
    free(w);
    return rc;
}

static void set_status(const char *utf8)
{
    WCHAR *w;

    w = utf8_dup_wide(utf8 ? utf8 : "");
    if (w) {
        SetWindowText(g_status, w);
        free(w);
    }
}

static void set_busy(int busy)
{
    g_busy = busy;
    EnableWindow(g_send, !busy);
    EnableWindow(g_refresh, !busy);
    EnableWindow(g_save, !busy);
    EnableWindow(g_model, !busy);
    EnableWindow(g_url, !busy);
    EnableWindow(g_stop, busy);
}

static void apply_font(HWND hwnd)
{
    SendMessage(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
}

static void layout_children(HWND hwnd)
{
    RECT rc;
    int w, h;
    int x, y;
    int margin = 8;
    int gap = 6;
    int row = 22;
    int label_w = 52;
    int btn_w = 72;
    int prompt_h;
    int out_top;
    int status_h = 20;
    int inner_w;

    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w < 240) {
        w = 240;
    }
    if (h < 200) {
        h = 200;
    }
    inner_w = w - margin * 2;
    x = margin;
    y = margin;

    MoveWindow(g_lbl_url, x, y + 2, label_w, row - 2, TRUE);
    MoveWindow(g_url, x + label_w, y, inner_w - label_w - (btn_w + gap) * 2, row, TRUE);
    MoveWindow(g_refresh, x + inner_w - (btn_w + gap) * 2 + gap, y, btn_w, row, TRUE);
    MoveWindow(g_save, x + inner_w - btn_w, y, btn_w, row, TRUE);
    y += row + gap;

    MoveWindow(g_lbl_model, x, y + 2, label_w, row - 2, TRUE);
    MoveWindow(g_model, x + label_w, y, inner_w - label_w, 180, TRUE);
    y += row + gap;

    MoveWindow(g_lbl_prompt, x, y, inner_w, 16, TRUE);
    y += 16;
    prompt_h = h / 5;
    if (prompt_h < 56) {
        prompt_h = 56;
    }
    if (prompt_h > 110) {
        prompt_h = 110;
    }
    MoveWindow(g_prompt, x, y, inner_w, prompt_h, TRUE);
    y += prompt_h + gap;

    MoveWindow(g_send, x, y, btn_w, row, TRUE);
    MoveWindow(g_stop, x + btn_w + gap, y, btn_w, row, TRUE);
    MoveWindow(g_clear, x + (btn_w + gap) * 2, y, btn_w, row, TRUE);
    y += row + gap;

    MoveWindow(g_lbl_out, x, y, inner_w, 16, TRUE);
    y += 16;
    out_top = y;
    MoveWindow(g_output, x, out_top, inner_w, h - out_top - status_h - margin, TRUE);
    MoveWindow(g_status, x, h - status_h - 4, inner_w, status_h, TRUE);
}

static void trim_ascii(char *s)
{
    int len;

    while (*s == ' ' || *s == '\t') {
        memmove(s, s + 1, strlen(s));
    }
    len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = 0;
    }
}

static void read_fields(void)
{
    char before[OLLAMA_CE_MAX_URL];

    wide_to_utf8_hwnd(g_url, g_cfg.url, (int)sizeof(g_cfg.url));
    wide_to_utf8_hwnd(g_model, g_cfg.model, (int)sizeof(g_cfg.model));
    trim_ascii(g_cfg.model);

    strncpy(before, g_cfg.url, sizeof(before) - 1);
    before[sizeof(before) - 1] = 0;
    ollama_normalize_url(g_cfg.url, (int)sizeof(g_cfg.url));
    if (strcmp(before, g_cfg.url) != 0) {
        WCHAR *w;

        w = utf8_dup_wide(g_cfg.url);
        if (w) {
            SetWindowText(g_url, w);
            free(w);
        }
    }
}

static void fill_models(const char *names)
{
    const char *p;
    WCHAR *saved;
    int select;

    saved = utf8_dup_wide(g_cfg.model);
    SendMessage(g_model, CB_RESETCONTENT, 0, 0);
    p = names ? names : "";
    while (*p) {
        char item[256];
        int n;
        WCHAR *w;
        const char *nl;

        nl = strchr(p, '\n');
        n = nl ? (int)(nl - p) : (int)strlen(p);
        if (n >= (int)sizeof(item)) {
            n = (int)sizeof(item) - 1;
        }
        memcpy(item, p, (size_t)n);
        item[n] = 0;
        w = utf8_dup_wide(item);
        if (w) {
            SendMessage(g_model, CB_ADDSTRING, 0, (LPARAM)w);
            free(w);
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }

    select = CB_ERR;
    if (saved && saved[0]) {
        select = (int)SendMessage(g_model, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)saved);
        if (select == CB_ERR) {
            SetWindowText(g_model, saved);
        } else {
            SendMessage(g_model, CB_SETCURSEL, (WPARAM)select, 0);
        }
    } else if (SendMessage(g_model, CB_GETCOUNT, 0, 0) > 0) {
        SendMessage(g_model, CB_SETCURSEL, 0, 0);
    }
    free(saved);
    read_fields();
}

static WCHAR *utf8_dup_wide_crlf(const char *s)
{
    char *tmp;
    int i;
    int o;
    int cap;
    WCHAR *w;

    if (!s) {
        s = "";
    }
    cap = 0;
    for (i = 0; s[i]; i++) {
        cap++;
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) {
            cap++;
        }
    }
    tmp = (char *)malloc((size_t)cap + 1);
    if (!tmp) {
        return utf8_dup_wide(s);
    }
    o = 0;
    for (i = 0; s[i]; i++) {
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) {
            tmp[o++] = '\r';
        }
        tmp[o++] = s[i];
    }
    tmp[o] = 0;
    w = utf8_dup_wide(tmp);
    free(tmp);
    return w;
}

static int wide_starts_you(const WCHAR *s)
{
    return s[0] == (WCHAR)'Y' && s[1] == (WCHAR)'o' &&
           s[2] == (WCHAR)'u' && s[3] == (WCHAR)':';
}

static void prune_transcript(int extra)
{
    int len;
    int drop;
    int i;
    WCHAR *buf;

    len = GetWindowTextLength(g_output);
    if (len + extra <= TRANSCRIPT_MAX) {
        return;
    }

    drop = len + extra - TRANSCRIPT_KEEP;
    if (drop < 1) {
        drop = 1;
    }
    if (drop > len) {
        drop = len;
    }

    buf = (WCHAR *)malloc(((size_t)len + 1) * sizeof(WCHAR));
    if (!buf) {
        SendMessage(g_output, EM_SETSEL, 0, (LPARAM)drop);
        SendMessage(g_output, EM_REPLACESEL, FALSE, (LPARAM)TEXT(""));
        return;
    }
    GetWindowText(g_output, buf, len + 1);

    i = drop;
    while (i < len && buf[i] != (WCHAR)'\n') {
        i++;
    }
    if (i < len) {
        i++;
    }
    while (i < len) {
        if ((i == 0 || buf[i - 1] == (WCHAR)'\n') && wide_starts_you(buf + i)) {
            drop = i;
            break;
        }
        i++;
    }

    SendMessage(g_output, EM_SETSEL, 0, (LPARAM)drop);
    SendMessage(g_output, EM_REPLACESEL, FALSE, (LPARAM)TEXT(""));
    free(buf);
}

static void append_output(const WCHAR *text)
{
    int add;
    int len;

    if (!text || !text[0]) {
        return;
    }
    add = 0;
    while (text[add]) {
        add++;
    }
    prune_transcript(add);
    len = GetWindowTextLength(g_output);
    SendMessage(g_output, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(g_output, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessage(g_output, EM_SCROLLCARET, 0, 0);
}

static void append_utf8(const char *s)
{
    WCHAR *w;

    w = utf8_dup_wide_crlf(s);
    if (w) {
        append_output(w);
        free(w);
    }
}

static void begin_turn(const char *prompt, const char *model)
{
    if (GetWindowTextLength(g_output) > 0) {
        append_utf8("\n");
    }
    append_utf8("You:\n");
    append_utf8(prompt && prompt[0] ? prompt : "");
    append_utf8("\n\n");
    if (model && model[0]) {
        append_utf8(model);
        append_utf8(":\n");
    } else {
        append_utf8("Ollama:\n");
    }
}

static void post_utf8(HWND hwnd, const char *s)
{
    WCHAR *w;

    if (!s || !s[0]) {
        return;
    }
    w = utf8_dup_wide_crlf(s);
    if (w) {
        if (!PostMessage(hwnd, WM_WORKER_APPEND, 0, (LPARAM)w)) {
            free(w);
        }
    }
}

static void stream_flush(stream_t *s)
{
    char pretty_out[1600];
    int n;

    n = pretty_flush(&s->pretty, pretty_out, (int)sizeof(pretty_out));
    if (n > 0) {
        post_utf8(s->hwnd, pretty_out);
    }
}

static int on_token(const char *text, int len, void *user)
{
    stream_t *s;
    char pretty_out[1600];
    int n;

    s = (stream_t *)user;
    n = pretty_feed(&s->pretty, text, len, pretty_out, (int)sizeof(pretty_out));
    if (n > 0) {
        post_utf8(s->hwnd, pretty_out);
    }
    return 0;
}

static DWORD WINAPI worker_proc(LPVOID param)
{
    work_t *w;
    char err[256];
    WCHAR *wmsg;

    w = (work_t *)param;
    err[0] = 0;

    if (w->kind == WORK_LIST) {
        char names[8192];

        names[0] = 0;
        if (ollama_list_models(w->url, names, (int)sizeof(names), &g_cancel, err, (int)sizeof(err)) == 0) {
            char *copy;

            char count_msg[64];
            int count;
            int i;

            copy = (char *)malloc(strlen(names) + 1);
            if (copy) {
                strcpy(copy, names);
                if (!PostMessage(w->hwnd, WM_WORKER_MODELS, 0, (LPARAM)copy)) {
                    free(copy);
                }
            }
            count = names[0] ? 1 : 0;
            for (i = 0; names[i]; i++) {
                if (names[i] == '\n') {
                    count++;
                }
            }
            sprintf(count_msg, "%d model(s) loaded", count);
            wmsg = utf8_dup_wide(count_msg);
        } else {
            wmsg = utf8_dup_wide(err[0] ? err : "Failed to list models");
        }
        if (wmsg && !PostMessage(w->hwnd, WM_WORKER_STATUS, 0, (LPARAM)wmsg)) {
            free(wmsg);
        }
    } else if (w->kind == WORK_GEN) {
        stream_t stream;

        memset(&stream, 0, sizeof(stream));
        stream.hwnd = w->hwnd;
        pretty_init(&stream.pretty);
        if (ollama_generate(w->url, w->model, w->prompt ? w->prompt : "",
                            on_token, &stream, &g_cancel, err, (int)sizeof(err)) == 0) {
            stream_flush(&stream);
            post_utf8(w->hwnd, "\n");
            wmsg = utf8_dup_wide(g_cancel ? "Stopped" : "Done");
        } else {
            stream_flush(&stream);
            wmsg = utf8_dup_wide(err[0] ? err : "Generate failed");
        }
        if (wmsg && !PostMessage(w->hwnd, WM_WORKER_STATUS, 0, (LPARAM)wmsg)) {
            free(wmsg);
        }
    }

    PostMessage(w->hwnd, WM_WORKER_DONE, 0, 0);
    free(w->prompt);
    free(w);
    return 0;
}

static int start_worker(int kind)
{
    work_t *w;
    HANDLE th;
    DWORD tid;

    if (g_busy) {
        return 0;
    }
    read_fields();
    if (kind == WORK_GEN && !g_cfg.model[0]) {
        set_status("Select or type a model name");
        return 0;
    }
    if (kind == WORK_GEN && GetWindowTextLength(g_prompt) <= 0) {
        set_status("Type a prompt first");
        return 0;
    }

    w = (work_t *)calloc(1, sizeof(*w));
    if (!w) {
        set_status("Out of memory");
        return 0;
    }
    w->kind = kind;
    w->hwnd = g_hwnd;
    strncpy(w->url, g_cfg.url, sizeof(w->url) - 1);
    strncpy(w->model, g_cfg.model, sizeof(w->model) - 1);
    if (kind == WORK_GEN) {
        int n;
        WCHAR *wp;

        n = GetWindowTextLength(g_prompt);
        wp = (WCHAR *)malloc(((size_t)n + 1) * sizeof(WCHAR));
        if (!wp) {
            free(w);
            set_status("Out of memory");
            return 0;
        }
        GetWindowText(g_prompt, wp, n + 1);
        w->prompt = (char *)malloc((size_t)n * 3 + 8);
        if (!w->prompt) {
            free(wp);
            free(w);
            set_status("Out of memory");
            return 0;
        }
        if (utf8_from_utf16((const unsigned short *)wp, w->prompt, n * 3 + 8) < 0) {
            free(wp);
            free(w->prompt);
            free(w);
            set_status("Prompt too large");
            return 0;
        }
        free(wp);
    }

    g_cancel = 0;
    set_busy(1);
    {
        char msg[OLLAMA_CE_MAX_URL + OLLAMA_CE_MAX_MODEL + 48];

        if (kind == WORK_LIST) {
            sprintf(msg, "Contacting %s ...", g_cfg.url);
        } else {
            sprintf(msg, "Asking %s ...", g_cfg.model);
            begin_turn(w->prompt, g_cfg.model);
            SetWindowText(g_prompt, TEXT(""));
        }
        set_status(msg);
    }

    th = CreateThread(NULL, 0, worker_proc, w, 0, &tid);
    if (!th) {
        set_busy(0);
        free(w->prompt);
        free(w);
        set_status("CreateThread failed");
        return 0;
    }
    CloseHandle(th);
    return 1;
}

static void on_save(void)
{
    read_fields();
    if (config_save(&g_cfg)) {
        set_status("Settings saved");
    } else {
        set_status("Could not save ollama-ce.ini");
    }
}

static void create_children(HWND hwnd)
{
    DWORD edit_ex = WS_EX_CLIENTEDGE;

    g_lbl_url = CreateWindow(TEXT("STATIC"), TEXT("Server"),
                             WS_CHILD | WS_VISIBLE | SS_LEFT,
                             0, 0, 0, 0, hwnd, (HMENU)IDC_LBL_URL, g_inst, NULL);
    g_url = CreateWindowEx(edit_ex, TEXT("EDIT"), NULL,
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                           0, 0, 0, 0, hwnd, (HMENU)IDC_URL, g_inst, NULL);
    g_refresh = CreateWindow(TEXT("BUTTON"), TEXT("Models"),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 0, 0, hwnd, (HMENU)IDC_REFRESH, g_inst, NULL);
    g_save = CreateWindow(TEXT("BUTTON"), TEXT("Save"),
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                          0, 0, 0, 0, hwnd, (HMENU)IDC_SAVE, g_inst, NULL);

    g_lbl_model = CreateWindow(TEXT("STATIC"), TEXT("Model"),
                               WS_CHILD | WS_VISIBLE | SS_LEFT,
                               0, 0, 0, 0, hwnd, (HMENU)IDC_LBL_MODEL, g_inst, NULL);
    g_model = CreateWindow(TEXT("COMBOBOX"), NULL,
                           WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL |
                           WS_VSCROLL | WS_TABSTOP,
                           0, 0, 0, 0, hwnd, (HMENU)IDC_MODEL, g_inst, NULL);

    g_lbl_prompt = CreateWindow(TEXT("STATIC"), TEXT("Prompt"),
                                WS_CHILD | WS_VISIBLE | SS_LEFT,
                                0, 0, 0, 0, hwnd, (HMENU)IDC_LBL_PROMPT, g_inst, NULL);
    g_prompt = CreateWindowEx(edit_ex, TEXT("EDIT"), NULL,
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                              ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                              0, 0, 0, 0, hwnd, (HMENU)IDC_PROMPT, g_inst, NULL);

    g_send = CreateWindow(TEXT("BUTTON"), TEXT("Send"),
                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                          0, 0, 0, 0, hwnd, (HMENU)IDC_SEND, g_inst, NULL);
    g_stop = CreateWindow(TEXT("BUTTON"), TEXT("Stop"),
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                          0, 0, 0, 0, hwnd, (HMENU)IDC_STOP, g_inst, NULL);
    g_clear = CreateWindow(TEXT("BUTTON"), TEXT("Clear"),
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           0, 0, 0, 0, hwnd, (HMENU)IDC_CLEAR, g_inst, NULL);

    g_lbl_out = CreateWindow(TEXT("STATIC"), TEXT("Chat"),
                             WS_CHILD | WS_VISIBLE | SS_LEFT,
                             0, 0, 0, 0, hwnd, (HMENU)IDC_LBL_OUT, g_inst, NULL);
    g_output = CreateWindowEx(edit_ex, TEXT("EDIT"), NULL,
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                              ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                              0, 0, 0, 0, hwnd, (HMENU)IDC_OUTPUT, g_inst, NULL);
    g_status = CreateWindow(TEXT("STATIC"), TEXT("Ready"),
                            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                            0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, g_inst, NULL);

    apply_font(g_lbl_url);
    apply_font(g_url);
    apply_font(g_refresh);
    apply_font(g_save);
    apply_font(g_lbl_model);
    apply_font(g_model);
    apply_font(g_lbl_prompt);
    apply_font(g_prompt);
    apply_font(g_send);
    apply_font(g_stop);
    apply_font(g_clear);
    apply_font(g_lbl_out);
    apply_font(g_output);
    apply_font(g_status);

    EnableWindow(g_stop, FALSE);
    SendMessage(g_output, EM_LIMITTEXT, TRANSCRIPT_MAX, 0);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        create_children(hwnd);
        {
            WCHAR *wurl;

            wurl = utf8_dup_wide(g_cfg.url);
            if (wurl) {
                SetWindowText(g_url, wurl);
                free(wurl);
            }
            if (g_cfg.model[0]) {
                WCHAR *wm;

                wm = utf8_dup_wide(g_cfg.model);
                if (wm) {
                    SetWindowText(g_model, wm);
                    free(wm);
                }
            }
        }
        set_status("Set server URL, then Models. Ollama must listen on the LAN.");
        return 0;

    case WM_SIZE:
        layout_children(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_REFRESH:
            start_worker(WORK_LIST);
            break;
        case IDC_SAVE:
            on_save();
            break;
        case IDC_SEND:
            start_worker(WORK_GEN);
            break;
        case IDC_STOP:
            g_cancel = 1;
            http_abort();
            set_status("Stopping...");
            break;
        case IDC_CLEAR:
            SetWindowText(g_output, TEXT(""));
            set_status("Cleared");
            break;
        default:
            break;
        }
        return 0;

    case WM_WORKER_STATUS:
        if (lParam) {
            SetWindowText(g_status, (WCHAR *)lParam);
            free((void *)lParam);
        }
        return 0;

    case WM_WORKER_APPEND:
        if (lParam) {
            append_output((WCHAR *)lParam);
            free((void *)lParam);
        }
        return 0;

    case WM_WORKER_MODELS:
        if (lParam) {
            fill_models((const char *)lParam);
            free((void *)lParam);
        }
        return 0;

    case WM_WORKER_DONE:
        set_busy(0);
        read_fields();
        config_save(&g_cfg);
        if (g_closing) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        g_cancel = 1;
        http_abort();
        if (g_busy) {
            g_closing = 1;
            set_status("Stopping...");
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_hwnd = NULL;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;
    int scr_w;
    int scr_h;

    (void)hPrevInstance;
    (void)lpCmdLine;

    g_inst = hInstance;
    InitCommonControls();
    config_load(&g_cfg);

    if (http_startup() != 0) {
        MessageBox(NULL, TEXT("WSAStartup failed. Check the device network."),
                    TEXT("Ollama CE"), MB_OK | MB_ICONERROR);
        return 1;
    }

    g_font = (HFONT)GetStockObject(SYSTEM_FONT);

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, TEXT("RegisterClass failed"), TEXT("Ollama CE"), MB_OK);
        http_cleanup();
        return 1;
    }

    scr_w = GetSystemMetrics(SM_CXSCREEN);
    scr_h = GetSystemMetrics(SM_CYSCREEN);
    hwnd = CreateWindowEx(WS_EX_CONTROLPARENT, kClassName,
                          TEXT("Ollama CE"),
                          WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                          WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                          0, 0, scr_w, scr_h,
                          NULL, NULL, hInstance, NULL);
    if (!hwnd) {
        MessageBox(NULL, TEXT("CreateWindow failed"), TEXT("Ollama CE"), MB_OK);
        http_cleanup();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow ? nCmdShow : SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    http_cleanup();
    return (int)msg.wParam;
}
