# arena_alloc

A small, single-file **bump allocator with optional, dynamically-named
checkpoints** for C++98.

Memory is backed by an anonymous `mmap` region and handed out by advancing a
single offset. A checkpoint records the offset at a logical point in the
program; restoring to it reclaims everything allocated after that point. There
is **no per-block free** — freed space is recovered only by resetting or
restoring to a checkpoint.

## Features

- O(1) allocation — bump a cursor, no free-list, no fragmentation
- Power-of-two alignment control per allocation
- **Dynamic checkpoints** — request N slots at creation, tag allocations with
  any name in `[1, N)`; names need not be sequential or match alloc order
- Checkpoints are **optional** — pass `0` (the default) for a plain bump alloc,
  even in a checkpoint-capable arena. Mix tagged and untagged allocs freely.
- `mmap`/`munmap` backed — no `malloc` overhead, page-aligned regions
- Pure C++98, compiles with `-Wall -Wextra -Werror`
- Ships as a static library (`arena_alloc.a`)

## Build

```sh
make            # build arena_alloc.a
make test       # build and run the test suite
make fclean     # remove build artifacts
```

Requires `c++` and `ar`. No external dependencies.

## Usage

```cpp
#include "arena_alloc.hpp"

int main()
{
    t_arena a = t_arena();
    arena_create(&a, 1 << 16, 0);          // 64 KiB usable, no checkpoints
    if (!a.base)
        return 1;

    char *name = static_cast<char*>(arena_alloc(&a, 64, 16));

    arena_destroy(&a);                     // release the mapping
    return 0;
}
```

The caller owns the `t_arena` struct (stack or heap); value-initialise it with
`t_arena a = t_arena();` before passing it to `arena_create()`.

### Checkpoints

Pass a non-zero `checkpoint_slots` to `arena_create()` to enable checkpoints.
Then tag individual allocations with `checkpoint_name` (4th arg of
`arena_alloc()`):

```cpp
t_arena a = t_arena();
arena_create(&a, 1 << 16, 5);              // 5 slots -> valid names 1..4

char *fn   = static_cast<char*>(arena_alloc(&a, 32, 8, 1));  // tag 1
char *ln   = static_cast<char*>(arena_alloc(&a, 32, 8, 2));  // tag 2
char *req  = static_cast<char*>(arena_alloc(&a, 64, 8, 4));  // tag 4 (skip 3)
char *plain = static_cast<char*>(arena_alloc(&a, 16, 8));    // no tag (0)

arena_restore_with_checkpoint(&a, 2);      // drop ln and everything after it
```

Semantics:

- **Names are arbitrary integers in `[1, checkpoint_slots)`.** They do not need
  to be assigned in allocation order, and you may skip names. The recorded
  offset always follows allocation order, not name order.
- **Each name can be used once** per arena lifetime; reusing one fails with
  `"checkpoint already taken"`.
- **`checkpoint_name == 0` (the default) records nothing.** It's allowed in
  both plain and checkpoint modes, so you can mix tagged and untagged
  allocations in the same arena.
- `arena_restore_with_checkpoint(&a, name)` rewinds the bump cursor to the
  offset recorded under `name`, dropping everything allocated after it.

## API

| Function | Description |
| --- | --- |
| `arena_create(&a, capacity, slots)` | Initialise an arena: map `capacity` usable bytes plus checkpoint storage. `a->base == NULL` on failure. |
| `arena_alloc(a, size, align, name=0)` | Bump-allocate `size` bytes with `align` alignment. Optional `name` records a checkpoint; `NULL` on overflow / bad name. |
| `arena_restore_with_checkpoint(a, name)` | Rewind to the checkpoint recorded under `name`. `name` in `[1, slots)`; `NULL` on bad name. |
| `arena_reset(a)` | Clear the offset and checkpoints; keep the mapping. |
| `arena_destroy(a)` | `munmap` the region and zero the arena. |
| `arena_print_checkpoint(a)` | Dump every checkpoint slot to stdout (for debugging). |
| `arena_add_checkpoint(a, offset, name)` | Low-level: record an arbitrary offset under `name` without allocating. |

Full per-function documentation is in the Doxygen comments in
[`arena_alloc.hpp`](arena_alloc.hpp).

## Linking from another project

```sh
c++ -std=c++98 -I/path/to/arena_alloc your_app.cpp /path/to/arena_alloc/arena_alloc.a -o your_app
```

You need the header on the include path and the archive on the link line.

## Caveats

- **Not thread-safe.** One arena per thread, or wrap access in a mutex.
- **No per-block free.** Reclaim space only via `arena_reset()` /
  `arena_restore_with_checkpoint()`.
- **Pointer lifetime.** A pointer is valid until `arena_destroy()` or until a
  restore rewinds past it.
- **Checkpoints are bounded.** `checkpoint_slots` is fixed at creation; asking
  for more names than allocated fails. Use plain allocs (`name = 0`) for
  unbounded bump allocation.
- **`perror()` on error.** Failure paths print to `stderr`; `NULL` is also
  returned.

## License

[MIT](LICENSE)
