-- schcvx.lua : Wireshark dissector for SCHC VXLAN fragmentation
--
-- Written from the revised Internet-Draft
-- draft-vladyka-schc-vxlan-fragmentation-00 (draft/revised/), not from either
-- implementation.  Lua 5.4 (Wireshark 4.x).
--
-- UDP payload on the configured port is either
--   * an unfragmented SCHC Packet: C/D RuleID (8 bits), VXLAN header (8 bytes),
--     Ethernet frame without FCS (at least 23 bytes), or
--   * a fragment: RuleID | DTag | FCN [| RCS (All-1 only)] | tile.
--     compact : RuleID 4, DTag 32, FCN 4 bits (5-byte header)
--     extended: RuleID 8, DTag 60, FCN 4 bits (9-byte header)
-- Classification: first byte == configured C/D RuleID -> unfragmented.
--
-- Load with:  tshark -X lua_script:schcvx.lua ...   or copy into the personal
-- Lua plugins folder.

local VERSION = "1.0"

------------------------------------------------------------------------------
-- CRC-32C (Castagnoli), reflected, init/final XOR 0xffffffff
------------------------------------------------------------------------------
local CRC_TABLE = {}
for i = 0, 255 do
  local c = i
  for _ = 1, 8 do
    if (c & 1) ~= 0 then c = (c >> 1) ~ 0x82F63B78 else c = c >> 1 end
  end
  CRC_TABLE[i] = c
end

local function crc32c(s)
  local crc = 0xFFFFFFFF
  for i = 1, #s do
    crc = CRC_TABLE[(crc ~ s:byte(i)) & 0xFF] ~ (crc >> 8)
  end
  return (~crc) & 0xFFFFFFFF
end
-- self-check at load time
assert(crc32c("123456789") == 0xE3069283, "schcvx: CRC-32C self-test failed")

------------------------------------------------------------------------------
-- Protocol, fields, expert infos, preferences
------------------------------------------------------------------------------
local proto = Proto("schcvx", "SCHC VXLAN Fragmentation")

local VARIANT_COMPACT, VARIANT_EXTENDED = 1, 2
local DEFAULT_PORT = 60789

proto.prefs.port = Pref.uint("UDP port", DEFAULT_PORT,
  "UDP destination port of the SCHC VXLAN binding")
proto.prefs.variant = Pref.enum("Wire variant", VARIANT_COMPACT,
  "Compact: RuleID 4, DTag 32, FCN 4 bits. Extended: RuleID 8, DTag 60, FCN 4 bits",
  { { 1, "Compact", VARIANT_COMPACT }, { 2, "Extended", VARIANT_EXTENDED } }, true)
proto.prefs.cd_rule = Pref.uint("C/D RuleID (NoCompress)", 0,
  "8-bit NoCompress C/D RuleID; a datagram starting with this byte is an unfragmented SCHC Packet")
proto.prefs.frag_rule = Pref.uint("Fragmentation RuleID (0xFFFF = any)", 1,
  "Configured fragmentation RuleID; other values are flagged and not reassembled. 65535 accepts any")
proto.prefs.reassemble = Pref.bool("Reassemble fragments", true,
  "Collect fragments and dissect the reassembled SCHC Packet (verifies the RCS)")

local pf = {}
pf.kind      = ProtoField.string("schcvx.kind", "Kind")
pf.rule_id   = ProtoField.uint8("schcvx.rule_id", "Fragmentation RuleID", base.DEC)
pf.dtag      = ProtoField.uint64("schcvx.dtag", "DTag", base.HEX)
pf.fcn       = ProtoField.uint8("schcvx.fcn", "FCN", base.DEC)
pf.fcn_bits  = ProtoField.string("schcvx.fcn.bits", "FCN (binary)")
pf.fcn_first = ProtoField.bool("schcvx.fcn.first", "First fragment")
pf.fcn_cd    = ProtoField.uint8("schcvx.fcn.countdown", "Countdown", base.DEC)
pf.k         = ProtoField.uint8("schcvx.k", "Number of fragments (K)", base.DEC)
pf.rcs       = ProtoField.uint32("schcvx.rcs", "RCS (CRC-32C)", base.HEX)
pf.rcs_stat  = ProtoField.string("schcvx.rcs.status", "RCS status")
pf.rcs_calc  = ProtoField.uint32("schcvx.rcs.calculated", "RCS calculated", base.HEX)
pf.cd_rule   = ProtoField.uint8("schcvx.cd_rule_id", "C/D RuleID", base.DEC)
pf.plen      = ProtoField.uint32("schcvx.payload_len", "Payload length", base.DEC)
pf.payload   = ProtoField.bytes("schcvx.payload", "Fragment payload")
pf.reass_in  = ProtoField.framenum("schcvx.reassembled_in", "Reassembled in frame",
                                   base.NONE, frametype.NONE)
pf.reass_len = ProtoField.uint32("schcvx.reassembled_len", "Reassembled SCHC Packet length", base.DEC)
pf.vni       = ProtoField.uint32("schcvx.vni", "VNI", base.DEC)

local fields = {}
for _, f in pairs(pf) do fields[#fields + 1] = f end
proto.fields = fields

local ei = {}
ei.invalid_fcn  = ProtoExpert.new("schcvx.expert.invalid_fcn", "Invalid FCN (0110 and 0111 are reserved)",
                                  expert.group.MALFORMED, expert.severity.ERROR)
ei.short_all1   = ProtoExpert.new("schcvx.expert.short_all1",
                                  "All-1 shorter than header + 4-byte RCS + 1 payload byte",
                                  expert.group.MALFORMED, expert.severity.ERROR)
ei.short_frag   = ProtoExpert.new("schcvx.expert.short_fragment",
                                  "Fragment shorter than its header or with an empty tile",
                                  expert.group.MALFORMED, expert.severity.ERROR)
ei.abort        = ProtoExpert.new("schcvx.expert.sender_abort",
                                  "Sender-Abort (header-only All-1)",
                                  expert.group.RESPONSE_CODE, expert.severity.WARN)
ei.first_bad    = ProtoExpert.new("schcvx.expert.first_bad",
                                  "First fragment lacks the C/D RuleID or has fewer than 9 payload bytes",
                                  expert.group.MALFORMED, expert.severity.ERROR)
ei.bad_rcs      = ProtoExpert.new("schcvx.expert.bad_rcs", "Reassembly check sequence mismatch",
                                  expert.group.CHECKSUM, expert.severity.ERROR)
ei.short_unfrag = ProtoExpert.new("schcvx.expert.short_unfragmented",
                                  "Unfragmented SCHC Packet shorter than 23 bytes",
                                  expert.group.MALFORMED, expert.severity.ERROR)
ei.unknown_rule = ProtoExpert.new("schcvx.expert.unknown_rule",
                                  "Fragmentation RuleID differs from the configured one",
                                  expert.group.PROTOCOL, expert.severity.WARN)
ei.conflict     = ProtoExpert.new("schcvx.expert.conflict",
                                  "Fragment conflicts with stored state",
                                  expert.group.MALFORMED, expert.severity.WARN)
ei.incomplete   = ProtoExpert.new("schcvx.expert.no_reassembly",
                                  "Fragment not (yet) part of a reassembled packet",
                                  expert.group.REASSEMBLE, expert.severity.NOTE)
ei.lengths      = ProtoExpert.new("schcvx.expert.length",
                                  "Fragment length problem", expert.group.MALFORMED, expert.severity.ERROR)
local expert_fields = {}
for _, e in pairs(ei) do expert_fields[#expert_fields + 1] = e end
proto.experts = expert_fields

------------------------------------------------------------------------------
-- Reassembly state (rebuilt in init, filled on the first pass only)
------------------------------------------------------------------------------
local states      -- key -> state under construction
local reassembled -- completing frame number -> {data=, rcs=, calc=, good=, frames=}
local done_in     -- fragment frame number -> completing frame number
local notes       -- frame number -> list of text (conflicts etc.)
local frag_cache  -- frame number -> parsed fragment (first pass)

local function reset_state()
  states, reassembled, done_in, notes, frag_cache = {}, {}, {}, {}, {}
end
reset_state()
proto.init = reset_state

local function add_note(frame, text)
  local t = notes[frame]
  if not t then t = {}; notes[frame] = t end
  t[#t + 1] = text
end

local function to_hex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

------------------------------------------------------------------------------
-- Helpers
------------------------------------------------------------------------------
local function fcn_bits(f)
  return string.format("%d%d%d%d", (f >> 3) & 1, (f >> 2) & 1, (f >> 1) & 1, f & 1)
end

local vxlan_dissector = nil
local function get_vxlan()
  if not vxlan_dissector then vxlan_dissector = Dissector.get("vxlan") end
  return vxlan_dissector
end

local function header_len()
  return (proto.prefs.variant == VARIANT_EXTENDED) and 9 or 5
end

-- Parse the fragment header from tvb (len >= header_len()).
local function parse_header(tvb)
  local b = {}
  local h = header_len()
  for i = 0, h - 1 do b[i] = tvb(i, 1):uint() end
  local rule, dtag, fcn
  if h == 5 then
    rule = b[0] >> 4
    dtag = ((b[0] & 0xF) << 28) | (b[1] << 20) | (b[2] << 12) | (b[3] << 4) | (b[4] >> 4)
    fcn = b[4] & 0xF
  else
    rule = b[0]
    dtag = (b[1] << 52) | (b[2] << 44) | (b[3] << 36) | (b[4] << 28)
         | (b[5] << 20) | (b[6] << 12) | (b[7] << 4) | (b[8] >> 4)
    fcn = b[8] & 0xF
  end
  return rule, dtag, fcn
end

local function uint64_of(v)
  return UInt64.new(v & 0xFFFFFFFF, (v >> 32) & 0xFFFFFFFF)
end

-- Dissect an SCHC Packet held in `tvb` (C/D RuleID + VXLAN + Ethernet).
-- Returns the VNI (or nil) and the info string of the inner dissection.
local function dissect_schc_packet(tvb, pinfo, tree)
  local len = tvb:len()
  local vni
  tree:add(pf.cd_rule, tvb(0, 1))
  if len < 23 then
    tree:add_proto_expert_info(ei.short_unfrag)
    if len >= 9 then vni = tvb(5, 3):uint() end
    return vni, nil
  end
  vni = tvb(5, 3):uint()
  local d = get_vxlan()
  local inner
  if d then
    local ok = pcall(function() d:call(tvb(1):tvb(), pinfo, tree) end)
    if ok then inner = tostring(pinfo.cols.info) end
  end
  return vni, inner
end

------------------------------------------------------------------------------
-- Reassembly (first pass)
------------------------------------------------------------------------------
-- f: {frame, rule, dtag, fcn, kind="first"|"regular"|"all1", countdown, payload, rcs}
local function reassemble_add(key, f)
  local st = states[key]
  if not st then
    st = { first = nil, n = nil, tiles = {}, all1 = nil, frames = {} }
    states[key] = st
  end
  st.frames[#st.frames + 1] = f.frame

  local function conflict(msg)
    add_note(f.frame, msg)
    return false
  end

  if f.kind == "first" then
    if st.first then
      if st.first.payload == f.payload and st.n == f.countdown then return end -- identical duplicate
      return conflict("conflicting duplicate first fragment")
    end
    if st.tiles[f.countdown] then
      return conflict("first marker collides with stored regular position " .. f.countdown)
    end
    for pos in pairs(st.tiles) do
      if pos >= f.countdown then
        add_note(f.frame, "stored regular position " .. pos .. " outside expected set; entry discarded")
        states[key] = nil
        return
      end
    end
    st.first, st.n = f, f.countdown
  elseif f.kind == "regular" then
    local old = st.tiles[f.countdown]
    if old then
      if old.payload == f.payload then return end
      return conflict("conflicting duplicate regular fragment (countdown " .. f.countdown .. ")")
    end
    if st.n and f.countdown >= st.n then
      add_note(f.frame, "countdown " .. f.countdown .. " outside expected set; entry discarded")
      states[key] = nil
      return
    end
    st.tiles[f.countdown] = f
  else -- all1
    if st.all1 then
      if st.all1.payload == f.payload and st.all1.rcs == f.rcs then return end
      return conflict("conflicting duplicate All-1")
    end
    st.all1 = f
  end

  -- completion test
  if not (st.first and st.all1) then return end
  local parts = { st.first.payload }
  for pos = st.n - 1, 0, -1 do
    local t = st.tiles[pos]
    if not t then return end
    parts[#parts + 1] = t.payload
  end
  parts[#parts + 1] = st.all1.payload
  local data = table.concat(parts)
  local calc = crc32c(data)
  local rec = { data = data, rcs = st.all1.rcs, calc = calc,
                good = (calc == st.all1.rcs), frames = st.frames }
  reassembled[f.frame] = rec
  for _, fr in ipairs(st.frames) do done_in[fr] = f.frame end
  states[key] = nil
end

------------------------------------------------------------------------------
-- Main dissector
------------------------------------------------------------------------------
function proto.dissector(tvb, pinfo, tree)
  local len = tvb:len()
  if len == 0 then return 0 end
  local cd = proto.prefs.cd_rule & 0xFF
  local sub = tree:add(proto, tvb(), "SCHC VXLAN")
  pinfo.cols.protocol:set("SCHCVX")

  -- Unfragmented SCHC Packet
  if tvb(0, 1):uint() == cd then
    sub:add(pf.kind, tvb(0, 1), "unfragmented"):set_generated()
    sub:add(pf.plen, tvb(), len):set_generated()
    local vni, inner = dissect_schc_packet(tvb, pinfo, sub)
    local info = "SCHC unfragmented" .. (vni and (" VNI=" .. vni) or "")
    if inner then info = info .. " | " .. inner end
    pinfo.cols.protocol:set("SCHCVX")
    pinfo.cols.info:set(info)
    return len
  end

  -- Fragment
  local h = header_len()
  if len < h then
    sub:add(pf.kind, tvb(), "fragment (truncated)"):set_generated()
    sub:add_proto_expert_info(ei.short_frag, "Datagram (" .. len .. " bytes) shorter than the " .. h .. "-byte fragment header")
    pinfo.cols.info:set("SCHC frag (truncated, " .. len .. " bytes)")
    return len
  end

  local rule, dtag, fcn = parse_header(tvb)
  local is_all1 = (fcn == 0xF)
  local invalid = (fcn == 6 or fcn == 7)
  local first = (fcn & 8) ~= 0 and not is_all1
  local countdown = fcn & 7
  local kind, hdr_total
  local plen = 0
  local rcs
  if is_all1 then
    hdr_total = h + 4
    if len == h then
      kind = "sender-abort"
    elseif len < h + 5 then
      kind = "all-1"
    else
      kind = "all-1"
      rcs = tvb(h, 4):uint()
    end
    plen = (len > h + 4) and (len - h - 4) or 0
  else
    kind = "regular"
    hdr_total = h
    plen = len - h
  end

  local ruleid_range = tvb(0, 1)
  local dtag_range = tvb(0, h)
  local rule_item = sub:add(pf.rule_id, ruleid_range, rule)
  local dtag_item = sub:add(pf.dtag, dtag_range, uint64_of(dtag))
  local fcn_range = tvb(h - 1, 1)
  local fcn_item = sub:add(pf.fcn, fcn_range, fcn)
  fcn_item:add(pf.fcn_bits, fcn_range, fcn_bits(fcn)):set_generated()
  local k
  if not is_all1 and not invalid then
    fcn_item:add(pf.fcn_first, fcn_range, first)
    fcn_item:add(pf.fcn_cd, fcn_range, countdown)
    if first then
      k = countdown + 2
      fcn_item:add(pf.k, fcn_range, k):set_generated()
    end
  end
  sub:add(pf.kind, fcn_range, kind):set_generated()

  local fr = proto.prefs.frag_rule
  local rule_ok = (fr == 0xFFFF) or (rule == fr)
  if not rule_ok then
    rule_item:add_proto_expert_info(ei.unknown_rule,
      "Fragmentation RuleID " .. rule .. " differs from configured " .. fr)
  end

  local payload_off = hdr_total
  local malformed = false
  if invalid then
    fcn_item:add_proto_expert_info(ei.invalid_fcn)
    malformed = true
  end

  if is_all1 then
    if len == h then
      sub:add_proto_expert_info(ei.abort)
      malformed = true  -- no data to reassemble
    elseif len < h + 5 then
      sub:add_proto_expert_info(ei.short_all1,
        "All-1 of " .. len .. " bytes: must be exactly " .. h .. " (Sender-Abort) or at least " .. (h + 5))
      malformed = true
    else
      sub:add(pf.rcs, tvb(h, 4))
    end
  elseif plen < 1 then
    sub:add_proto_expert_info(ei.short_frag, "Regular fragment without payload (empty tile)")
    malformed = true
  end

  if plen > 0 then
    sub:add(pf.plen, tvb(payload_off, plen), plen):set_generated()
    sub:add(pf.payload, tvb(payload_off, plen))
  end

  -- first fragment payload checks
  local first_ok = true
  if first and not invalid and plen >= 1 then
    if tvb(payload_off, 1):uint() ~= cd or plen < 9 then
      sub:add_proto_expert_info(ei.first_bad)
      first_ok = false
      malformed = true
    else
      local pk = sub:add(pf.cd_rule, tvb(payload_off, 1))
      local vni = tvb(payload_off + 5, 3):uint()
      pk:add(pf.vni, tvb(payload_off + 5, 3), vni):set_generated()
    end
  elseif first and not invalid then
    malformed = true
  end

  -- Info column pieces
  local dtag_s = string.format("0x%0" .. (h == 5 and 8 or 15) .. "x", dtag)
  local info
  if kind == "sender-abort" then
    info = string.format("SCHC frag DTag=%s FCN=1111 (Sender-Abort)", dtag_s)
  elseif is_all1 then
    info = string.format("SCHC frag DTag=%s FCN=1111 (All-1%s)", dtag_s,
      rcs and string.format(", RCS=0x%08x", rcs) or ", short")
  elseif invalid then
    info = string.format("SCHC frag DTag=%s FCN=%s (invalid)", dtag_s, fcn_bits(fcn))
  elseif first then
    info = string.format("SCHC frag DTag=%s FCN=%s (first, K=%d)", dtag_s, fcn_bits(fcn), k)
  else
    info = string.format("SCHC frag DTag=%s FCN=%s (countdown %d)", dtag_s, fcn_bits(fcn), countdown)
  end

  -- Reassembly bookkeeping (first pass only)
  local do_reass = proto.prefs.reassemble and not malformed and rule_ok
  if do_reass and not pinfo.visited then
    local f = { frame = pinfo.number, rule = rule, dtag = dtag, fcn = fcn }
    if is_all1 then
      f.kind = "all1"; f.rcs = rcs
    elseif first then
      f.kind = "first"; f.countdown = countdown
    else
      f.kind = "regular"; f.countdown = countdown
    end
    f.payload = tvb:raw(payload_off, plen)
    local key = string.format("%s>%s/%d/%x", tostring(pinfo.src), tostring(pinfo.dst), rule, dtag)
    reassemble_add(key, f)
  end

  -- Notes recorded on the first pass
  for _, text in ipairs(notes[pinfo.number] or {}) do
    sub:add_proto_expert_info(ei.conflict, text)
  end

  -- Reassembly results
  local rec = reassembled[pinfo.number]
  local done = done_in[pinfo.number]
  if rec then
        local status = rec.good and "good" or "bad"
    local bytearray = ByteArray.new(to_hex(rec.data))
    local rtvb = bytearray:tvb("Reassembled SCHC Packet")
    local pt = sub:add(rtvb(), "Reassembled SCHC Packet (" .. #rec.data .. " bytes)")
    pt:add(pf.reass_len, rtvb(0, 0), #rec.data):set_generated()
    pt:add(pf.rcs, rec.rcs):set_generated()
    pt:add(pf.rcs_calc, rec.calc):set_generated()
    local si = pt:add(pf.rcs_stat, status)
    si:set_generated()
    if not rec.good then
      si:add_proto_expert_info(ei.bad_rcs,
        string.format("RCS 0x%08x does not match computed CRC-32C 0x%08x", rec.rcs, rec.calc))
    end
    local vni, inner = dissect_schc_packet(rtvb, pinfo, pt)
    info = info .. string.format(" reassembled (%d bytes, RCS %s)", #rec.data, status)
    if vni then info = info .. " VNI=" .. vni end
    if inner then info = info .. " | " .. inner end
  elseif done and done ~= pinfo.number then
    sub:add(pf.reass_in, done):set_generated()
    info = info .. ", reassembled in frame " .. done
  elseif do_reass and not (kind == "sender-abort") then
    sub:add_proto_expert_info(ei.incomplete)
  end

  pinfo.cols.protocol:set("SCHCVX")
  pinfo.cols.info:set(info)
  return len
end

------------------------------------------------------------------------------
-- Port registration
------------------------------------------------------------------------------
local udp_table = DissectorTable.get("udp.port")
local registered_port = nil

local function register_port()
  local port = proto.prefs.port
  if registered_port == port then return end
  if registered_port then udp_table:remove(registered_port, proto) end
  if port and port > 0 then udp_table:add(port, proto) end
  registered_port = port
end

function proto.prefs_changed()
  register_port()
end

register_port()
