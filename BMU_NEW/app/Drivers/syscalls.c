#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "rs232.h"

#define SYSCALLS_RS232_TIMEOUT_MS 100u

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    if (st) {
        st->st_mode = S_IFCHR;
    }
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len)
{
    (void)file;

    if ((ptr == NULL) || (len < 0)) {
        errno = EINVAL;
        return -1;
    }

    /* 标准输入采用非阻塞方式，返回当前已经收到的数据。 */
    return (int)rs232_read((uint8_t *)ptr, (size_t)len);
}

int _write(int file, char *ptr, int len)
{
    (void)file;

    if ((ptr == NULL) || (len < 0)) {
        errno = EINVAL;
        return -1;
    }

    /* 将printf等标准输出重定向到板载隔离RS232接口。 */
    if (rs232_write((const uint8_t *)ptr,
                    (size_t)len,
                    SYSCALLS_RS232_TIMEOUT_MS) != RS232_STATUS_OK) {
        errno = EIO;
        return -1;
    }

    return len;
}

void *_sbrk(ptrdiff_t incr)
{
    extern char end;
    static char *heap_end;
    char *prev_heap_end;

    if (heap_end == 0) {
        heap_end = &end;
    }

    prev_heap_end = heap_end;
    heap_end += incr;
    return prev_heap_end;
}
