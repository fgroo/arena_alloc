#include <cstddef>
#include <cstdio>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>

typedef	struct s_chkpt
{
	int		first_name;
	int		last_name;
	int		credentials;
	int		request;
	int		response;
}	t_chkpt;


typedef	struct s_arena
{
	unsigned char		*base;
	size_t				offset;
	size_t				capacity;
	t_chkpt chkpt;

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


int main(void)
{
	t_arena a;

	a = arena_create(10);

	int i = 0;
	int j = 0;
	char	first_names[4][10] = {"Meister", "Timmy", "Günther", "Lars"};
	while (j < 4) {
		i = 0;
		while (first_names[i][j])
			a.base[a.offset++] = first_names[i++][j];
		a.base[a.offset++] = '\0';
		j++;
	}

	a.chkpt.first_name = a.offset;


	(void)0;
}