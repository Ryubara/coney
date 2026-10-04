# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.config: loading coney.local.toml and finding the repository root."""

from pathlib import Path

import pytest

from coney_tools.config import PATH_KEYS, ConfigError, find_repo_root, load_config


def make_repo(tmp_path: Path, local: str | None) -> Path:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    if local is not None:
        (tmp_path / "coney.local.toml").write_text(local, encoding="utf-8")
    return tmp_path


def test_relative_paths_resolve_against_the_repo_root(tmp_path: Path) -> None:
    root = make_repo(tmp_path, 'game_dir = "../game"\n')
    assert load_config(root).paths["game_dir"] == (root / "../game").resolve()


def test_absolute_paths_are_kept(tmp_path: Path) -> None:
    target = tmp_path / "elsewhere"
    # A TOML literal string (single quotes): Windows backslashes are not escapes there.
    root = make_repo(tmp_path, f"scratch_dir = '{target}'\n")
    assert load_config(root).paths["scratch_dir"] == target.resolve()


def test_empty_and_missing_keys_are_unset(tmp_path: Path) -> None:
    config = load_config(make_repo(tmp_path, 'jdk_home = ""\n'))
    assert set(config.paths) == set(PATH_KEYS)
    assert config.paths["jdk_home"] is None
    assert config.paths["pcsx2_dir"] is None


def test_missing_file_names_the_example(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match=r"coney\.local\.example\.toml"):
        load_config(make_repo(tmp_path, None))


def test_invalid_toml_names_the_file(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match=r"coney\.local\.toml"):
        load_config(make_repo(tmp_path, "game_dir = \n"))


def test_unknown_key_is_named(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="game_dri"):
        load_config(make_repo(tmp_path, 'game_dri = "x"\n'))


def test_non_string_path_is_named(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="game_dir"):
        load_config(make_repo(tmp_path, "game_dir = 3\n"))


def test_find_repo_root_walks_up(tmp_path: Path) -> None:
    root = make_repo(tmp_path, None)
    nested = root / "a" / "b"
    nested.mkdir(parents=True)
    assert find_repo_root(nested) == root.resolve()


def test_find_repo_root_fails_clearly(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match=r"coney\.local\.example\.toml"):
        find_repo_root(tmp_path)


def test_non_utf8_file_names_the_file(tmp_path: Path) -> None:
    root = make_repo(tmp_path, None)
    # What Notepad writes for "game_dir = ''" when saving as UTF-16: a BOM, then two bytes per character.
    (root / "coney.local.toml").write_bytes(b"\xff\xfeg\x00a\x00")
    with pytest.raises(ConfigError, match=r"coney\.local\.toml"):
        load_config(root)
