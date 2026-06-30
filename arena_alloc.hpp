#ifndef ARENA_ALLOC_HPP
# define ARENA_ALLOC_HPP

# include <cstddef>
# include <sys/mman.h>

/**
 * @file arena_alloc.hpp
 * @brief Bump allocator with optional, dynamically-named checkpoints.
 *
 * Allocates by bumping a single offset into an mmap'd region. A checkpoint
 * records the offset at a logical point in the program; restoring to it
 * reclaims everything allocated after that point. There is no per-block free.
 *
 * Checkpoints are dynamic: at creation you ask for N slots, then tag
 * individual allocations with an arbitrary name in [1, N). Passing 0 (the
 * default) means "don't record a checkpoint" — a plain bump alloc. The same
 * arena can mix tagged and untagged allocations freely.
 *
 * Typical usage:
 * @code
 *   t_arena a = t_arena();
 *   arena_create(&a, 1 << 16, 5);            // 64 KiB usable, 5 checkpoint slots
 *   if (!a.base) return 1;                   // allocation failed
 *
 *   char *plain = static_cast<char*>(arena_alloc(&a, 64, 16));        // no tag
 *   char *tag   = static_cast<char*>(arena_alloc(&a, 64, 16, 1));     // tag name 1
 *   ...
 *   arena_restore_with_checkpoint(&a, 1);    // drop tag and everything after
 *   arena_destroy(&a);                       // release the mapping
 * @endcode
 *
 * @note Not thread-safe. Pair every arena_create() with an arena_destroy().
 * @note The caller owns the t_arena struct (stack or heap); arena_create()
 *       only initialises it and owns the mmap'd memory.
 */

/**
 * @brief Arena state.
 *
 * All fields are managed by the library; callers may read `offset`,
 * `capacity`, `checkpoint_slots`, and `checkpoint[]` but must not mutate them
 * directly. Value-initialise with `t_arena a = t_arena();` before passing it
 * to arena_create().
 */
typedef struct s_arena
{
	unsigned char	*base;            /**< Start of the mmap'd region. NULL when empty/destroyed or on alloc failure. */
	size_t			offset;           /**< Current bump cursor, in bytes from base. */
	size_t			capacity;         /**< Usable allocation bytes (excludes checkpoint storage). */

	size_t			checkpoint_slots; /**< Number of checkpoint slots requested at creation. 0 = plain mode. */
	size_t			prev;             /**< Previous offset, for optional debugging. */
	size_t			*checkpoint;      /**< Array of recorded offsets (size checkpoint_slots). NULL in plain mode. Index 0 is unused (0 = "no tag"). */
}				t_arena;

/**
 * @brief Initialise an arena backed by an anonymous mmap region.
 *
 * The caller owns the t_arena struct. Maps `capacity` usable bytes plus, when
 * `checkpoint_slots > 0`, an extra `checkpoint_slots * sizeof(size_t)` bytes
 * laid out immediately after the usable region to hold the checkpoint table.
 *
 * @param a                Arena to initialise. Must be non-NULL. Should be
 *                         value-initialised beforehand (`t_arena a = t_arena();`).
 * @param capacity         Usable allocation bytes. Must be > 0.
 * @param checkpoint_slots Number of checkpoint slots. 0 disables checkpoints
 *                         (plain bump allocator). Pass `> capacity` to be
 *                         rejected.
 * @return                 Nothing. On failure `a->base == NULL` and an error
 *                         is printed via perror().
 *
 * @code
 *   t_arena a = t_arena();
 *   arena_create(&a, 1 << 16, 5);
 *   if (!a.base) { / * handle failure * / }
 * @endcode
 */
void	arena_create(t_arena *a, size_t capacity, size_t checkpoint_slots);

/**
 * @brief Bump-allocate `size` bytes with the requested alignment.
 *
 * @param a               Arena to allocate from. Must be non-NULL and created.
 * @param size            Bytes to reserve. 0 is allowed and returns a valid
 *                        aligned pointer.
 * @param alignment       Power-of-two alignment constraint. 0 falls back to
 *                        sizeof(void*).
 * @param checkpoint_name Optional checkpoint tag. 0 (the default) records
 *                        nothing — a plain alloc, allowed in both modes.
 *                        A value in [1, checkpoint_slots) tags this allocation
 *                        and records the current offset at that slot. Each tag
 *                        can be used at most once per arena lifetime; reusing
 *                        one fails with "already taken".
 * @return                Pointer into the arena, or NULL if the request would
 *                        overflow capacity or the checkpoint name is out of
 *                        range / already taken (error printed via perror()).
 *
 * @warning Returned pointers stay valid only until arena_destroy(), or until
 *          a restore to a checkpoint at/before them. Restoring past a pointer
 *          invalidates it.
 */
void	*arena_alloc(t_arena *a, size_t size, size_t alignment,
					size_t checkpoint_name = 0);

/**
 * @brief Roll back to a previously recorded checkpoint.
 *
 * Sets the bump cursor to the offset stored under `checkpoint_name`, dropping
 * every allocation made after that point. Future allocations reuse the
 * reclaimed space.
 *
 * @param a              Arena to rewind. Must be non-NULL and in checkpoint mode.
 * @param checkpoint_name Slot index in [1, checkpoint_slots). 0 is rejected
 *                       (use arena_reset() for a full reset); values >=
 *                       checkpoint_slots are rejected.
 * @return               Pointer to the restored offset within the arena, or
 *                       NULL on an out-of-range name or plain-mode arena
 *                       (error via perror()).
 *
 * @note Restoring invalidates every pointer obtained after that checkpoint.
 */
void	*arena_restore_with_checkpoint(t_arena *a, size_t checkpoint_name);

/**
 * @brief Reset the arena to an empty state without releasing the mapping.
 *
 * Clears the offset and all recorded checkpoints. The mmap'd region stays
 * mapped and is reused from the start.
 *
 * @note In checkpoint mode this also zeroes the slot count, so the arena
 *       becomes plain-mode after reset.
 *
 * @param a  Arena to reset. Must be non-NULL.
 */
void	arena_reset(t_arena *a);

/**
 * @brief Release the arena's mmap'd memory.
 *
 * Calls munmap() on the backing region and zeroes the arena's fields. The
 * arena must not be used after this call.
 *
 * @param a  Arena to destroy. NULL is rejected with an error (no-op).
 */
void	arena_destroy(t_arena *a);

/**
 * @brief Record a checkpoint manually (low-level helper).
 *
 * Normally arena_alloc() records checkpoints via its `checkpoint_name` arg.
 * This entry point lets you tag an arbitrary offset directly. It does not
 * allocate memory.
 *
 * @param a              Arena. Must be non-NULL and in checkpoint mode.
 * @param offset         Offset to record (stored internally as offset+1).
 * @param checkpoint_name Slot in [1, checkpoint_slots).
 */
void	arena_add_checkpoint(t_arena *a, size_t offset, size_t checkpoint_name);

/**
 * @brief Print all checkpoint slots to stdout.
 *
 * Empty slots print as "(empty)"; used slots print their offset and pointer.
 *
 * @param a  Arena to inspect. NULL or a plain-mode arena prints an error.
 */
void	arena_print_checkpoint(t_arena *a);

#endif
