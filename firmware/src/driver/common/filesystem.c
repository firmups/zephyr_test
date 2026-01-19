#include "filesystem.h"

#include <zephyr/fs/fs.h>

int filesystem_append_bytes(const char *path, const uint8_t *data, size_t len)
{
	int rc;
	struct fs_file_t file;

	fs_file_t_init(&file);

	/* Open for write, create if it doesn't exist */
	rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		return rc; // negative errno
	}

	/* Seek to end for append */
	rc = fs_seek(&file, 0, FS_SEEK_END);
	if (rc < 0) {
		fs_close(&file);
		return rc;
	}

	/* Write with partial-write handling */
	size_t written_total = 0;
	while (written_total < len) {
		rc = fs_write(&file, data + written_total, len - written_total);
		if (rc < 0) {
			fs_close(&file);
			return rc;
		}
		if (rc == 0) {
			/* Shouldn't happen: no progress */
			fs_close(&file);
			return -5;
		}
		written_total += (size_t)rc;
	}

	/* Ensure data hits media (especially important for power-loss scenarios) */
	(void)fs_sync(&file);
	fs_close(&file);
	return 0;
}
