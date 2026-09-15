/*
 * P-757: a translation unit whose only job is to make the compiler emit DWARF
 * for every type the converter walks.
 *
 * GCC emits debug info only for types a TU actually *uses*, so scraping the
 * ordinary objects gives whichever layouts happened to be referenced -- the
 * set silently changes as unrelated code changes.  Including the headers here
 * and building with `-g3 -fno-eliminate-unused-debug-types` makes the set
 * explicit and complete instead.
 *
 * It must be compiled with the same `-m32 -malign-double` and layout options
 * as the rest of the port, or the offsets it reports are not the ones the
 * running game uses.  native/CMakeLists.txt does that; see the
 * `melee_dwarf_types` target.
 */
#include <melee/ef/types.h>
#include <melee/ft/ftcpuattack.h>
#include <melee/ft/types.h>
#include <melee/gr/types.h>
#include <melee/it/types.h>
#include <melee/lb/lbanim.h>
#include <melee/mp/types.h>
#include <melee/sc/types.h>
#include <melee/ty/toy.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/particle.h>
#include <sysdolphin/baselib/spline.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/sobjlib.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/wobj.h>

/* Nothing here is linked; the object exists to be read with objdump. */
int melee_dwarf_types_unused;
