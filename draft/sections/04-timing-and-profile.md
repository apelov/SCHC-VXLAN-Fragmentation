# Discard Deadline

A receiver can observe incompleteness only at the cutoff. At that moment it cannot tell whether a missing fragment was lost or will arrive later. A fragment received after expiry is positive evidence of lateness. An implementation SHOULD report the two events separately: an expired entry with missing fragments (incompleteness, loss or lateness not distinguished) and a fragment arriving after expiry or retirement (late).

The deadline is `expiry = first_rx + Δ`, fixed at the first admissible fragment and never extended. The prototype rule, subject to the Open Issue below, is that an entry is expired for any fragment whose arrival timestamp `t` satisfies `t >= expiry`.

## Choosing Δ

For a deterministic path, `Δ` is budgeted from three terms:

* the maximum sender emission span for the fragments of one frame;
* the maximum differential path delay between fragments of the same frame; and
* the maximum receiver scheduling, processing and clock-resolution margin, which includes assembly and RCS verification after the completing fragment arrives.

An absolute maximum one-way latency can inform the differential bound, but it is the spread between pieces of the same frame that drives this timer.

For a best-effort path, the operator measures the spread under realistic contention and chooses the cutoff against the service's latency and discard budgets. The observation stage at which arrival timestamps are taken is defined when benchmarking, and MUST be applied consistently within a receiver.

## Scope of the Deadline

The first-arrival deadline bounds only the time the receiver holds a frame for reassembly. It is not an end-to-end frame age bound. An end-to-end bound requires a separate age mechanism, such as sender timestamps with bounded clock error, which is outside the base format of this document.

# Frame Identification and Restart

The Frame ID is a 64-bit counter. The sender uses one monotonic counter per sending association and VNI, and every frame fragmented in that scope takes the next value. A Frame ID MUST be unique across active and recently retired frames within the configured peer association and VNI. The counter starts at any value chosen by the sender. At 64 bits, wrapping is not expected within the lifetime of a deployment, and behaviour at wrap is not specified. The 8-byte width is a prototype choice kept for alignment; the width itself is an Open Issue.

## Sender Restart

A sender restart must not result in the reuse of a Frame ID that a receiver may still hold as active or retired. This is a requirement on the deployment; the mechanism is an Open Issue. Two approaches are candidates, and neither is specified in this version:

* it persists counter allocation, so that after a restart the counter resumes above every value previously used; or
* it establishes a new association generation that the receiver can distinguish, and the receiver rejects the old generation.

## Receiver Restart

A receiver that loses its retirement records can accept late or duplicate fragments of frames it already delivered or retired. If duplicate suppression must survive a receiver restart, the receiver needs either a fresh association or a drain interval before it reopens the old association. The drain interval covers the residual packet lifetime of the deployment.

The CRC-32C RCS provides a partial safeguard: it guards against splicing slices of different frames, for example after a restart. It does not replace Frame ID uniqueness.

## Status

The prototype puts no epoch or generation value on the wire. Restart-safe interoperability is not claimed.

Open Issue: the restart and generation mechanism is not specified. It covers how a new association generation is signalled and distinguished, how the old generation is rejected, how counter persistence is performed and the length of the receiver drain interval. Until it is specified, this document makes no claim of restart-safe interoperability.

## Retirement Bound L

Frame ID uniqueness depends on the retirement bound `L`. The retirement record of a frame is kept for `L` after retirement. In the first prototype, `L` is configured as an upper bound on the arrival spread of all copies of the fragments of a frame, including duplicates. A receiver MUST NOT evict a retirement record while the deployment still allows fragments for that Frame ID to arrive. Without a defensible bound, suppression through finite retention is best effort.

# Default Operational Profile

This section describes the prototype operating point. It is not a claim of general suitability.

## Fragment Count

The profile starts with a configured maximum of two fragments. This is valid where the minimum configured PMTU carries the maximum allowed frame in two slices, for example an Ethernet-1500 service over a 1500-byte PMTU. The receiver can then keep a two-bit receive mask and bounded per-worker state.

## Minimum-PMTU Validation

A configuration MUST be rejected if the minimum configured PMTU cannot carry the maximum frame length in the maximum number of fragments. Validation uses the slice budget `C` for each configured peer and direction, with the actual `H_IP`.

## Configured Bounds

An implementation provides the following configured values:

* the maximum frame length, and the frame-length range accepted;
* the maximum fragment count (2 in the profile above), and the minimum count of 2;
* the receiver's maximum slice length, a local bound that does not exceed the maximum frame length;
* caps on incomplete entries and retained bytes per peer and globally; and
* capacity for retirement records at the admitted rate, or a reservation of a retirement slot at admission with admission stopped within an exhausted scope until space returns.

## Meaning and Configuration of Δ and L

`Δ` is the receiver-held reassembly time measured from the first admissible fragment, chosen as described above. `L` is the retention time of a retirement record, measured from retirement, bounding the arrival spread of all copies of a frame. Both are local receiver configuration. Neither value is carried on the wire.

Open Issue: this document gives no numeric default for `Δ` or `L`, for the maximum frame length, or for the entry and byte caps. The values depend on the path and on measurement. They are not set here.

Open Issue: the deadline equality rule is that the prototype treats `t >= expiry` as expired, so a fragment arriving exactly at the deadline is late. Whether this is the final rule is to be confirmed.
