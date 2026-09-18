#ifndef MELEE_PLATFORM_PORT_FS_H
#define MELEE_PLATFORM_PORT_FS_H

/*
 * The filesystem calls that do not spell the same on Windows.
 *
 * Only what the port actually uses, and only where the spelling differs --
 * this is not a POSIX emulation layer, and it should not grow into one.  If a
 * new call needs a Windows branch, the branch belongs here rather than as a
 * fifth `#ifdef` at a call site.
 */

#ifdef _WIN32
#include <direct.h>
/* Windows has no POSIX mode bits: `_mkdir` takes the path alone and the
 * directory inherits its parent's ACL.  The mode is accepted and dropped so
 * the call sites stay one expression on both platforms. */
#define melee_mkdir(path, mode) (((void) (mode)), _mkdir(path))
#else
#include <sys/stat.h>
#include <sys/types.h>
#define melee_mkdir(path, mode) mkdir((path), (mode))
#endif

#endif
