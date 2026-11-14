import pathlib
import sys

_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_ROOT / "tools"))

from check_section import (
    check_balanced_fences,
    check_file,
    check_first_line_heading,
    check_must,
    check_nonempty,
    check_throughput_claims,
    check_unresolved_markers,
    main,
)


def test_nonempty_pass():
    assert check_nonempty("# Heading\n") == []


def test_nonempty_fail_on_empty_string():
    assert check_nonempty("") == ["file is empty"]


def test_must_pass_when_all_present():
    text = "# Heading\n\nThis mentions RuleID and VNI.\n"
    assert check_must(text, ["RuleID", "VNI"]) == []


def test_must_fail_when_missing():
    text = "# Heading\n\nThis mentions RuleID only.\n"
    failures = check_must(text, ["RuleID", "VNI"])
    assert failures == ["missing required text: 'VNI'"]


def test_must_is_case_sensitive():
    text = "# Heading\n\nthis mentions ruleid in lowercase.\n"
    failures = check_must(text, ["RuleID"])
    assert failures == ["missing required text: 'RuleID'"]


def test_unresolved_markers_pass_when_absent():
    text = "# Heading\n\nAll items are resolved.\n"
    assert check_unresolved_markers(text) == []


def test_unresolved_markers_fail_each_kind():
    text = "# Heading\n\nTBD here, TODO there, FIXME this, XXX that.\n"
    assert check_unresolved_markers(text) == [
        "unresolved marker: FIXME",
        "unresolved marker: TBD",
        "unresolved marker: TODO",
        "unresolved marker: XXX",
    ]


def test_unresolved_markers_whole_word_only():
    text = "# Heading\n\nThe ATBDX and TODOIST are not markers.\n"
    assert check_unresolved_markers(text) == []


def test_unresolved_markers_lowercase_not_matched():
    text = "# Heading\n\na tbd item and a todo note.\n"
    assert check_unresolved_markers(text) == []


def test_throughput_claims_pass_without_claim_words():
    text = "# Heading\n\nThe receiver bounds state per peer.\n"
    assert check_throughput_claims(text) == []


def test_throughput_claims_pass_with_pending_measurement():
    text = "# Heading\n\nObserved throughput of 10 Gbps is pending measurement.\n"
    assert check_throughput_claims(text) == []


def test_throughput_claims_fail_without_pending_measurement():
    text = "# Heading\n\nThis design achieves 10 Gbps on commodity hardware.\n"
    failures = check_throughput_claims(text)
    assert len(failures) == 1
    assert "Gbps" in failures[0]


def test_throughput_claims_fail_for_line_rate_achieved():
    text = "# Heading\n\nTesting showed line rate achieved in all conditions.\n"
    failures = check_throughput_claims(text)
    assert len(failures) == 1
    assert "line rate achieved" in failures[0]


def test_throughput_claims_pending_measurement_must_share_sentence():
    text = (
        "# Heading\n\n"
        "This design achieves 10 Gbps on commodity hardware. "
        "Results are pending measurement.\n"
    )
    failures = check_throughput_claims(text)
    assert len(failures) == 1
    assert "Gbps" in failures[0]


def test_balanced_fences_pass():
    text = "# Heading\n\n```\ncode\n```\n"
    assert check_balanced_fences(text) == []


def test_balanced_fences_pass_with_no_fences():
    text = "# Heading\n\nNo fences here.\n"
    assert check_balanced_fences(text) == []


def test_balanced_fences_fail_when_odd():
    text = "# Heading\n\n```\ncode\n"
    assert check_balanced_fences(text) == ["unbalanced code fences: odd number of ``` lines"]


def test_first_line_heading_pass():
    text = "# Heading\n\nBody text.\n"
    assert check_first_line_heading(text) == []


def test_first_line_heading_pass_skips_leading_blank_lines():
    text = "\n\n# Heading\n\nBody text.\n"
    assert check_first_line_heading(text) == []


def test_first_line_heading_fail_when_not_heading():
    text = "Body text without a heading.\n"
    assert check_first_line_heading(text) == [
        "first non-blank line does not start with '#'"
    ]


def test_first_line_heading_fail_when_no_non_blank_line():
    text = "\n\n   \n"
    assert check_first_line_heading(text) == ["file has no non-blank line"]


def test_check_file_fail_when_missing(tmp_path):
    missing = tmp_path / "nope.md"
    failures = check_file(missing, [])
    assert failures == [f"file does not exist: {missing}"]


def test_check_file_pass(tmp_path):
    section = tmp_path / "ok.md"
    section.write_text("# Heading\n\nMentions RuleID and VNI here.\n")
    assert check_file(section, ["RuleID", "VNI"]) == []


def test_check_file_collects_multiple_failures(tmp_path):
    section = tmp_path / "bad.md"
    section.write_text("Body text with a TBD marker and 10 Gbps claim.\n")
    failures = check_file(section, ["RuleID"])
    assert "missing required text: 'RuleID'" in failures
    assert "unresolved marker: TBD" in failures
    assert any("Gbps" in f for f in failures)
    assert "first non-blank line does not start with '#'" in failures


def test_main_exit_zero_on_pass(tmp_path, capsys):
    section = tmp_path / "ok.md"
    section.write_text("# Heading\n\nMentions RuleID here.\n")
    rc = main([str(section), "--must", "RuleID"])
    assert rc == 0
    assert capsys.readouterr().out == ""


def test_main_exit_nonzero_and_prints_one_line_per_failure(tmp_path, capsys):
    section = tmp_path / "bad.md"
    section.write_text("Body text with a TODO marker.\n")
    rc = main([str(section), "--must", "RuleID"])
    assert rc == 1
    out_lines = capsys.readouterr().out.splitlines()
    assert "missing required text: 'RuleID'" in out_lines
    assert "unresolved marker: TODO" in out_lines
    assert "first non-blank line does not start with '#'" in out_lines
