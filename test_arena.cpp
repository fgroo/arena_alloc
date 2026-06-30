#include "arena_alloc.hpp"
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <cassert>

// ---------------------------------------------------------------------------
// Tiny test harness
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do {                                  \
	if (cond) { g_pass++; }                                \
	else { g_fail++; printf("  FAIL: %s:%d: %s\n",         \
		__FILE__, __LINE__, #cond); }                       \
} while (0)

#define RUN_TEST(fn) do {                                 \
	printf("== %s ==\n", #fn); fn();                       \
} while (0)

// ---------------------------------------------------------------------------
// Test data pools (varied length / content to exercise packing & alignment)
// ---------------------------------------------------------------------------
static const char *kFirstNames[] = {
	"Meister", "Timmy", "Günther", "Lars", "Ana", "Wei",
	"Xochipil", "Björk", "Zoe", "Jean-Christophe"
};
static const char *kLastNames[] = {
	"Propper", "Turner", "Jauch", "Vegas", "O'Brien", "Nguyen",
	"McMillan-The-Third", "de la Cruz", "Kowalski", "Stroustrup"
};
static const char *kCities[] = {
	"Berlin", "Tokyo", "São Paulo", "New York", "Reykjavík",
	"Ushuaia", "Ulaanbaatar", "Llanfairpwllgwyngyll", "Paris", "Ouagadougou"
};
static const char *kEmailDomains[] = {
	"example.com", "mail.io", "long-domain-name.company", "x.io", "a.b.c.d"
};

// ---------------------------------------------------------------------------
// Helpers
//
// The arena control struct must start zeroed (arena_create does not memset it),
// so every test value-initialises it with `= t_arena()`.
//
// dup_str() takes an optional checkpoint name: omit it for plain bump allocs
// (checkpoint_slots == 0), pass a name for checkpoint-tagged allocs.
// ---------------------------------------------------------------------------
static char *dup_str(t_arena *a, const char *s, size_t name = 0)
{
	size_t n = strlen(s) + 1;
	char *p = static_cast<char*>(arena_alloc(a, n, sizeof(char), name));
	if (p) memcpy(p, s, n);
	return p;
}

static int is_aligned(const void *p, size_t align)
{
	return ((uintptr_t)p & (align - 1)) == 0;
}

// ---------------------------------------------------------------------------
// 1. Basic string interning (plain mode, no checkpoints)
// ---------------------------------------------------------------------------
static void test_string_intern(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 0);
	CHECK(a.base != NULL);

	char *names[10];
	for (int i = 0; i < 10; i++) {
		names[i] = dup_str(&a, kFirstNames[i]);
		CHECK(names[i] != NULL);
		CHECK(strcmp(names[i], kFirstNames[i]) == 0);
	}
	CHECK(a.offset > 0 && a.offset < a.capacity);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 2. Alignment guarantees (plain mode)
// ---------------------------------------------------------------------------
static void test_alignment(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 0);

	size_t aligns[] = {1, 2, 4, 8, 16, 32, 64, 128};
	for (size_t k = 0; k < sizeof(aligns)/sizeof(aligns[0]); k++) {
		void *p = arena_alloc(&a, 7, aligns[k]);   // odd size on purpose
		CHECK(p != NULL);
		CHECK(is_aligned(p, aligns[k]));
	}
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 3. Record (struct) allocation + packing (plain mode)
// ---------------------------------------------------------------------------
struct Person {
	char	first[16];
	char	last[16];
	int		age;
	double	score;
};

static void test_struct_records(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 0);

	Person *people = static_cast<Person*>(
		arena_alloc(&a, sizeof(Person) * 10, __alignof__(Person)));
	CHECK(people != NULL);
	CHECK(is_aligned(people, __alignof__(Person)));

	for (int i = 0; i < 10; i++) {
		strncpy(people[i].first, kFirstNames[i], 15);
		strncpy(people[i].last, kLastNames[i], 15);
		people[i].first[15] = 0;
		people[i].last[15] = 0;
		people[i].age = 20 + i;
		people[i].score = 1.0 + i * 0.5;
	}
	CHECK(strcmp(people[7].first, kFirstNames[7]) == 0);
	CHECK(people[3].age == 23);
	CHECK(people[9].score == 5.5);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 4. Plain mode is the default: omit checkpoint_name entirely.
//    Verifies that checkpoint_slots == 0 => a plain bump allocator.
// ---------------------------------------------------------------------------
static void test_plain_mode_optional(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 12, 0);
	CHECK(a.checkpoint == NULL);
	CHECK(a.checkpoint_slots == 0);

	char *p = dup_str(&a, kCities[0]);   // no 4th arg -> checkpoint_name defaults to 0
	CHECK(p != NULL);
	CHECK(strcmp(p, kCities[0]) == 0);

	// arena_alloc with an explicit checkpoint_name in a no-checkpoint arena fails
	CHECK(arena_alloc(&a, 16, 8, 1) == NULL);

	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 5. Checkpoint / restore (request-response scenario)
//
// Slots are named dynamically: create with N slots, then tag each allocation
// with any name in [1, N). Names need not be sequential and need not match
// allocation order. restore(name) rewinds to the offset recorded at that name.
//
// NOTE: a name can only be used once ("already taken" guard). Slot 0 is
// reserved (0 means "no checkpoint"); valid names are 1..N-1.
// ---------------------------------------------------------------------------
static void test_checkpoint_restore(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 6);   // 6 slots -> valid names 1..5
	CHECK(a.base != NULL);
	CHECK(a.checkpoint != NULL);
	CHECK(a.checkpoint_slots == 6);

	char *fn   = dup_str(&a, kFirstNames[0],            1);
	char *ln   = dup_str(&a, kLastNames[0],             2);
	char *cred = dup_str(&a, "basic dXNlcjpwdw==",       3);
	char *req  = dup_str(&a, "GET /users/42 HTTP/1.1",   4);
	char *res  = dup_str(&a, "200 OK {\"id\":42}",       5);
	CHECK(fn && ln && cred && req && res);

	size_t req_off = a.checkpoint[4] - 1;   // -1: stored as offset+1
	size_t res_off = a.checkpoint[5] - 1;
	CHECK(req_off < res_off && res_off < a.offset);

	// restore(name=5): drop only the response, keep the request
	void *p1 = arena_restore_with_checkpoint(&a, 5);
	CHECK(p1 != NULL);
	CHECK(a.offset == res_off);
	CHECK(strcmp(req, "GET /users/42 HTTP/1.1") == 0); // request still intact

	// restore(name=4): drop request and response, rewind to request start
	void *p2 = arena_restore_with_checkpoint(&a, 4);
	CHECK(p2 != NULL);
	CHECK(a.offset == req_off);

	// out-of-range restores are rejected
	CHECK(arena_restore_with_checkpoint(&a, 0) == NULL);
	CHECK(arena_restore_with_checkpoint(&a, 6) == NULL);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 6. Dynamic naming: names can be assigned out of order.
//    The recorded offset follows allocation order, not name order.
// ---------------------------------------------------------------------------
static void test_checkpoint_dynamic_names(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 5);   // valid names 1..4

	// assign names in non-sequential order: 3, then 1, then 2
	char *c = dup_str(&a, "gamma", 3);   // allocated first  -> smallest offset
	char *b = dup_str(&a, "alpha", 1);   // allocated second -> middle offset
	char *d = dup_str(&a, "beta",  2);   // allocated third  -> largest offset
	CHECK(c && b && d);

	// offsets follow allocation order, regardless of the name chosen
	CHECK(a.checkpoint[3] < a.checkpoint[1]);
	CHECK(a.checkpoint[1] < a.checkpoint[2]);

	// restore to name 1: drops the name-2 alloc ("beta"); "alpha"(1) and
	// "gamma"(3, before the restore point) stay valid.
	size_t off1 = a.checkpoint[1] - 1;   // -1: stored as offset+1
	void *p = arena_restore_with_checkpoint(&a, 1);
	CHECK(p != NULL);
	CHECK(a.offset == off1);
	CHECK(strcmp(c, "gamma") == 0);   // before restore point -> intact
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 7. A checkpoint name can only be taken once per arena lifetime.
// ---------------------------------------------------------------------------
static void test_checkpoint_already_taken(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 5);

	char *first = dup_str(&a, "one", 1);
	CHECK(first != NULL);

	// reusing name 1 must fail
	CHECK(dup_str(&a, "two", 1) == NULL);

	// a fresh name still works
	CHECK(dup_str(&a, "three", 2) != NULL);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 8. Boundary / out-of-range checks in checkpoint mode.
// ---------------------------------------------------------------------------
static void test_checkpoint_out_of_range(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 3);   // valid names 1,2

	// name 0 always means "plain alloc, no tag" — allowed even in checkpoint mode
	CHECK(dup_str(&a, "x", 0) != NULL);
	// name == slots => out of range
	CHECK(dup_str(&a, "x", 3) == NULL);
	// name > slots => out of range
	CHECK(dup_str(&a, "x", 99) == NULL);

	// valid name works
	CHECK(dup_str(&a, "ok", 1) != NULL);

	// restore out of range rejected
	CHECK(arena_restore_with_checkpoint(&a, 0) == NULL);
	CHECK(arena_restore_with_checkpoint(&a, 3) == NULL);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 9. arena_print_checkpoint runs without crashing and covers every slot.
// ---------------------------------------------------------------------------
static void test_checkpoint_print(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 4);

	dup_str(&a, "first",  1);
	dup_str(&a, "second", 2);

	arena_print_checkpoint(&a);   // should print slots 0..3 (0 unused)

	CHECK(a.checkpoint[1] != 0);
	CHECK(a.checkpoint[2] != 0);
	CHECK(a.checkpoint[0] == 0);  // slot 0 never used
	CHECK(a.checkpoint[3] == 0);  // never tagged
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 10. arena_reset reuses whole buffer (plain mode)
// ---------------------------------------------------------------------------
static void test_reset(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 12, 0);
	for (int i = 0; i < 5; i++) dup_str(&a, kCities[i]);
	CHECK(a.offset > 0);

	arena_reset(&a);
	CHECK(a.offset == 0);

	char *p = dup_str(&a, kCities[6]);
	CHECK(p != NULL);
	CHECK(strcmp(p, kCities[6]) == 0);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 11. Capacity overflow must fail safely
// ---------------------------------------------------------------------------
static void test_overflow(void)
{
	t_arena a = t_arena();
	arena_create(&a, 128, 0);
	CHECK(a.base != NULL);

	void *ok = arena_alloc(&a, 64, 1);
	CHECK(ok != NULL);

	void *too_big = arena_alloc(&a, 1 << 20, 1);   // far too big
	CHECK(too_big == NULL);

	void *edge = arena_alloc(&a, 128, 1);          // still won't fit
	CHECK(edge == NULL);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 12. Many small allocations (packing; plain mode so there is no slot limit)
// ---------------------------------------------------------------------------
static void test_stress_small(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 20, 0);   // 1 MiB
	int n = 5000;
	int ok = 0;
	for (int i = 0; i < n; i++) {
		char *p = dup_str(&a, kCities[i % 10]);
		if (p) ok++;
	}
	CHECK(ok == n);
	CHECK(a.offset < a.capacity);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 13. Mixed-type workload (realistic-ish, plain mode)
// ---------------------------------------------------------------------------
static void test_mixed_workload(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 16, 0);

	char *fn = dup_str(&a, kFirstNames[4]);
	char *ln = dup_str(&a, kLastNames[5]);
	int  *ids = static_cast<int*>(arena_alloc(&a, sizeof(int) * 50, __alignof__(int)));
	double *scores = static_cast<double*>(
		arena_alloc(&a, sizeof(double) * 50, __alignof__(double)));
	char *email = dup_str(&a, kEmailDomains[2]); // "long-domain-name.company"

	CHECK(fn && ln && ids && scores && email);
	CHECK(is_aligned(ids, __alignof__(int)));
	CHECK(is_aligned(scores, __alignof__(double)));
	CHECK(strcmp(email, kEmailDomains[2]) == 0);

	for (int i = 0; i < 50; i++) { ids[i] = i; scores[i] = i * 1.25; }
	CHECK(ids[49] == 49 && scores[7] == 8.75);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 14. Zero-size / edge allocs (plain mode)
// ---------------------------------------------------------------------------
static void test_edge_cases(void)
{
	t_arena a = t_arena();
	arena_create(&a, 1 << 12, 0);
	void *p1 = arena_alloc(&a, 0, 0);
	CHECK(p1 != NULL);                  // zero size still returns a pointer
	CHECK(is_aligned(p1, sizeof(void*)));

	void *p2 = arena_alloc(&a, 1, 1);
	CHECK(p2 != NULL);
	arena_destroy(&a);
}

int main(void)
{
	RUN_TEST(test_string_intern);
	RUN_TEST(test_alignment);
	RUN_TEST(test_struct_records);
	RUN_TEST(test_plain_mode_optional);
	RUN_TEST(test_checkpoint_restore);
	RUN_TEST(test_checkpoint_dynamic_names);
	RUN_TEST(test_checkpoint_already_taken);
	RUN_TEST(test_checkpoint_out_of_range);
	RUN_TEST(test_checkpoint_print);
	RUN_TEST(test_reset);
	RUN_TEST(test_overflow);
	RUN_TEST(test_stress_small);
	RUN_TEST(test_mixed_workload);
	RUN_TEST(test_edge_cases);

	printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
