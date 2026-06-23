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
// ---------------------------------------------------------------------------
static char *dup_str(t_arena *a, const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = static_cast<char*>(arena_alloc(a, n, sizeof(char)));
	if (p) memcpy(p, s, n);
	return p;
}

static int is_aligned(const void *p, size_t align)
{
	return ((uintptr_t)p & (align - 1)) == 0;
}

// ---------------------------------------------------------------------------
// 1. Basic string interning
// ---------------------------------------------------------------------------
static void test_string_intern(void)
{
	t_arena a = arena_create(1 << 16);
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
// 2. Alignment guarantees
// ---------------------------------------------------------------------------
static void test_alignment(void)
{
	t_arena a = arena_create(1 << 16);

	size_t aligns[] = {1, 2, 4, 8, 16, 32, 64, 128};
	for (size_t k = 0; k < sizeof(aligns)/sizeof(aligns[0]); k++) {
		void *p = arena_alloc(&a, 7, aligns[k]);   // odd size on purpose
		CHECK(p != NULL);
		CHECK(is_aligned(p, aligns[k]));
	}
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 3. Record (struct) allocation + packing
// ---------------------------------------------------------------------------
struct Person {
	char	first[16];
	char	last[16];
	int		age;
	double	score;
};

static void test_struct_records(void)
{
	t_arena a = arena_create(1 << 16);

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
// 4. Checkpoint / restore (request-response scenario)
//
// NOTE on prototype semantics: arena_auto_checkpoint records the offset that
// existed *before* the Nth allocation into checkpoints[N]. Because the CHKPT
// enum maps name->index, allocations MUST happen in enum order for
// arena_restore(a, REQUEST) to point at the right place.
//   restore(N)  -> offset = checkpoints[N] = start of the Nth allocation,
//                  i.e. allocations [0..N) are kept, [N..] are dropped.
// ---------------------------------------------------------------------------
static void test_checkpoint_restore(void)
{
	t_arena a = arena_create(1 << 16);

	// allocate strictly in enum order so indices line up
	char *fn   = dup_str(&a, kFirstNames[0]);           // i:0 -> FIRST_NAME
	char *ln   = dup_str(&a, kLastNames[0]);            // i:1 -> LAST_NAME
	char *cred = dup_str(&a, "basic dXNlcjpwdw==");      // i:2 -> CREDENTIALS
	char *req  = dup_str(&a, "GET /users/42 HTTP/1.1");  // i:3 -> REQUEST
	char *res  = dup_str(&a, "200 OK {\"id\":42}");      // i:4 -> RESPONSE
	CHECK(fn && ln && cred && req && res);

	size_t req_start   = a.checkpoints[REQUEST];
	size_t res_start   = a.checkpoints[RESPONSE];
	CHECK(req_start < res_start && res_start < a.offset);

	// restore(RESPONSE): drop only the response, keep the request
	void *p1 = arena_restore(&a, RESPONSE);
	CHECK(p1 != NULL);
	CHECK(a.offset == res_start);
	CHECK(strcmp(req, "GET /users/42 HTTP/1.1") == 0); // request still intact

	// restore(REQUEST): drop request and response, rewind to request start
	void *p2 = arena_restore(&a, REQUEST);
	CHECK(p2 != NULL);
	CHECK(a.offset == req_start);

	// re-use the reclaimed space for a new request
	char *req2 = dup_str(&a, "POST /login");
	CHECK(req2 != NULL);
	CHECK((void*)req2 == (void*)(a.base + req_start));
	CHECK(strcmp(req2, "POST /login") == 0);

	// restore(0) / out-of-range are intentionally rejected by the prototype
	CHECK(arena_restore(&a, 0) == NULL);
	CHECK(arena_restore(&a, COUNT) == NULL);
	arena_destroy(&a);
}

// ---------------------------------------------------------------------------
// 5. arena_reset reuses whole buffer
// ---------------------------------------------------------------------------
static void test_reset(void)
{
	t_arena a = arena_create(1 << 12);
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
// 6. Capacity overflow must fail safely
// ---------------------------------------------------------------------------
static void test_overflow(void)
{
	t_arena a = arena_create(128);
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
// 7. Many small allocations (packing + checkpoint table bound)
// ---------------------------------------------------------------------------
static void test_stress_small(void)
{
	t_arena a = arena_create(1 << 20);   // 1 MiB
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
// 8. Mixed-type workload (realistic-ish)
// ---------------------------------------------------------------------------
static void test_mixed_workload(void)
{
	t_arena a = arena_create(1 << 16);

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
// 9. Zero-size / edge allocs
// ---------------------------------------------------------------------------
static void test_edge_cases(void)
{
	t_arena a = arena_create(1 << 12);
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
	RUN_TEST(test_checkpoint_restore);
	RUN_TEST(test_reset);
	RUN_TEST(test_overflow);
	RUN_TEST(test_stress_small);
	RUN_TEST(test_mixed_workload);
	RUN_TEST(test_edge_cases);

	printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
