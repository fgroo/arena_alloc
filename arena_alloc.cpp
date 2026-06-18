#include <cstddef>
#include <cstdio>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>




// typedef	struct s_chkpt
// {
// 	static size_t	last;

// 	void	*first_name;
// 	void	*last_name;
// 	void	*credentials;
// 	void	*request;
// 	void	*response;
// }	t_chkpt;


// inline void	arena_auto_checkpoint(t_arena *a)
// {
// 	a->chkpt.last++; // using last as a index of the t_chkpt struct and also as a counter of the checkpoints
// 	a->chkpt[a->chkpt.last] = a->offset; // 
// }


enum CHKPT
{
	FIRST_NAME = 0,
	LAST_NAME,
	CREDENTIALS,
	REQUEST,
	RESPONSE,
	COUNT
};

typedef	struct s_arena
{
	unsigned char	*base;
	size_t			offset;
	size_t			capacity;

	size_t			last; // optional
	void			*checkpoints[COUNT];
}	t_arena;

t_arena arena_create(size_t capacity)
{
	t_arena a = {0};
	a.base = static_cast<unsigned char*>(mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));

	a.capacity = capacity;
	if (a.base == MAP_FAILED)
		return perror("MAP_FAILED\n"), (t_arena){0};
	return a;
}

void	*arena_alloc(t_arena *a, size_t size, size_t alignment)
{
	size_t align = alignment ? alignment : sizeof(void*);
	size_t aligned = (a->offset + align - 1) & ~(align - 1);
	if (aligned + size > a->capacity)
		return perror("wrong use of arena_alloc\n"), (void *)NULL;
	
	void	*ptr = a->base + aligned;
	a->last = a->offset;
	a->offset = size + aligned;
	return ptr;
}



void	*arena_restore(t_arena *a, size_t checkpoint) // TODO: doesent make any sense
{
	if (!checkpoint || checkpoint > a->last)
		return perror("wrong use of arena_restore\n"), (void*)NULL;
	a->last = a->offset;
	a->offset = a->last - ; // setting the offset of the selected checkpoint
	return a->checkpoints[checkpoint];
}

void	arena_reset(t_arena *a)
{
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

int main(void)
{
	t_arena a;

	a = arena_create(1 << 10);

	int j = 0;
	char	first_names[4][10] = {"Meister", "Timmy", "Günther", "Lars"};
	char *first_name_ptr = static_cast<char*>(arena_alloc(&a, sizeof(char) * 100, sizeof(char)));
	int first_name_len = 0;
	while (j < 4) {
		int i = 0;
		while (first_names[j][i])
			first_name_ptr[first_name_len++] = first_names[j][i++];
		first_name_ptr[first_name_len++] = 0;
		j++;
	}
	printf("%s\n", first_name_ptr);
	// question: do i need a munmap?
	(void)0;
}


// something interesting: madvise(MADV_DONTNEED) : when? when i dont need the memory anymore, 
// but i want to keep the mapping for later use. an real example scenario? e.g. a web server that handles multiple requests in a short period of time.