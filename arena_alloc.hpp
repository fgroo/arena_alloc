#ifndef ARENA_ALLOC_HPP
# define ARENA_ALLOC_HPP

# include <cstddef>
# include <sys/mman.h>

enum CHKPT
{
	FIRST_NAME = 0,
	LAST_NAME,
	CREDENTIALS,
	REQUEST,
	RESPONSE,
	COUNT
};

typedef struct s_arena
{
	unsigned char	*base;
	size_t			offset;
	size_t			capacity;

	size_t			i;
	size_t			prev;
	size_t			checkpoints[COUNT];
}				t_arena;

t_arena	arena_create(size_t capacity);
void	*arena_alloc(t_arena *a, size_t size, size_t alignment);
void	*arena_restore(t_arena *a, size_t checkpoint);
void	arena_reset(t_arena *a);
void	arena_destroy(t_arena *a);

#endif
