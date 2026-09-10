#ifndef OLLAMA_CE_COMPAT_H
#define OLLAMA_CE_COMPAT_H

/*
 * Shared types and helpers for CeGCC/Windows CE and a Linux host build.
 * Windows CE 3.0 (Sigmarion III / HPC 2000) is the primary target.
 */

#ifdef HOST_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#else /* Windows CE / CeGCC */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef snprintf
#define snprintf _snprintf
#endif

#endif /* HOST_BUILD */

#ifndef OLLAMA_CE_VERSION
#define OLLAMA_CE_VERSION "0.1"
#endif

#define OLLAMA_CE_DEFAULT_URL "http://192.168.1.4:11434"
#define OLLAMA_CE_MAX_URL 256
#define OLLAMA_CE_MAX_MODEL 128
#define OLLAMA_CE_MAX_STATUS 256

#endif
