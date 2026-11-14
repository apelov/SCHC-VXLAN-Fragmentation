import importlib
import sys
from pathlib import Path

import pytest

_TOOLS_DIR = Path(__file__).resolve().parent.parent / "tools"
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

gen_examples = importlib.import_module("gen_examples")


@pytest.fixture(scope="module")
def example_a():
    return gen_examples.build_example_a()


@pytest.fixture(scope="module")
def example_b():
    return gen_examples.build_example_b()


# --- Regeneration is byte-for-byte deterministic ----------------------------


def test_regeneration_matches_committed_file(example_a, example_b):
    rendered = gen_examples.render(example_a, example_b)
    committed = gen_examples.OUTPUT_PATH.read_text()
    assert rendered == committed


def test_output_file_has_no_trailing_whitespace_issues():
    committed = gen_examples.OUTPUT_PATH.read_text()
    assert committed.endswith("\n")
    assert "\t" not in committed


# --- Example A: last-fragment-first completion -------------------------------


def test_example_a_pinned_slice_and_outer_ip_lengths(example_a):
    frag0, frag1 = example_a["frag0"], example_a["frag1"]
    assert len(frag0.payload) == 1454
    assert len(frag1.payload) == 60
    assert gen_examples._outer_ip_len(example_a["datagram0"]) == 1496
    assert gen_examples._outer_ip_len(example_a["datagram1"]) == 106


def test_example_a_final_fragment_arrives_first(example_a):
    first_event, second_event = example_a["events"]
    assert first_event["result"] == "PENDING"
    assert first_event["bitmap"] == "01"
    assert second_event["result"] == "DELIVERED"
    assert second_event["bitmap"] == "11"


def test_example_a_counters_match_narrative(example_a):
    assert example_a["counters"]["completed"] == 1
    assert example_a["counters"]["expired"] == 0
    assert example_a["counters"]["late"] == 0


def test_example_a_rcs_matches_crc32c_of_frame(example_a):
    from schcvx.crc32c import crc32c

    assert example_a["rcs"] == crc32c(gen_examples.FRAME)


# --- Example B: deadline expiry ----------------------------------------------


def test_example_b_only_final_fragment_then_late_nonfinal(example_b):
    first_event, second_event = example_b["events"]
    assert first_event["result"] == "PENDING"
    assert first_event["bitmap"] == "01"
    assert second_event["result"] == "LATE"


def test_example_b_counters_match_narrative(example_b):
    assert example_b["counters"]["completed"] == 0
    assert example_b["counters"]["expired"] == 1
    assert example_b["counters"]["late"] == 1


def test_example_b_expiry_equals_first_rx_plus_delta(example_b):
    assert example_b["expiry_ms"] == gen_examples.DELTA
