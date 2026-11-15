#include "schcvx/wire.h"
#include "schcvx/crc32c.h"

size_t schcvx_header_len(enum schcvx_variant v)
{
    return v == SCHCVX_COMPACT ? 5 : v == SCHCVX_EXTENDED ? 9 : 0;
}
size_t schcvx_all1_header_len(enum schcvx_variant v)
{
    size_t h = schcvx_header_len(v);
    return h ? h + 4 : 0;
}
int schcvx_wire_config_check(const struct schcvx_assoc_wire *a)
{
    if (!a || !schcvx_header_len(a->variant) || !a->fr_rule_ids ||
        !a->fr_rule_count)
        return -SCHCVX_E_CONFIG;
    for (size_t i = 0; i < a->fr_rule_count; ++i) {
        unsigned r = a->fr_rule_ids[i];
        if (a->variant == SCHCVX_COMPACT ?
            (r > 15 || r == (unsigned)(a->cd_rule_id >> 4)) :
            r == a->cd_rule_id)
            return -SCHCVX_E_CONFIG;
    }
    return 0;
}
static bool known_rule(const struct schcvx_assoc_wire *a, uint8_t r)
{
    for (size_t i = 0; i < a->fr_rule_count; ++i)
        if (a->fr_rule_ids[i] == r)
            return true;
    return false;
}
bool schcvx_fcn_is_first(uint8_t f)
{
    return f >= 8 && f <= 14;
}
int schcvx_fcn_countdown(uint8_t f)
{
    return f <= 5 || schcvx_fcn_is_first(f) ? (int)(f & 7) :
        -SCHCVX_E_INVALID_FCN;
}
int schcvx_fcn_value(unsigned k, unsigned position)
{
    if (k < 2 || k > 8 || position >= k)
        return -SCHCVX_E_INVALID_FCN;
    if (position == k - 1)
        return 15;
    return (int)(k - 2 - position) + (position == 0 ? 8 : 0);
}
int schcvx_classify(const struct schcvx_assoc_wire *a, const uint8_t *data,
                    size_t len, struct schcvx_msg *out)
{
    struct schcvx_msg m = {0};
    size_t h;
    if (schcvx_wire_config_check(a))
        return -SCHCVX_E_CONFIG;
    if (!out || !data || !len)
        return -SCHCVX_E_MALFORMED;
    if (data[0] == a->cd_rule_id) {
        if (len < 23)
            return -SCHCVX_E_MALFORMED;
        m.kind = SCHCVX_UNFRAGMENTED;
        m.rule_id = a->cd_rule_id;
        m.payload = data;
        m.payload_len = len;
        *out = m;
        return 0;
    }
    h = schcvx_header_len(a->variant);
    if (len < h)
        return -SCHCVX_E_MALFORMED;
    m.rule_id = a->variant == SCHCVX_COMPACT ? data[0] >> 4 : data[0];
    if (!known_rule(a, m.rule_id))
        return -SCHCVX_E_UNKNOWN_RULE;
    /* Accumulate only DTag bits, so the 72-bit extended header never needs
     * an integer wider than uint64_t. */
    m.dtag = a->variant == SCHCVX_COMPACT ? data[0] & 15 : 0;
    for (size_t i = 1; i < h - 1; ++i)
        m.dtag = (m.dtag << 8) | data[i];
    m.dtag = (m.dtag << 4) | (data[h - 1] >> 4);
    m.fcn = data[h - 1] & 15;
    if (m.fcn == 15) {
        if (len == h) {
            m.kind = SCHCVX_ABORT;
        } else {
            if (len < h + 5)
                return -SCHCVX_E_SHORT_ALL1;
            m.kind = SCHCVX_ALL1;
            for (size_t i = 0; i < 4; ++i)
                m.rcs = (m.rcs << 8) | data[h + i];
            h += 4;
        }
    } else {
        int countdown = schcvx_fcn_countdown(m.fcn);
        if (countdown < 0)
            return countdown;
        if (len == h)
            return -SCHCVX_E_MALFORMED;
        m.kind = SCHCVX_REGULAR;
        m.first = schcvx_fcn_is_first(m.fcn);
        m.countdown = (uint8_t)countdown;
        if (m.first && (len - h < 9 || data[h] != a->cd_rule_id))
            return -SCHCVX_E_FIRST;
    }
    m.payload = data + h;
    m.payload_len = len - h;
    *out = m;
    return 0;
}
size_t schcvx_encode_header(const struct schcvx_assoc_wire *a,
                            const struct schcvx_msg *m, uint8_t *out)
{
    size_t h;
    uint64_t tag;
    if (schcvx_wire_config_check(a) || !m || !out)
        return 0;
    if (!known_rule(a, m->rule_id) || m->fcn > 15 ||
        (a->variant == SCHCVX_COMPACT &&
         (m->rule_id > 15 || m->dtag > UINT32_MAX)) ||
        (a->variant == SCHCVX_EXTENDED && m->dtag > UINT64_C(0x0fffffffffffffff)))
        return 0;
    if (m->kind == SCHCVX_REGULAR) {
        if (schcvx_fcn_countdown(m->fcn) < 0)
            return 0;
    } else if ((m->kind != SCHCVX_ALL1 && m->kind != SCHCVX_ABORT) || m->fcn != 15) {
        return 0;
    }
    h = schcvx_header_len(a->variant);
    tag = m->dtag;
    out[h - 1] = (uint8_t)((tag & 15) << 4) | m->fcn;
    tag >>= 4;
    for (size_t i = h - 1; i > 1;) {
        out[--i] = (uint8_t)tag;
        tag >>= 8;
    }
    out[0] = a->variant == SCHCVX_COMPACT ?
        (uint8_t)((m->rule_id << 4) | tag) : m->rule_id;
    if (m->kind == SCHCVX_ALL1) {
        schcvx_put_be32(out + h, m->rcs);
        h += 4;
    }
    return h;
}
