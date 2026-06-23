#include "arena_alloc.hpp"
#include <cstdio>

static void	arena_auto_checkpoint(t_arena *a, size_t offset)
{
	a->prev = offset;
	if (a->i < COUNT)
		a->checkpoints[a->i] = offset;
	a->i++;
}

t_arena	arena_create(size_t capacity)
{
	t_arena a = t_arena();
	a.base = static_cast<unsigned char*>(mmap(NULL, capacity,
				PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
	a.capacity = capacity;
	if (a.base == MAP_FAILED)
		return perror("MAP_FAILED\n"), t_arena();
	return a;
}

void	*arena_alloc(t_arena *a, size_t size, size_t alignment)
{
	size_t align = alignment ? alignment : sizeof(void*);
	size_t aligned = (a->offset + align - 1) & ~(align - 1);
	if (aligned + size > a->capacity)
		return perror("wrong use of arena_alloc\n"), static_cast<void *>(NULL);

	void *ptr = a->base + aligned;
	arena_auto_checkpoint(a, a->offset);
	a->offset = size + aligned;
	return ptr;
}

void	*arena_restore(t_arena *a, size_t checkpoint) // checkpoints are the first indexes of the multiple blocks of memory in the arena
{
	if (!checkpoint || checkpoint >= COUNT) // if 0, just use arena_reset
		return perror("wrong use of arena_restore\n"), static_cast<void *>(NULL);
	a->prev = a->offset;
	a->offset = a->checkpoints[checkpoint]; 
	a->i = checkpoint;
	return static_cast<void *>(a->base + a->checkpoints[checkpoint]);
}

void	arena_reset(t_arena *a)
{
	for (size_t i = 0; i < COUNT; i++)
		a->checkpoints[i] = 0;
	a->prev = 0;
	a->i = 0;
	a->offset = 0;
}

void	arena_destroy(t_arena *a)
{
	if (a && a->base)
		munmap(a->base, a->capacity);
	a->base = NULL;
	a->offset = 0;
	a->capacity = 0;
}

// something interesting: madvise(MADV_DONTNEED) : when? when i dont need the memory anymore, 
// but i want to keep the mapping for later use. an real example scenario? e.g. a web server that handles multiple requests in a short period of time.