#include "arena_alloc.hpp"
#include <stdio.h>
#include <string.h>

static char *dup_str(t_arena *a, const char *s, size_t name = 0)
{
	size_t n = strlen(s) + 1;
	char *p = static_cast<char *>(arena_alloc(a, n, sizeof(char), name));
	if (p) memcpy(p, s, n);
	return p;
}

int main(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 10, 3);   // 1 KiB usable, 3 checkpoint slots -> names 1,2
	if (!a.base)
		return 1;

	char *first = dup_str(&a, "Meister", 1);   // dynamically tag name 1
	char *last  = dup_str(&a, "Propper", 2);   // dynamically tag name 2

	printf("first: %s\n", first);
	printf("last : %s\n", last);

	arena_print_checkpoint(&a);

	// restore to name 1: drops `last` and reclaims its space
	arena_restore_with_checkpoint(&a, 1);
	printf("after restore(1), first still ok: %s\n", first);

	arena_destroy(&a);
	return 0;
}
