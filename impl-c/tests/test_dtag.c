#include "schcvx/dtag.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
 struct schcvx_dtag s; uint64_t v;
 CHECK(!schcvx_dtag_init(&s,32,UINT32_MAX,100,1000000000,2,2));
 CHECK(schcvx_dtag_allocate(&s,1000000099,&v)==-SCHCVX_E_DTAG_QUARANTINE);
 CHECK(!schcvx_dtag_allocate(&s,1000000100,&v) && v==UINT32_MAX);
 CHECK(!schcvx_dtag_allocate(&s,1000000100,&v) && v==0);
 CHECK(schcvx_dtag_allocate(&s,1000000100,&v)==-SCHCVX_E_DTAG_RATE);
 CHECK(schcvx_dtag_allocate(&s,1500000099,&v)==-SCHCVX_E_DTAG_RATE);
 CHECK(!schcvx_dtag_allocate(&s,1500000100,&v) && v==1);
 CHECK(schcvx_dtag_init(&s,32,0,0,UINT64_C(4294967295000000000),1,1)==-SCHCVX_E_DTAG_CONFIG);
 CHECK(!schcvx_dtag_init(&s,60,0,0,0,1,1)); CHECK(!schcvx_dtag_allocate(&s,0,&v) && v==0); return 0;
}
