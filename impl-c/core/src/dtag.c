#include "schcvx/dtag.h"
#define NS UINT64_C(1000000000)
int schcvx_dtag_init(struct schcvx_dtag *s, unsigned bits, uint64_t start, uint64_t startup, uint64_t q, uint32_t rate, uint32_t burst)
{
    uint64_t limit, whole, rem, product;
    if (!s || (bits!=32 && bits!=60) || !rate || !burst) return -SCHCVX_E_DTAG_CONFIG;
    limit=UINT64_C(1)<<bits; if (start>=limit || burst>=limit) return -SCHCVX_E_DTAG_CONFIG;
    whole=q/NS; rem=q%NS; if (whole>(limit-1-burst)/rate) return -SCHCVX_E_DTAG_CONFIG;
    product=whole*rate; rem=rem*rate;
    if (product>=limit-burst || rem/NS>=limit-burst-product) return -SCHCVX_E_DTAG_CONFIG;
    *s=(struct schcvx_dtag){.next=start,.startup_ns=startup,.quarantine_ns=q,.last_ns=startup,.rate=rate,.burst=burst,.tokens=burst,.bits=bits}; return 0;
}
int schcvx_dtag_allocate(struct schcvx_dtag *s, uint64_t now, uint64_t *value)
{
    uint64_t elapsed, seconds, frac, add, mask;
    if (!s || !value || (s->bits!=32 && s->bits!=60)) return -SCHCVX_E_DTAG_CONFIG;
    if (now<s->startup_ns || now<s->last_ns) return -SCHCVX_E_DTAG_TIME;
    if (now-s->startup_ns<s->quarantine_ns) return -SCHCVX_E_DTAG_QUARANTINE;
    elapsed=now-s->last_ns; seconds=elapsed/NS;
    if (seconds>=((uint64_t)s->burst+s->rate-1)/s->rate) { s->tokens=s->burst; s->fraction=0; }
    else { frac=(elapsed%NS)*s->rate+s->fraction; add=seconds*s->rate+frac/NS; if (add>=s->burst-s->tokens) { s->tokens=s->burst; s->fraction=0; } else { s->tokens+=(uint32_t)add; s->fraction=frac%NS; } }
    s->last_ns=now; if (!s->tokens) return -SCHCVX_E_DTAG_RATE;
    --s->tokens; *value=s->next; mask=(UINT64_C(1)<<s->bits)-1; s->next=(s->next+1)&mask; return 0;
}
