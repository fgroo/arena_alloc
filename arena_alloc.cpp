#include "arena_alloc.hpp"
#include <cstdio>

void	arena_create(t_arena *a, size_t capacity, size_t checkpoint_slots)
{
	if (!a || checkpoint_slots > capacity) {
		perror("arena_create: wrong use of arena_create\n");
		return;
	}
	a->base = static_cast<unsigned char*>(mmap(NULL, capacity + (checkpoint_slots * sizeof(size_t)),
				PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
	if (a->base == MAP_FAILED) {
		perror("MAP_FAILED");
		return;
	}
	a->capacity = capacity;
	if (checkpoint_slots) {
		a->checkpoint = reinterpret_cast<size_t*>(a->base + capacity);
		a->checkpoint_slots = checkpoint_slots;
	}
}

void	*arena_alloc(t_arena *a, size_t size, size_t alignment, size_t checkpoint_name)
{
	if (!a) {
		perror("arena_alloc: wrong use of arena_alloc\n");
		return NULL;
	}
	size_t align = alignment ? alignment : sizeof(void*);
	size_t aligned = (a->offset + align - 1) & ~(align - 1);
	if (aligned + size > a->capacity)
		return perror("arena_alloc: wrong use of arena_alloc\n"), static_cast<void *>(NULL);

	void *ptr = a->base + aligned;

	// checkpoint logic
	if (checkpoint_name && checkpoint_name >= a->checkpoint_slots)
		return perror("arena_alloc: checkpoint not initialized or capacity exceeded\n"), static_cast<void *>(NULL);
	if (checkpoint_name && a->checkpoint && a->checkpoint[checkpoint_name])
		return perror("arena_alloc: checkpoint already taken\n"), static_cast<void *>(NULL);
	if (checkpoint_name && a->checkpoint && !a->checkpoint[checkpoint_name])
		arena_add_checkpoint(a, aligned, checkpoint_name);
	// end checkpoint logic
	a->offset = size + aligned;
	return ptr;
}

void	arena_reset(t_arena *a)
{
	if (!a) {
		perror("arena_reset: wrong use of arena_reset\n");
		return;
	}
	if (a->checkpoint) {
		for (size_t i = 0; i < a->checkpoint_slots; i++) {
			a->checkpoint[i] = 0;
		}
		a->checkpoint_slots = 0;
	}
	a->prev = 0;
	a->offset = 0;
}

void	arena_destroy(t_arena *a)
{
	if (!a) {
		perror("arena_destroy: wrong use of arena_destroy\n");
		return;
	}
	if (a->base)
		munmap(a->base, a->capacity + a->checkpoint_slots * sizeof(size_t));
	a->base = NULL;
	a->offset = 0;
	a->capacity = 0;
	a->checkpoint_slots = 0;
}

void	arena_add_checkpoint(t_arena *a, size_t offset, size_t checkpoint_name)
{
	if (!a || !checkpoint_name || checkpoint_name >= a->checkpoint_slots) {
		perror("arena_add_checkpoint: wrong use of arena_add_checkpoint\n");
		return;
	}
	a->prev = offset;
	a->checkpoint[checkpoint_name] = offset + 1;   // +1: 0 stays reserved = "empty"
}

void	arena_print_checkpoint(t_arena *a)
{
	if (!a || !a->checkpoint) {
		perror("arena_print_checkpoint: wrong use of arena_print_checkpoint\n");
		return;
	}
	for (size_t i = 0; i < a->checkpoint_slots; i++)
	{
		if (!a->checkpoint[i])
			printf("checkpoint %zu: (empty)\n", i);
		else
			printf("checkpoint %zu: offset %zu (ptr %p)\n", i, a->checkpoint[i] - 1,
				static_cast<void *>(a->base + (a->checkpoint[i] - 1)));
	}
}

void	*arena_restore_with_checkpoint(t_arena *a, size_t checkpoint) // checkpoints are the first indexes of the multiple blocks of memory in the arena
{
	if (!checkpoint || !a->checkpoint_slots || !a->checkpoint || checkpoint >= a->checkpoint_slots) // if 0, just use arena_reset
		return perror("arena_restore_with_checkpoint: wrong use of arena_restore_with_checkpoint\n"), static_cast<void *>(NULL);
	a->prev = a->offset;
	a->offset = a->checkpoint[checkpoint] - 1;   // undo the +1 stored at add time
	return static_cast<void *>(a->base + a->offset);
}

// something interesting: madvise(MADV_DONTNEED) : when? when i dont need the memory anymore, 
// but i want to keep the mapping for later use. an real example scenario? e.g. a web server that handles multiple requests in a short period of time.