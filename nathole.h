/* Common part */
#define  SIGSRV_PORT    12321
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#define debug(...) fprintf(stderr, __VA_ARGS__)

static inline void die(const char *format, ...) __attribute__((format(printf, 1, 2), noreturn));
static inline void die(const char *format, ...)
{
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
    exit(1);
}
static inline void fatal(const char *name) __attribute__((noreturn));
static inline void fatal(const char *name)
{
    perror(name);
    exit(1);
}

struct peer_addr {
    uint32_t in_addr;
    uint16_t port;
};

#define QUOTE(x) _QUOTE(x)
#define _QUOTE(x) #x
#define SIGSRV_PORT_STR QUOTE(SIGSRV_PORT)
