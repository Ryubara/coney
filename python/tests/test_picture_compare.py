# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools picture compare`, on pictures drawn here (no game data).

The rules come from docs/engine/renderer.md#comparing: a pixel is over the tolerance when any channel differs by more
than it, a picture passes when the share of such pixels is at most the limit, and the counts reported are the pixels
over and the largest difference.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from PIL import Image

from coney_tools import picture_compare
from coney_tools.cli import main
from coney_tools.config import ConfigError

WIDTH = 100
HEIGHT = 10  # 1000 pixels, so 0.1 % is exactly one pixel


def make_picture(path: Path, colour: tuple[int, int, int, int] = (40, 80, 120, 255), size=(WIDTH, HEIGHT)) -> Path:
    """Write a flat RGBA picture and return its path."""
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", size, colour).save(path)
    return path


def make_with_pixels(path: Path, changes: dict[tuple[int, int], tuple[int, int, int, int]]) -> Path:
    """Write the flat picture of `make_picture` with the given pixels set to other colours."""
    make_picture(path)
    picture = Image.open(path).convert("RGBA")
    for position, colour in changes.items():
        picture.putpixel(position, colour)
    picture.save(path)
    return path


def test_identical_pictures_have_no_pixel_over_and_pass(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    # Expected: engine/renderer.md#comparing, the counts are the pixels over the tolerance and the largest difference
    a = make_picture(tmp_path / "a.png")
    b = make_picture(tmp_path / "b.png")
    assert main(["picture", "compare", str(a), str(b)]) == 0
    out = capsys.readouterr().out
    assert "0 of 1000 pixels over 2" in out
    assert "largest difference 0" in out


def test_a_difference_of_exactly_the_tolerance_is_not_over(tmp_path: Path) -> None:
    a = make_picture(tmp_path / "a.png", (40, 80, 120, 255))
    b = make_picture(tmp_path / "b.png", (42, 78, 120, 255))
    result = picture_compare.compare_pictures(a, b, channel_tolerance=2)
    assert result.over == 0
    assert result.largest == 2


def test_one_more_than_the_tolerance_in_any_one_channel_is_over(tmp_path: Path) -> None:
    a = make_picture(tmp_path / "a.png", (40, 80, 120, 255))
    # Only blue is off, by 3: the whole pixel counts once.
    b = make_picture(tmp_path / "b.png", (40, 80, 123, 255))
    result = picture_compare.compare_pictures(a, b, channel_tolerance=2)
    assert result.over == 1000
    assert result.largest == 3


def test_the_alpha_channel_is_compared_too(tmp_path: Path) -> None:
    a = make_picture(tmp_path / "a.png", (40, 80, 120, 255))
    b = make_picture(tmp_path / "b.png", (40, 80, 120, 200))
    result = picture_compare.compare_pictures(a, b, channel_tolerance=2)
    assert result.largest == 55
    assert result.over == 1000


def test_the_largest_difference_is_over_all_pixels_and_channels(tmp_path: Path) -> None:
    a = make_picture(tmp_path / "a.png")
    b = make_with_pixels(tmp_path / "b.png", {(3, 3): (40, 80, 120 + 17, 255), (9, 2): (40 - 31, 80, 120, 255)})
    result = picture_compare.compare_pictures(a, b, channel_tolerance=2)
    assert result.over == 2
    assert result.largest == 31


def test_the_share_limit_allows_the_limit_itself_and_refuses_more(tmp_path: Path) -> None:
    # Expected: engine/renderer.md#comparing, "over at most 0.1 % of the pixels"; 0.1 % of 1000 pixels is one
    a = make_picture(tmp_path / "a.png")
    one = make_with_pixels(tmp_path / "one.png", {(0, 0): (200, 80, 120, 255)})
    two = make_with_pixels(tmp_path / "two.png", {(0, 0): (200, 80, 120, 255), (1, 0): (200, 80, 120, 255)})
    assert main(["picture", "compare", str(a), str(one)]) == 0
    assert main(["picture", "compare", str(a), str(two)]) == 1


def test_the_limits_can_be_set_on_the_command_line(tmp_path: Path) -> None:
    a = make_picture(tmp_path / "a.png", (40, 80, 120, 255))
    b = make_picture(tmp_path / "b.png", (45, 80, 120, 255))
    assert main(["picture", "compare", str(a), str(b)]) == 1
    assert main(["picture", "compare", str(a), str(b), "--channel-tolerance", "5"]) == 0
    assert main(["picture", "compare", str(a), str(b), "--max-share", "100"]) == 0


def test_pictures_of_different_sizes_fail_and_say_so(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    a = make_picture(tmp_path / "a.png", size=(10, 10))
    b = make_picture(tmp_path / "b.png", size=(10, 11))
    assert main(["picture", "compare", str(a), str(b)]) == 1
    assert "size differs: 10x10 and 10x11" in capsys.readouterr().out


def test_folders_are_paired_by_name_and_a_picture_in_one_only_fails(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    for name in ("same", "other"):
        make_picture(tmp_path / "a" / f"{name}.png")
        make_picture(tmp_path / "b" / f"{name}.png")
    make_picture(tmp_path / "a" / "only_a.png")
    make_picture(tmp_path / "b" / "sub" / "only_b.png")
    (tmp_path / "a" / "notes.txt").write_text("not a picture")
    assert main(["picture", "compare", str(tmp_path / "a"), str(tmp_path / "b")]) == 1
    out = capsys.readouterr().out
    assert "only_a.png: missing from the second folder" in out
    assert "sub/only_b.png: missing from the first folder" in out
    assert "notes.txt" not in out
    assert "same.png: 0 of 1000 pixels over 2" in out


def test_two_folders_with_the_same_pictures_pass_and_the_total_is_printed(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    for name in ("one", "two", "three"):
        make_picture(tmp_path / "a" / f"{name}.png")
        make_picture(tmp_path / "b" / f"{name}.png")
    assert main(["picture", "compare", str(tmp_path / "a"), str(tmp_path / "b")]) == 0
    assert "3 pictures: 0 pixels over 2, largest difference 0" in capsys.readouterr().out


def test_a_file_against_a_folder_is_a_usage_error(tmp_path: Path) -> None:
    a = make_picture(tmp_path / "a.png")
    (tmp_path / "b").mkdir()
    with pytest.raises(ConfigError, match="both be pictures or both be folders"):
        picture_compare.compare_paths(a, tmp_path / "b", channel_tolerance=2, max_share_percent=0.1)


def test_an_unreadable_picture_ends_with_exit_code_2_and_names_the_file(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    bad = tmp_path / "bad.png"
    bad.write_bytes(b"not a png")
    good = make_picture(tmp_path / "good.png")
    assert main(["picture", "compare", str(bad), str(good)]) == 2
    assert "bad.png" in capsys.readouterr().err


def test_a_missing_path_ends_with_exit_code_2(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    good = make_picture(tmp_path / "good.png")
    assert main(["picture", "compare", str(good), str(tmp_path / "nowhere.png")]) == 2
    assert "nowhere.png" in capsys.readouterr().err


def test_diff_out_writes_a_picture_that_marks_the_pixels_over(tmp_path: Path) -> None:
    a = make_picture(tmp_path / "a.png")
    b = make_with_pixels(tmp_path / "b.png", {(4, 5): (255, 0, 0, 255)})
    out = tmp_path / "diff"
    assert main(["picture", "compare", str(a), str(b), "--diff-out", str(out)]) == 1
    written = sorted(path.name for path in out.iterdir())
    assert written == ["b.diff.png"] or written == ["a.diff.png"]
    diff = Image.open(next(out.iterdir())).convert("RGBA")
    assert diff.size == (WIDTH, HEIGHT)
    assert diff.getpixel((4, 5)) == (255, 0, 255, 255)
    assert diff.getpixel((0, 0)) != (255, 0, 255, 255)


def test_diff_out_for_folders_keeps_the_relative_name(tmp_path: Path) -> None:
    make_picture(tmp_path / "a" / "sub" / "x.png")
    make_with_pixels(tmp_path / "b" / "sub" / "x.png", {(0, 0): (255, 0, 0, 255)})
    out = tmp_path / "diff"
    main(["picture", "compare", str(tmp_path / "a"), str(tmp_path / "b"), "--diff-out", str(out)])
    assert (out / "sub" / "x.diff.png").is_file()


def test_the_default_limits_are_the_renderer_designs_stand_ins() -> None:
    # Expected: engine/renderer.md#comparing, "at most 2 of 255 apart in any channel, over at most 0.1 %"
    assert picture_compare.DEFAULT_CHANNEL_TOLERANCE == 2
    assert picture_compare.DEFAULT_MAX_SHARE_PERCENT == pytest.approx(0.1)


def test_a_negative_tolerance_or_share_is_refused(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    a = make_picture(tmp_path / "a.png")
    assert main(["picture", "compare", str(a), str(a), "--channel-tolerance", "-1"]) == 2
    assert "channel tolerance" in capsys.readouterr().err
    assert main(["picture", "compare", str(a), str(a), "--max-share", "101"]) == 2
    assert "share" in capsys.readouterr().err
