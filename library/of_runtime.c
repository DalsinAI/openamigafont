/* The C library pieces that HarfBuzz and OpenFont's core use
 * inside openfont.library: memory from one Exec pool. Every call into the
 * core holds the library's lock, so the pool needs no lock of its own.
 * The library runs no C startup and keeps no stdio: the rest of what the
 * engines call (memcpy, strlen, qsort, setjmp) is libnix's, which keeps
 * no state that needs one.
 *
 * Copyright (c) 2026 AmigaChrome Team. OpenFont, MIT licence (LICENSE). */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <stddef.h>
#include <string.h>

#include "of_runtime.h"

static APTR of_pool;

int of_runtime_init(void)
{
    /* puddles of 64 KB; a block of 16 KB or more gets its own */
    of_pool = CreatePool(MEMF_ANY, 65536, 16384);
    return of_pool != NULL;
}

void of_runtime_exit(void)
{
    if (of_pool)
        DeletePool(of_pool);    /* frees every block still in it */
    of_pool = NULL;
}

/* Each block carries its size in front, 8 bytes so doubles stay aligned. */
void *malloc(size_t n)
{
    ULONG *p;
    if (!of_pool || n > 0x7FFFFFF0UL)
        return NULL;
    p = (ULONG *)AllocPooled(of_pool, (ULONG)n + 8);
    if (!p)
        return NULL;
    p[0] = (ULONG)n;
    p[1] = 0x4F464D45UL;        /* "OFME": checked on free */
    return p + 2;
}

void free(void *ptr)
{
    ULONG *p;
    if (!ptr)
        return;
    p = (ULONG *)ptr - 2;
    if (p[1] != 0x4F464D45UL)
        return;                 /* not ours: never free what we didn't hand out */
    p[1] = 0;
    FreePooled(of_pool, p, p[0] + 8);
}

void *calloc(size_t n, size_t m)
{
    void *p;
    if (m && n > (size_t)0x7FFFFFF0UL / m)
        return NULL;
    p = malloc(n * m);
    if (p)
        memset(p, 0, n * m);
    return p;
}

void *realloc(void *ptr, size_t n)
{
    void *np;
    size_t old;
    if (!ptr)
        return malloc(n);
    if (!n) {
        free(ptr);
        return NULL;
    }
    old = ((ULONG *)ptr)[-2];
    if (n <= old && old - n < 256)
        return ptr;             /* shrinking by a little: keep the block */
    np = malloc(n);
    if (!np)
        return NULL;
    memcpy(np, ptr, n < old ? n : old);
    free(ptr);
    return np;
}

/* HarfBuzz is C++ with no exceptions and no virtual calls left pure; these
 * are what its objects can still name. */
void __cxa_pure_virtual(void)
{
}

/* snprintf and vsnprintf, for the few places HarfBuzz formats a number
 * into a buffer (a feature or variation as text). libnix's would bring its
 * stdio and its own allocator into the library. This one knows %d %i %u
 * %x %X %c %s %g and %%, with a width, a '0' flag and the l and h
 * length letters; it never allocates. */
#include <stdarg.h>

struct Fmt {
    char *out;
    size_t size, n;
};

static void fput(struct Fmt *f, char c)
{
    if (f->n + 1 < f->size)
        f->out[f->n] = c;
    f->n++;
}

static void fnum(struct Fmt *f, unsigned long v, unsigned base, int upper, int neg, int width, int zero)
{
    char b[24];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        b[i++] = digits[v % base];
        v /= base;
    } while (v && i < (int)sizeof b);
    if (neg)
        width--;
    if (neg && zero)
        fput(f, '-');
    while (width-- > i)
        fput(f, zero ? '0' : ' ');
    if (neg && !zero)
        fput(f, '-');
    while (i)
        fput(f, b[--i]);
}

int vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
    struct Fmt f;
    f.out = out;
    f.size = size;
    f.n = 0;
    for (; *fmt; ++fmt) {
        int width = 0, zero = 0, lng = 0;
        if (*fmt != '%') {
            fput(&f, *fmt);
            continue;
        }
        ++fmt;
        if (*fmt == '0') {
            zero = 1;
            ++fmt;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') {
            if (*fmt == 'l')
                lng++;
            ++fmt;
        }
        switch (*fmt) {
        case 'd':
        case 'i': {
            long v = lng ? va_arg(ap, long) : va_arg(ap, int);
            fnum(&f, v < 0 ? (unsigned long)-v : (unsigned long)v, 10, 0, v < 0, width, zero);
            break;
        }
        case 'u':
            fnum(&f, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, 0, 0, width, zero);
            break;
        case 'x':
        case 'X':
            fnum(&f, lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, *fmt == 'X', 0, width, zero);
            break;
        case 'c':
            fput(&f, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s)
                fput(&f, *s++);
            break;
        }
        case 'g':
        case 'f': {
            /* up to six decimals, trailing zeros dropped */
            double v = va_arg(ap, double);
            unsigned long ip, fp;
            int neg = v < 0, d;
            if (neg)
                v = -v;
            ip = (unsigned long)v;
            fp = (unsigned long)((v - (double)ip) * 1000000.0 + 0.5);
            if (fp >= 1000000UL) {
                ip++;
                fp -= 1000000UL;
            }
            fnum(&f, ip, 10, 0, neg, 0, 0);
            if (fp) {
                char b[7];
                for (d = 5; d >= 0; --d) {
                    b[d] = (char)('0' + fp % 10);
                    fp /= 10;
                }
                d = 6;
                while (d > 0 && b[d - 1] == '0')
                    d--;
                fput(&f, '.');
                for (int k = 0; k < d; ++k)
                    fput(&f, b[k]);
            }
            break;
        }
        case '%':
            fput(&f, '%');
            break;
        default:
            if (!*fmt)
                --fmt;
            break;
        }
    }
    if (size)
        out[f.n < size ? f.n : size - 1] = 0;
    return (int)f.n;
}

int snprintf(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(out, size, fmt, ap);
    va_end(ap);
    return n;
}

/* strtol and strtoul, in the C locale: libnix's reach for its locale
 * machinery, and that brings its stdio and environment with it. */
#include <limits.h>

static unsigned long of_strtoul_core(const char *s, char **end, int base, int *neg, int *overflow)
{
    const char *p = s;
    unsigned long v = 0;
    int any = 0;
    *neg = 0;
    *overflow = 0;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        ++p;
    if (*p == '-' || *p == '+')
        *neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }
    for (;; ++p) {
        int d;
        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if (*p >= 'a' && *p <= 'z')
            d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')
            d = *p - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        if (v > (ULONG_MAX - (unsigned long)d) / (unsigned long)base)
            *overflow = 1;
        v = v * (unsigned long)base + (unsigned long)d;
        any = 1;
    }
    if (end)
        *end = (char *)(any ? p : s);
    return v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    int neg, overflow;
    unsigned long v = of_strtoul_core(s, end, base, &neg, &overflow);
    if (overflow)
        return ULONG_MAX;
    return neg ? (unsigned long)-(long)v : v;
}

long strtol(const char *s, char **end, int base)
{
    int neg, overflow;
    unsigned long v = of_strtoul_core(s, end, base, &neg, &overflow);
    if (neg) {
        if (overflow || v > (unsigned long)LONG_MAX + 1UL)
            return LONG_MIN;
        return -(long)v;
    }
    if (overflow || v > (unsigned long)LONG_MAX)
        return LONG_MAX;
    return (long)v;
}
