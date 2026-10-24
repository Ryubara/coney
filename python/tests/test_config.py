# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.config: loading coney.local.toml and finding the repository root."""

import os
import subprocess
from pathlib import Path

import pytest

from coney_tools import config
from coney_tools.config import PATH_KEYS, ConfigError, find_repo_root, load_config, local_config_file, main_checkout


def make_repo(tmp_path: Path, local: str | None) -> Path:
    """A made-up checkout with `local` as its coney.local.toml, or none when `local` is None."""
    tmp_path.mkdir(parents=True, exist_ok=True)
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


def make_worktree(main: Path, worktree: Path) -> None:
    """Turn `main` into a git repository holding the example file and add `worktree` as a worktree of it."""
    env = {
        **os.environ,
        "GIT_AUTHOR_NAME": "t",
        "GIT_AUTHOR_EMAIL": "t@example.invalid",
        "GIT_COMMITTER_NAME": "t",
        "GIT_COMMITTER_EMAIL": "t@example.invalid",
    }
    for args in (
        ["init", "-q"],
        ["add", "coney.local.example.toml"],
        ["commit", "-q", "-m", "x"],
        ["worktree", "add", "-q", "--detach", str(worktree)],
    ):
        subprocess.run(["git", "-C", str(main), *args], check=True, capture_output=True, env=env)


def test_a_real_worktree_reads_the_main_checkouts_file(tmp_path: Path) -> None:
    main = make_repo(tmp_path / "repos" / "coney", None)
    (main / "coney.local.toml").write_text('game_dir = "../../game"\n', encoding="utf-8")
    worktree = tmp_path / "worktrees" / "coney" / "branch"
    make_worktree(main, worktree)

    assert not (worktree / "coney.local.toml").exists()
    assert local_config_file(worktree) == main / "coney.local.toml"
    # Relative paths resolve from the main checkout, not from the worktree three levels deeper.
    assert load_config(worktree).paths["game_dir"] == (tmp_path / "game").resolve()


def test_a_worktree_with_its_own_file_keeps_it(tmp_path: Path) -> None:
    main = make_repo(tmp_path / "repos" / "coney", 'game_dir = "../../game"\n')
    worktree = tmp_path / "worktrees" / "coney" / "branch"
    make_worktree(main, worktree)
    (worktree / "coney.local.toml").write_text('game_dir = "own"\n', encoding="utf-8")

    assert local_config_file(worktree) == worktree / "coney.local.toml"
    assert load_config(worktree).paths["game_dir"] == (worktree / "own").resolve()


def test_a_worktree_without_any_file_names_both_checkouts(tmp_path: Path) -> None:
    main = make_repo(tmp_path / "repos" / "coney", None)
    worktree = tmp_path / "worktrees" / "coney" / "branch"
    make_worktree(main, worktree)

    assert local_config_file(worktree) is None
    with pytest.raises(ConfigError) as error:
        load_config(worktree)
    assert str(main) in str(error.value)
    assert str(worktree) in str(error.value)


def test_main_checkout_is_the_resolver_for_a_faked_worktree(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    main = make_repo(tmp_path / "main", 'scratch_dir = "s"\n')
    fake = make_repo(tmp_path / "fake", None)
    monkeypatch.setattr(config, "main_checkout", lambda root: main)
    assert load_config(fake).paths["scratch_dir"] == (main / "s").resolve()


def test_main_checkout_of_a_plain_folder_is_itself(tmp_path: Path) -> None:
    assert main_checkout(tmp_path) == tmp_path


def test_workspace_root_is_two_levels_up_in_the_workspace_layout(tmp_path: Path) -> None:
    checkout = make_repo(tmp_path / "ws" / "repos" / "coney", None)
    assert config.workspace_root(checkout) == (tmp_path / "ws").resolve()
    assert config.workspace_folder(checkout, "scratch") == (tmp_path / "ws" / "scratch").resolve()


def test_a_plain_clone_keeps_its_workspace_folders_beside_itself(tmp_path: Path) -> None:
    # A clone anywhere else (~/code/coney) must not write two levels above itself, nor inside itself: the Ghidra
    # projects, the emulators and the BIOS never go in the repository.
    checkout = make_repo(tmp_path / "code" / "coney", None)
    workspace = (tmp_path / "code" / "coney-workspace").resolve()
    assert config.workspace_root(checkout) == workspace
    assert config.workspace_folder(checkout, "ghidra") == workspace / "ghidra"
    assert not config.workspace_folder(checkout, "emulators").is_relative_to(checkout.resolve())


def test_a_worktree_uses_its_main_checkouts_workspace(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    main = make_repo(tmp_path / "ws" / "repos" / "coney", None)
    worktree = make_repo(tmp_path / "ws" / "worktrees" / "coney" / "branch", None)
    monkeypatch.setattr(config, "main_checkout", lambda root: main)
    assert config.workspace_root(worktree) == (tmp_path / "ws").resolve()


def test_the_disc_for_coney_is_the_one_iso_in_the_folder_else_the_folder(tmp_path: Path) -> None:
    folder = tmp_path / "game"
    folder.mkdir()
    assert config.disc_for_coney(folder) == folder  # extracted files, or no image yet
    (folder / "warriors.iso").write_bytes(b"")
    assert config.disc_for_coney(folder) == folder / "warriors.iso"
    (folder / "other.iso").write_bytes(b"")
    assert config.disc_for_coney(folder) == folder  # two images: not guessed
    assert config.disc_for_coney(folder / "warriors.iso") == folder / "warriors.iso"  # an image named itself


def test_the_retired_clang_tidy_keys_are_still_accepted_and_ignored(tmp_path: Path) -> None:
    """A coney.local.toml written while the local clang-tidy gate existed still loads; the keys mean nothing now."""
    root = make_repo(tmp_path, 'tidy_slots = 2\ntidy_jobs = 4\ntidy_cache_dir = "cache/tidy"\nbuild_slots = 3\n')
    loaded = load_config(root)
    assert loaded.numbers["build_slots"] == 3
    assert "tidy_slots" not in loaded.numbers and "tidy_cache_dir" not in loaded.paths
    assert set(config.RETIRED_KEYS) == {"tidy_slots", "tidy_jobs", "tidy_cache_dir"}
