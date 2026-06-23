#include "arena_alloc.hpp"
#include <stdio.h>


int main(void)
{
	t_arena a = arena_create(1 << 10);

	char first_names[4][10] = {"Meister", "Timmy", "Günther", "Lars"};
	char last_names[4][10]  = {"Propper", "Turner", "Jauch", "Vegas"};
	char credentials[4][10] = {"root", "admin", "guest", "svc"};

	char *first_name_ptr = intern_strings(&a, first_names, 4); // ckpt FIRST_NAME
	char *last_name_ptr  = intern_strings(&a, last_names, 4);  // ckpt LAST_NAME
	char *cred_ptr       = intern_strings(&a, credentials, 4); // ckpt CREDENTIALS

	printf("first: %s\n", first_name_ptr);
	printf("last : %s\n", last_name_ptr);
	printf("cred : %s\n", cred_ptr);

	// restore back to LAST_NAME: drops credentials and everything after it
	arena_restore(&a, LAST_NAME);
	printf("after restore(LAST_NAME), first still ok: %s\n", first_name_ptr);

	arena_destroy(&a);
}