#ifndef OLLAMA_CE_WINSOCK2_H
#define OLLAMA_CE_WINSOCK2_H

#ifdef HOST_BUILD

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close

#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif

#define sock_errno() (errno)

typedef struct {
    unsigned short wVersion;
    unsigned short wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} WSADATA;

static int WSAStartup(unsigned short version, WSADATA *wsa)
{
    (void)version;
    (void)wsa;
    return 0;
}

static int WSACleanup(void)
{
    return 0;
}

static int sock_set_timeouts(SOCKET s, int timeout_ms)
{
    struct timeval tv;

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv)) != 0) {
        return -1;
    }
    if (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv)) != 0) {
        return -1;
    }
    return 0;
}

#else /* Windows CE */

#include <windows.h>
#include <winsock.h>

#ifndef INVALID_SOCKET
#define INVALID_SOCKET ((SOCKET)(~0))
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef SD_BOTH
#define SD_BOTH 2
#endif

#define sock_errno() ((int)WSAGetLastError())

/*
 * CE 3.0 takes SO_*TIMEO as milliseconds (DWORD), same as desktop Winsock.
 * select() timeouts are ignored on many HPC 2000 stacks, so we never use it.
 */
static int sock_set_timeouts(SOCKET s, int timeout_ms)
{
    int ms;

    ms = timeout_ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&ms, sizeof(ms));
    return 0;
}

#endif /* HOST_BUILD */

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffffUL
#endif

#endif
