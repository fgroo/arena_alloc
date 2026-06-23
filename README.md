# arena_alloc

A small, single-file **bump allocator with named checkpoints** for C++98.

Memory is backed by an anonymous `mmap` region and handed out by advancing a
single offset. A checkpoint records the offset at a logical point in the
program; restoring to it reclaims everything allocated after that point. There
is **no per-block free** — freed space is recovered only by resetting or
restoring to a checkpoint.

## Features

- O(1) allocation — bump a cursor, no free-list, no fragmentation
- Power-of-two alignment control per allocation
- Named checkpoints (`CHKPT` enum) for rollback to a logical point
- `mmap`/`munmap` backed — no `malloc` overhead, page-aligned regions
- Pure C++98, compiles with `-Wall -Wextra -Werror`
- Ships as a static library (`arena_alloc.a`)

## Build

```sh
make            # build arena_alloc.a
make test       # build and link the test suite against the .a
make fclean     # remove build artifacts
```

Requires `c++` and `ar`. No external dependencies.

## Usage

```cpp
#include "arena_alloc.hpp"

int main()
{
    t_arena a = arena_create(1 << 16);   // map 64 KiB
    if (!a.base)
        return 1;

    char *name = static_cast<char*>(arena_alloc(&a, 64, 16));

    arena_destroy(&a);                   // release the mapping
    return 0;
}
```

### Checkpoints

The arena records a checkpoint on every allocation. Checkpoint slot **N**
holds the offset that existed *before* the Nth allocation (0-indexed), so
restoring to slot **N** keeps allocations `[0..N)` and drops `[N..]`.

The `CHKPT` enum simply names these indices. Because slot index == allocation
index, allocations must happen in enum order for the names to line up:

```cpp
char *fn   = static_cast<char*>(arena_alloc(&a, 32, 8));  // slot 0 FIRST_NAME
char *ln   = static_cast<char*>(arena_alloc(&a, 32, 8));  // slot 1 LAST_NAME
char *cred = static_cast<char*>(arena_alloc(&a, 32, 8));  // slot 2 CREDENTIALS
char *req  = static_cast<char*>(arena_alloc(&a, 64, 8));  // slot 3 REQUEST
char *res  = static_cast<char*>(arena_alloc(&a, 64, 8));  // slot 4 RESPONSE

arena_restore(&a, RESPONSE);  // keep [0..4), drop only `res` and reclaim its space
```

> The `CHKPT` enum is application-specific. Edit it in `arena_alloc.hpp` to
> match the logical stages of your workload.

## API

| Function | Description |
| --- | --- |
| `arena_create(capacity)` | Map a new region; returns an arena (`.base == NULL` on failure). |
| `arena_alloc(a, size, align)` | Bump-allocate `size` bytes with `align` alignment. Returns `NULL` on overflow. |
| `arena_restore(a, checkpoint)` | Rewind to a recorded checkpoint. Must be in `[1, COUNT)`. |
| `arena_reset(a)` | Clear the offset and checkpoints; keep the mapping. |
| `arena_destroy(a)` | `munmap` the region and zero the arena. |

Full per-function documentation is in the Doxygen comments in
[`arena_alloc.hpp`](arena_alloc.hpp).

## Linking from another project

```sh
c++ -std=c++98 -I/path/to/arena_alloc your_app.cpp /path/to/arena_alloc/arena_alloc.a -o your_app
```

You need the header on the include path and the archive on the link line.

## Caveats

- **Not thread-safe.** One arena per thread, or wrap access in a mutex.
- **No per-block free.** Reclaim space only via `arena_reset()` / `arena_restore()`.
- **Pointer lifetime.** A pointer is valid until `arena_destroy()` or until a
  restore rewinds past it.
- **`perror()` on error.** Failure paths print to `stderr`; NULL is also returned.

## License

[MIT](LICENSE)
