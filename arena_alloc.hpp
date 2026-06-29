#ifndef ARENA_ALLOC_HPP
# define ARENA_ALLOC_HPP

# include <cstddef>
# include <sys/mman.h>

/**
 * @file arena_alloc.hpp
 * @brief Bump allocator with named checkpoints.
 *
 * Allocates by bumping a single offset into an mmap'd region. A checkpoint
 * records the offset at a logical point in the program; restoring to it
 * reclaims everything allocated after that point. There is no per-block free.
 *
 * Typical usage:
 * @code
 *   t_arena a = arena_create(1 << 16);
 *   if (!a.base) return 1;                 // allocation failed
 *
 *   char *p = static_cast<char*>(arena_alloc(&a, 64, 16));
 *   ...
 *   arena_destroy(&a);                      // release the mapping
 * @endcode
 *
 * @note Not thread-safe. Pair every arena_create() with an arena_destroy().
 */

/**
 * @brief Named checkpoint indices.
 *
 * Maps a logical stage of the program to a slot in t_arena::checkpoints.
 * The enum is application-specific; adapt the names/count to the workload.
 * COUNT is the sentinel marking the number of slots.
 */
// enum CHKPT // DEPRECATED
// {
// 	FIRST_NAME = 0,   /**< First checkpoint slot. */
// 	LAST_NAME,        /**< Second checkpoint slot. */
// 	CREDENTIALS,      /**< Third checkpoint slot. */
// 	REQUEST,          /**< Fourth checkpoint slot. */
// 	RESPONSE,         /**< Fifth checkpoint slot. */
// 	COUNT             /**< Number of checkpoint slots (sentinel). */
// };

/**
 * @brief Arena state.
 *
 * All fields are managed by the library; callers may read `offset`,
 * `capacity`, and `checkpoints[]` but must not mutate them directly.
 */
typedef struct s_arena
{
	unsigned char	*base;               /**< Start of the mmap'd region. NULL when empty/destroyed. */
	size_t			offset;              /**< Current bump cursor, in bytes from base. */
	size_t			capacity;            /**< Total bytes mapped. */

	size_t			checkpoint_slots;/**< Number of allocations since creation (next checkpoint slot). */
	size_t			prev;                /**< Previous offset, for optional debugging. */
	size_t			*checkpoint;         /**< optional: dynamic offset array of size checkpoint_slots */
}				t_arena;

/**
 * @brief Create a new arena backed by an anonymous mmap region.
 *
 * @param capacity  Bytes to map. Must be > 0.
 * @return          Zero-initialised arena. On success base != NULL; on
 *                  failure base == NULL, capacity is unset, and an error
 *                  message is printed via perror().
 *
 * @code
 *   t_arena a = arena_create(1 << 16);
 *   if (!a.base) { / handle failure / }
 * @endcode
 */
t_arena	*arena_create(size_t capacity, size_t checkpoint_slots);

/**
 * @brief Bump-allocate `size` bytes with the requested alignment.
 *
 * @param a          Arena to allocate from. Must be non-NULL and created.
 * @param size       Bytes to reserve. 0 is allowed and returns a valid
 *                   aligned pointer.
 * @param alignment  Power-of-two alignment constraint. 0 falls back to
 *                   sizeof(void*).
 * @return           Pointer into the arena, or NULL if the request would
 *                   overflow capacity (an error is printed via perror()).
 *
 * @warning Returned pointers stay valid only until arena_destroy(), or
 *          until a restore to a checkpoint at/before them. Restoring past
 *          a pointer invalidates it.
 */
void	*arena_alloc(t_arena *a, size_t size, size_t alignment);

/**
 * @brief Roll back to a previously recorded checkpoint.
 *
 * Sets the bump cursor to the offset stored at `checkpoint`, dropping every
 * allocation made after that point. Future allocations reuse the reclaimed
 * space.
 *
 * @param a           Arena to rewind. Must be non-NULL.
 * @param checkpoint  Index into t_arena::checkpoints (a CHKPT value).
 *                    Must be in [1, COUNT). 0 and COUNT are rejected.
 * @return            Pointer to the restored offset within the arena, or
 *                    NULL on an out-of-range checkpoint (error via perror()).
 *
 * @note Each arena_alloc() records a checkpoint, so slot N corresponds to
 *       the Nth allocation. Restoring invalidates every pointer obtained
 *       after that allocation.
 */
void	*arena_restore(t_arena *a, size_t checkpoint);

/**
 * @brief Reset the arena to an empty state without releasing the mapping.
 *
 * Clears the offset and all recorded checkpoints. The mmap'd region stays
 * mapped and is reused from the start.
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
 * @param a  Arena to destroy. NULL is accepted as a no-op.
 */
void	arena_destroy(t_arena *a);

#endif
