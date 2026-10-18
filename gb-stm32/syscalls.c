/*-------------------------------------------------------------------------------------------------------------------------------------------
 * syscalls.c - appels systeme newlib manquants (_sbrk, _fstat, _exit...)
 *
 * fs-stdio.c de STECCY fournit _open/_read/_write/_close/_lseek/_isatty sur FatFs. Les autres appels etaient
 * fournis par l'environnement EmBitz ; ils sont ajoutes ici pour une compilation avec arm-none-eabi-gcc + Makefile.
 *-------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License - voir LICENSE
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

extern char     end;                                                    // fin de .bss (script de liens)

extern void *   _sbrk (int incr);
extern int      _fstat (int fd, struct stat * st);
extern void     _exit (int status);
extern int      _kill (int pid, int sig);
extern int      _getpid (void);

void *
_sbrk (int incr)
{
    static char *   heap_end = &end;
    char *          prev     = heap_end;
    char *          sp;

    __asm volatile ("mov %0, sp" : "=r" (sp));

    if (heap_end + incr > sp - 1024)                                    // garder 1 Ko de marge pour la pile
    {
        errno = ENOMEM;
        return (void *) -1;
    }
    heap_end += incr;
    return prev;
}

int
_fstat (int fd, struct stat * st)
{
    (void) fd;
    st->st_mode = S_IFCHR;
    return 0;
}

void
_exit (int status)
{
    (void) status;

    for (;;)
    {
    }
}

int
_kill (int pid, int sig)
{
    (void) pid;
    (void) sig;
    errno = EINVAL;
    return -1;
}

int
_getpid (void)
{
    return 1;
}
