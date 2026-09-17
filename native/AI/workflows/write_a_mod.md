# Writing a mod

A mod is a folder in `mods/` with a manifest and a `.wasm`. Unbound's own
features go through this exact path -- if something you need is not
expressible here, the ABI is wrong and that is worth saying out loud rather
than working around.

## The shape

```
mods/<id>/
  mod.toml          committed
  <id>.wasm         built, gitignored
  src/*.c           the mod
```

`mod.toml`:

```toml
id = "mymod"
name = "My Mod"
version = "0.1.0"
abi_version = 3          # must equal UNBOUND_ABI_VERSION or the loader refuses it
priority = 500           # ascending; ties break on load order
module = "mymod.wasm"
```

`src/mymod.c`:

```c
#include "unbound_mod.h"

UNBOUND_MOD_MAIN()

void unbound_mod_init(void)
{
    unbound_hook_enable(UNBOUND_HOOK_CAMERA_SETUP, UNBOUND_PRIORITY_NORMAL);
}

void unbound_mod_on_hook(unsigned hook)
{
    void* payload = unbound_mod_payload();
    switch (hook) {
    case UNBOUND_HOOK_CAMERA_SETUP:
        /* read and write fields on (UnboundCameraSetup*) payload */
        break;
    }
}
```

Build it the way CMake builds Unbound's -- freestanding wasm32, no wasi-sdk
and no emsdk needed:

```sh
clang --target=wasm32 -nostdlib -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror \
      -Imods/include -Wl,--no-entry -Wl,--export-memory -Wl,--stack-first \
      -o mods/mymod/mymod.wasm mods/mymod/src/*.c
```

Add the same `add_custom_command` block `native/CMakeLists.txt` uses for
`unbound_mod` if it should build with the tree.

## Rules that are not style

- **Only scalars cross.** `int`, `unsigned`, `float`. No host pointer can be
  dereferenced by a guest, so engine objects are opaque handles that are only
  ever compared. Strings pass as (pointer, length) into the *guest's* memory.
- **Enable hooks only in `unbound_mod_init`.** The registry builds its chains
  once and they are immutable for the run.
- **Append to payload structs, never reorder**, and bump
  `UNBOUND_ABI_VERSION` when you do. The loader refuses a stale manifest with
  a reason; that check is tested and works.
- **Classify every new hook** `UNBOUND_EFFECT_PRESENT` or `..._SIM` in
  `mod.c`'s `hook_effect` table, at the point you define it.

## Adding a hook

Four edits and nothing else:

1. `mods/include/unbound_abi.h` -- the enum value and its payload struct.
2. `native/decomp/shim/decomp_shim.h` -- rename the engine symbol.
3. `native/mod/mod_cobj.c` (or a sibling) -- the interposer. **This is the
   only file that knows which engine symbol is behind the hook**, so a pin
   bump changes it and no mod.
4. `native/mod/mod.c` -- the `hook_effect` row.

Give the defining TU `MELEE_<AREA>_INTERNAL=1` in `native/CMakeLists.txt`, as
`cobj.c` and the interposer both get `MELEE_COBJ_INTERNAL`, or the rename
recurses into itself.

## Verifying

- `MELEE_NO_MODS=1` must reproduce the port exactly. That is the parity gate.
- `ctest --test-dir build/native` -- 33 tests, all must pass.
- Scene-dependent behaviour: test on `--frontend --input
  native/tests/frontend_vs.txt`, **not** `--match`, which never runs the scene
  machinery (see `learnings/mod_system.md`).
- Presentation changes: measure a screenshot rather than eyeballing it. A
  column-brightness profile looking for the largest adjacent-column jump found
  two real bugs from owner screenshots alone. Establish the run-to-run noise
  floor first -- the 240-frame match capture is not byte-reproducible.
