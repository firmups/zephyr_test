#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef NATIVE_SIM
#define FILESYSTEM_MOUNT_POINT "/lfs:"
#else
#define FILESYSTEM_MOUNT_POINT "/SD:"
#endif

char const *filesystem_get_mount_point(void);
int filesystem_append_bytes(const char *path, const uint8_t *data, size_t len);

#endif // FILESYSTEM_H
