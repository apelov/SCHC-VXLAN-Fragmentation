#include "schcvx/retire.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)
int main(void) {
 struct schcvx_retire t; struct schcvx_retire_record records[2]; uint64_t d=0;
 schcvx_retire_init(&t,records,2,10); CHECK(schcvx_retire_reserve(&t)); CHECK(schcvx_retire_insert(&t,1,42,5)); CHECK(schcvx_retire_contains(&t,1,42)); CHECK(schcvx_retire_next_deadline(&t,&d) && d==15); CHECK(schcvx_retire_reserve(&t)); CHECK(!schcvx_retire_reserve(&t)); CHECK(schcvx_retire_insert(&t,1,43,6)); CHECK(!schcvx_retire_reserve(&t)); schcvx_retire_expire(&t,14); CHECK(schcvx_retire_contains(&t,1,42)); schcvx_retire_expire(&t,15); CHECK(!schcvx_retire_contains(&t,1,42)); CHECK(schcvx_retire_contains(&t,1,43)); CHECK(schcvx_retire_reserve(&t)); return 0;
}
