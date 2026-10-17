/* openfont_io.h's folders on a POSIX host. Copyright (c) 2026 AmigaChrome Team. MIT. */
#include "openfont/openfont.h"
#include "openfont_io.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

int of_io_list_dir(const char *dir, OFDirFn fn, void *arg)
{
    DIR *d;
    struct dirent *e;
    char path[1024];
    struct stat st;
    int rc = OF_OK;

    if (!dir || !fn)
        return OF_ERR_ARGUMENT;
    d = opendir(dir);
    if (!d)
        return OF_ERR_IO;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        if (of_io_join(path, sizeof path, dir, e->d_name) != OF_OK)
            continue;
        if (stat(path, &st) || !S_ISREG(st.st_mode))
            continue;
        rc = fn(path, arg);
        if (rc)
            break;
    }
    closedir(d);
    return rc;
}

int of_io_stamp(const char *path, uint32_t stamp[3])
{
    struct stat st;
    if (!path || !stamp || stat(path, &st))
        return OF_ERR_IO;
    stamp[0] = (uint32_t)((uint64_t)st.st_mtime >> 32);
    stamp[1] = (uint32_t)st.st_mtime;
#if defined(__APPLE__)
    stamp[2] = (uint32_t)st.st_mtimespec.tv_nsec;
#else
    stamp[2] = (uint32_t)st.st_mtim.tv_nsec;
#endif
    return OF_OK;
}
