# SPDX-License-Identifier: GPL-3.0-or-later
"""Clip event reports use synthetic records and CSV, never game files or text."""

from pathlib import Path

import pytest

from coney_tools import clips
from coney_tools.cli import main


def test_clip_events_select_span_and_redact_text(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """The first frame is inclusive, the next frame after the clip is excluded, and text stays private."""
    clip = tmp_path / "example.mp4"
    clip.with_suffix(".record.csv").write_text("start_step,frames\n60,30\n", encoding="utf-8")
    clip.with_suffix(".events.csv").write_text(
        "step,kind,name,detail\n59,call,Before,private\n60,call,Begin,private\n"
        '75,hint,"synthetic secret, with punctuation",private\n'
        "89,sound,0x12345678,private\n90,call,After,private\n",
        encoding="utf-8",
    )
    assert main(["clips", "events", str(clip)]) == 0
    assert capsys.readouterr().out == "0.000s call Begin\n0.500s hint <redacted>\n0.967s sound 0x12345678\n"


def test_clip_events_explicit_log(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """An explicit event log overrides the companion filename without needing a video decoder."""
    clip = tmp_path / "example.mp4"
    clip.with_suffix(".record.csv").write_text("start_step,frames\n4,1\n", encoding="utf-8")
    log = tmp_path / "run.csv"
    log.write_text("step,kind,name,detail\n4,callback,Start,secret\n", encoding="utf-8")
    assert main(["clips", "events", str(clip), "--log", str(log)]) == 0
    assert capsys.readouterr().out == "0.000s callback Start\n"


@pytest.mark.parametrize("record", ["start_step,frames\n-1,30\n", "start_step,frames\n0,0\n", "bad\n"])
def test_clip_events_reject_invalid_record(tmp_path: Path, record: str) -> None:
    """Missing or invalid bounds must not silently associate events with the wrong clip."""
    clip = tmp_path / "example.mp4"
    clip.with_suffix(".record.csv").write_text(record, encoding="utf-8")
    with pytest.raises(clips.ClipError):
        clips.run_events(clip, None)


def test_clip_events_redact_unknown_names(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """Unknown emitters and non-identifier names cannot introduce text into a report."""
    clip = tmp_path / "example.mp4"
    clip.with_suffix(".record.csv").write_text("start_step,frames\n0,1\n", encoding="utf-8")
    clip.with_suffix(".events.csv").write_text(
        "step,kind,name,detail\n0,objective,secret,private\n0,call,secret words,private\n", encoding="utf-8"
    )
    assert clips.run_events(clip, None) == 0
    assert capsys.readouterr().out == "0.000s objective <redacted>\n0.000s call <redacted>\n"


def test_clip_events_quoted_details_and_equal_steps(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """Quoted multiline details are parsed without being reported, and simultaneous events keep their order."""
    clip = tmp_path / "example.mp4"
    clip.with_suffix(".record.csv").write_text("start_step,frames\n10,2\n", encoding="utf-8")
    clip.with_suffix(".events.csv").write_text(
        'step,kind,name,detail\n10,call,First,"synthetic secret,\nsecond line"\n'
        "10,callback,Second,private\n11,sound,0x00000001,private\n",
        encoding="utf-8",
    )
    assert clips.run_events(clip, None) == 0
    assert capsys.readouterr().out == "0.000s call First\n0.000s callback Second\n0.033s sound 0x00000001\n"


def test_clip_events_bad_log_prints_no_partial_report(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """Invalid step data fails without printing any raw CSV or an incomplete report."""
    clip = tmp_path / "example.mp4"
    clip.with_suffix(".record.csv").write_text("start_step,frames\n0,30\n", encoding="utf-8")
    clip.with_suffix(".events.csv").write_text(
        "step,kind,name,detail\n0,call,Valid,private\nsecret,call,Invalid,private\n", encoding="utf-8"
    )
    with pytest.raises(clips.ClipError, match="invalid or missing event log"):
        clips.run_events(clip, None)
    assert capsys.readouterr().out == ""
