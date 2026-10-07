# SPDX-License-Identifier: GPL-3.0-or-later
"""The operating-system layer of `coney-tools pcsx2`: starting, finding, closing and looking at PCSX2 processes and
windows.

Windows needs ctypes (this is the only module that touches it); other systems get the process calls and a clear
error from the window calls. Nothing here ever focuses a window or sends input to whichever window has focus: keys go
to a window by its handle (PostMessage), and screenshots read a window by its handle (PrintWindow).

**PCSX2 must never take the keyboard focus** (the machine's owner types while agents work). Do not "simplify" the
launch back to a Popen with a no-activate show command: that was tried and PCSX2 still came to the foreground,
because Qt ignores the show command for its first window and Windows lets a process activate its window when the
foreground process started it, and every agent runs under the owner's foreground terminal. So the launch has two
layers:

1. `start_detached` has WMI (Win32_Process.Create) start PCSX2. Its parent is then WMI's provider host, outside our
   process tree, which holds no foreground rights to hand down. Measured 2026-10-07 with the foreground logged every
   50 ms over a boot to the game: PCSX2 never owned it.
2. `FocusGuard` runs while an Emulator lives: whenever a PCSX2 window still becomes the foreground, it hands the
   foreground straight back to the window that had it before. It never sends input, never minimises PCSX2
   (PrintWindow screenshots need a restored window) and never raises PCSX2.
"""

from __future__ import annotations

import ctypes
import os
import re
import signal
import subprocess
import sys
import threading
import time
from collections.abc import Callable
from ctypes import wintypes
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError

#: Windows' exit code for a process that has not ended.
_STILL_ACTIVE = 259
_ERROR_ACCESS_DENIED = 5


def _need_windows(what: str) -> None:
    """Raise ConfigError unless this is Windows, where `what` is implemented."""
    if sys.platform != "win32":
        raise ConfigError(f"{what} needs Windows (it drives PCSX2's window through the Win32 API)")


@lru_cache(maxsize=1)
def _kernel32() -> Any:
    """kernel32 with the argument and result types of the calls used here (64-bit handles must not truncate)."""
    if sys.platform != "win32":  # callers check first; this also tells mypy the Win32-only ctypes names exist
        raise ConfigError("the Win32 API needs Windows")
    lib = ctypes.WinDLL("kernel32", use_last_error=True)
    lib.OpenProcess.restype = wintypes.HANDLE
    lib.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    lib.CloseHandle.argtypes = [wintypes.HANDLE]
    lib.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    lib.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    lib.QueryFullProcessImageNameW.argtypes = [
        wintypes.HANDLE,
        wintypes.DWORD,
        wintypes.LPWSTR,
        ctypes.POINTER(wintypes.DWORD),
    ]
    lib.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    lib.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    return lib


class _ProcessEntry(ctypes.Structure):
    """PROCESSENTRY32W, for walking the process list."""

    _fields_ = (
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", wintypes.LONG),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * 260),
    )


def pid_alive(pid: int) -> bool:
    """Whether a process with this id exists. (os.kill(pid, 0) would send Ctrl+C on Windows, so ask the API.)"""
    if sys.platform != "win32":
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True
    kernel = _kernel32()
    handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return ctypes.get_last_error() == _ERROR_ACCESS_DENIED
    try:
        code = wintypes.DWORD()
        return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == _STILL_ACTIVE
    finally:
        kernel.CloseHandle(handle)


def process_image(pid: int) -> Path | None:
    """The executable of a process, or None when it is gone or unreadable."""
    if sys.platform == "win32":
        kernel = _kernel32()
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            return None
        try:
            size = wintypes.DWORD(1024)
            buffer = ctypes.create_unicode_buffer(size.value)
            if kernel.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
                return Path(buffer.value)
            return None
        finally:
            kernel.CloseHandle(handle)
    try:
        return Path(f"/proc/{pid}/exe").readlink()
    except OSError:
        pass
    result = subprocess.run(["ps", "-p", str(pid), "-o", "comm="], capture_output=True, text=True, check=False)
    name = result.stdout.strip()
    return Path(name) if name else None


def is_pcsx2(pid: int) -> bool:
    """Whether the process is a PCSX2 (its executable is named pcsx2...), so a reused pid is never mistaken for it."""
    image = process_image(pid)
    return image is not None and "pcsx2" in image.name.lower()


def pcsx2_processes() -> dict[int, Path]:
    """Every running PCSX2 process and its executable (the copy it belongs to is the folder holding it)."""
    found: dict[int, Path] = {}
    if sys.platform == "win32":
        kernel = _kernel32()
        snapshot = kernel.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
        entry = _ProcessEntry()
        entry.dwSize = ctypes.sizeof(_ProcessEntry)
        kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(_ProcessEntry),
        ]
        try:
            more = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
            while more:
                if "pcsx2" in entry.szExeFile.lower():
                    image = process_image(entry.th32ProcessID)
                    if image is not None:
                        found[entry.th32ProcessID] = image
                more = kernel.Process32NextW(snapshot, ctypes.byref(entry))
        finally:
            kernel.CloseHandle(snapshot)
        return found
    result = subprocess.run(["ps", "-axo", "pid=,comm="], capture_output=True, text=True, check=False)
    for line in result.stdout.splitlines():
        match = re.match(r"\s*(\d+)\s+(.*)", line)
        if match and "pcsx2" in match.group(2).lower():
            found[int(match.group(1))] = process_image(int(match.group(1))) or Path(match.group(2))
    return found


#: The show command PCSX2 is started with: SW_SHOWNOACTIVATE (4). Qt ignores it for its first window, so on its own it
#: does not keep the focus off (see the module docstring); WMI's launch and FocusGuard do.
LAUNCH_SHOW = 4


#: PowerShell that has WMI start the program in CONEY_COMMAND_LINE (in CONEY_DIRECTORY) and prints its pid. WMI's
#: provider host, not our process tree, becomes the parent.
_WMI_CREATE = (
    "$s = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ShowWindow=[uint16]%d}; "
    "$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments "
    "@{CommandLine=$env:CONEY_COMMAND_LINE; CurrentDirectory=$env:CONEY_DIRECTORY; ProcessStartupInformation=$s}; "
    'Write-Output "$($r.ReturnValue) $($r.ProcessId)"'
)


def start_detached(command: list[str]) -> int:
    """Start a program outside this process tree and return its pid.

    Windows lets a process take the foreground when the foreground process (the owner's terminal, which every agent
    runs under) started it, so a child of ours may activate its window whatever its show command says. Started by
    WMI (Win32_Process.Create) the program's parent is WMI's provider host, which holds no foreground rights to
    pass on. Elsewhere this is a plain detached Popen. Raises ConfigError when WMI refuses."""
    if sys.platform != "win32":
        return subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True
        ).pid
    env = dict(os.environ)
    env["CONEY_COMMAND_LINE"] = subprocess.list2cmdline(command)
    env["CONEY_DIRECTORY"] = str(Path(command[0]).parent)
    result = subprocess.run(
        ["powershell", "-NoProfile", "-NonInteractive", "-Command", _WMI_CREATE % LAUNCH_SHOW],
        capture_output=True,
        text=True,
        env=env,
        check=False,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    match = re.search(r"(\d+) (\d+)", result.stdout)
    if result.returncode != 0 or match is None or match.group(1) != "0":
        raise ConfigError(f"WMI could not start {command[0]}: {(result.stdout + result.stderr).strip()}")
    return int(match.group(2))


def terminate(pid: int, wait: float = 15.0) -> bool:
    """End a process (PCSX2 saves nothing on this) and wait for it to go; False when it outlived `wait` seconds."""
    if sys.platform == "win32":
        kernel = _kernel32()
        handle = kernel.OpenProcess(0x1, False, pid)  # PROCESS_TERMINATE
        if handle:
            kernel.TerminateProcess(handle, 1)
            kernel.CloseHandle(handle)
    else:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            return True
    deadline = time.monotonic() + wait
    while pid_alive(pid):
        if time.monotonic() > deadline:
            return False
        time.sleep(0.1)
    return True


# --- windows ----------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Window:
    """A window of a process: its handle, class, title, client size and whether it is a top-level window."""

    hwnd: int
    title: str
    cls: str
    width: int
    height: int
    visible: bool
    top_level: bool


@lru_cache(maxsize=1)
def _user32() -> Any:
    """user32 with the argument types of the calls used here."""
    if sys.platform != "win32":  # callers check first; this also tells mypy the Win32-only ctypes names exist
        raise ConfigError("the Win32 API needs Windows")
    lib = ctypes.WinDLL("user32", use_last_error=True)
    enum_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    lib.EnumWindows.argtypes = [enum_proc, wintypes.LPARAM]
    lib.EnumChildWindows.argtypes = [wintypes.HWND, enum_proc, wintypes.LPARAM]
    lib.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    lib.GetWindowThreadProcessId.restype = wintypes.DWORD
    lib.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    lib.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    lib.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    lib.SetProcessDpiAwarenessContext.restype = wintypes.BOOL
    lib.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    lib.IsWindowVisible.argtypes = [wintypes.HWND]
    lib.IsIconic.argtypes = [wintypes.HWND]
    lib.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    lib.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
    lib.GetDC.restype = wintypes.HDC
    lib.GetDC.argtypes = [wintypes.HWND]
    lib.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    lib.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
    lib.GetForegroundWindow.restype = wintypes.HWND
    lib.SetForegroundWindow.argtypes = [wintypes.HWND]
    lib.IsWindow.argtypes = [wintypes.HWND]
    lib.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
    return lib


@lru_cache(maxsize=1)
def _gdi32() -> Any:
    """gdi32 with the argument types of the calls used here."""
    if sys.platform != "win32":  # callers check first; this also tells mypy the Win32-only ctypes names exist
        raise ConfigError("the Win32 API needs Windows")
    lib = ctypes.WinDLL("gdi32", use_last_error=True)
    lib.CreateCompatibleDC.restype = wintypes.HDC
    lib.CreateCompatibleDC.argtypes = [wintypes.HDC]
    lib.CreateCompatibleBitmap.restype = wintypes.HBITMAP
    lib.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
    lib.SelectObject.restype = wintypes.HGDIOBJ
    lib.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
    lib.DeleteObject.argtypes = [wintypes.HGDIOBJ]
    lib.DeleteDC.argtypes = [wintypes.HDC]
    lib.GetDIBits.argtypes = [
        wintypes.HDC,
        wintypes.HBITMAP,
        wintypes.UINT,
        wintypes.UINT,
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.UINT,
    ]
    return lib


def _describe(hwnd: int, top_level: bool) -> Window:
    """A Window record for a handle."""
    user = _user32()
    title = ctypes.create_unicode_buffer(256)
    cls = ctypes.create_unicode_buffer(256)
    user.GetWindowTextW(hwnd, title, 256)
    user.GetClassNameW(hwnd, cls, 256)
    rect = wintypes.RECT()
    user.GetClientRect(hwnd, ctypes.byref(rect))
    return Window(hwnd, title.value, cls.value, rect.right, rect.bottom, bool(user.IsWindowVisible(hwnd)), top_level)


def windows_of(pid: int) -> list[Window]:
    """The windows of a process: its top-level windows, each followed by its child windows (Qt may draw the game in
    one of them). Raises ConfigError off Windows."""
    if sys.platform != "win32":  # tells mypy the Win32-only ctypes names exist
        _need_windows("window access")
        return []
    user = _user32()
    found: list[Window] = []
    enum_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def child(hwnd: int, _: int) -> bool:
        found.append(_describe(hwnd, False))
        return True

    def top(hwnd: int, _: int) -> bool:
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            found.append(_describe(hwnd, True))
            user.EnumChildWindows(hwnd, enum_proc(child), 0)
        return True

    user.EnumWindows(enum_proc(top), 0)
    return found


def _biggest(windows: list[Window], pid: int) -> Window:
    """The visible window with the largest client area; ConfigError when there is none."""
    candidates = [w for w in windows if w.visible and w.width > 0 and w.height > 0]
    if not candidates:
        raise ConfigError(f"process {pid} has no visible window (minimised windows have no client area)")
    return max(candidates, key=lambda w: w.width * w.height)


def render_window(pid: int) -> Window:
    """The window to take a picture of: PCSX2's game widget (a visible child window), which holds the game's picture
    without the menu bar; the largest visible window when there is no child."""
    windows = windows_of(pid)
    return _biggest([w for w in windows if not w.top_level] or windows, pid)


def key_targets(pid: int) -> list[Window]:
    """The windows that get posted keys: the largest visible top-level window. Posting to it reaches the pad bindings
    and hotkeys (Space paused the game, verified); a child as well would press twice."""
    return [_biggest([w for w in windows_of(pid) if w.top_level], pid)]


# --- keeping the focus off PCSX2 ---------------------------------------------------------------------------------


@dataclass(frozen=True)
class FocusCalls:
    """The window calls FocusGuard makes, swappable so tests can fake the desktop: the foreground window, a window's
    process id, whether a window still exists, and handing the foreground to a window."""

    foreground: Callable[[], int]
    owner_pid: Callable[[int], int]
    exists: Callable[[int], bool]
    give_foreground: Callable[[int], bool]


class FocusGuard:
    """Hands the foreground back whenever a window of the guarded process takes it.

    Built before the launch, it remembers the foreground window then (the owner's); `check()` is one poll, and
    `start()` runs it every `interval` seconds on a daemon thread until `stop()`."""

    def __init__(self, calls: FocusCalls, interval: float = 0.05) -> None:
        """Remember the current foreground window as the one to give the focus back to."""
        self.calls = calls
        self.interval = interval
        self.pid: int | None = None
        self.restore_to = calls.foreground()
        self.returned = 0
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def check(self) -> bool:
        """One poll: True when the guarded process held the foreground and it was handed back. A foreground window
        of any other process becomes the one to return to (the owner moved on), so the guard never fights the
        owner's own clicks elsewhere."""
        if self.pid is None:
            return False
        current = self.calls.foreground()
        if not current:
            return False
        if self.calls.owner_pid(current) != self.pid:
            self.restore_to = current
            return False
        if not self.restore_to or not self.calls.exists(self.restore_to):
            return False
        self.calls.give_foreground(self.restore_to)
        self.returned += 1
        return True

    def start(self, pid: int) -> None:
        """Guard process `pid` on a background thread."""
        self.pid = pid
        self._thread = threading.Thread(target=self._run, name="pcsx2-focus-guard", daemon=True)
        self._thread.start()

    def _run(self) -> None:
        """Poll until stopped; a failing call ends only that poll."""
        while not self._stop.wait(self.interval):
            try:
                self.check()
            except OSError:
                continue

    def stop(self) -> None:
        """End the guard thread."""
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1)


def _give_foreground(hwnd: int) -> bool:
    """Make `hwnd` (the owner's window) the foreground again: attach to the input of the thread that holds the
    foreground now (PCSX2's), which allows SetForegroundWindow, then detach. No input is sent."""
    user, kernel = _user32(), _kernel32()
    current = user.GetForegroundWindow()
    theirs = user.GetWindowThreadProcessId(current, None)
    ours = kernel.GetCurrentThreadId()
    attached = bool(theirs and theirs != ours and user.AttachThreadInput(ours, theirs, True))
    try:
        return bool(user.SetForegroundWindow(hwnd))
    finally:
        if attached:
            user.AttachThreadInput(ours, theirs, False)


def _owner_pid(hwnd: int) -> int:
    """The process id of a window."""
    pid = wintypes.DWORD()
    _user32().GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


def focus_guard() -> FocusGuard | None:
    """A FocusGuard on the real desktop, made before PCSX2 starts; None off Windows (nothing to guard there)."""
    if sys.platform != "win32":
        return None
    user = _user32()
    calls = FocusCalls(
        foreground=lambda: int(user.GetForegroundWindow() or 0),
        owner_pid=_owner_pid,
        exists=lambda hwnd: bool(user.IsWindow(hwnd)),
        give_foreground=_give_foreground,
    )
    return FocusGuard(calls)


# --- keys -------------------------------------------------------------------------------------------------------

_NAMED_KEYS = {
    "return": 0x0D,
    "enter": 0x0D,
    "space": 0x20,
    "escape": 0x1B,
    "esc": 0x1B,
    "tab": 0x09,
    "backspace": 0x08,
    "left": 0x25,
    "up": 0x26,
    "right": 0x27,
    "down": 0x28,
    "shift": 0x10,
    "ctrl": 0x11,
    "alt": 0x12,
    **{f"f{n}": 0x70 + n - 1 for n in range(1, 13)},
}
#: Keys whose messages carry the "extended key" bit.
_EXTENDED = {0x25, 0x26, 0x27, 0x28}


def parse_key(name: str) -> int:
    """The Windows virtual-key code of a key name: a letter or digit, `space`, `return`, `up`, `f4`, ... Raises
    ConfigError for anything else."""
    lowered = name.lower()
    if lowered in _NAMED_KEYS:
        return _NAMED_KEYS[lowered]
    if len(name) == 1 and name.isascii() and name.isalnum():
        return ord(name.upper())
    raise ConfigError(f"unknown key {name!r}; use a letter, a digit, space, return, escape, tab, arrows or f1..f12")


def parse_chords(specs: list[str]) -> list[list[int]]:
    """Key arguments as chords: `W+K` is both held together, separate arguments are pressed one after another."""
    return [[parse_key(part) for part in spec.split("+")] for spec in specs]


def _key_lparam(vk: int, up: bool) -> int:
    """The lParam of a key message: repeat count 1, the scan code, the extended bit and (for a release) the
    previous-state and transition bits."""
    scan = _user32().MapVirtualKeyW(vk, 0)
    value = 1 | (scan << 16) | ((1 << 24) if vk in _EXTENDED else 0)
    return value | (0xC0000000 if up else 0)


def post_keys(hwnds: list[int], chords: list[list[int]], hold_ms: int, gap_ms: int) -> None:
    """Post key-down and key-up messages to windows by their handles: each chord goes down together, stays `hold_ms`
    (a 30 Hz game needs about 300), goes up, and `gap_ms` passes before the next. No window gains focus."""
    _need_windows("pcsx2 keys")
    user = _user32()
    for chord in chords:
        for hwnd in hwnds:
            for vk in chord:
                user.PostMessageW(hwnd, 0x0100, vk, _key_lparam(vk, False))  # WM_KEYDOWN
        time.sleep(hold_ms / 1000)
        for hwnd in hwnds:
            for vk in reversed(chord):
                user.PostMessageW(hwnd, 0x0101, vk, _key_lparam(vk, True))  # WM_KEYUP
        time.sleep(gap_ms / 1000)


# --- screenshots -------------------------------------------------------------------------------------------------


class _BitmapInfo(ctypes.Structure):
    """BITMAPINFOHEADER, enough to ask GetDIBits for top-down 32-bit pixels."""

    _fields_ = (
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    )


@lru_cache(maxsize=1)
def ensure_dpi_aware() -> bool:
    """Make this process per-monitor DPI aware (v2), once; True when the call took effect. Without it Windows hands a
    DPI-scaled process virtualised window sizes, so GetClientRect and the PrintWindow bitmap disagree and screenshots
    come out cropped. A no-op off Windows; a failure (the awareness is already set, by a manifest or an earlier call)
    is ignored, since the existing setting is then what the rectangles are measured in."""
    if sys.platform != "win32":
        return False
    try:
        # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is the pseudo-handle -4.
        return bool(_user32().SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)))
    except (OSError, AttributeError):
        return False


def capture(hwnd: int, out: Path) -> tuple[int, int, bool]:
    """Write the client area of a window to a PNG (PrintWindow into a memory bitmap: the window is not focused, moved
    or raised). Returns the width, the height and whether the picture is not all black. Raises ConfigError when the
    window is minimised or empty."""
    _need_windows("pcsx2 screenshot")
    from PIL import Image

    ensure_dpi_aware()  # before any rectangle is read
    user, gdi = _user32(), _gdi32()
    if user.IsIconic(hwnd):
        raise ConfigError("the window is minimised, so it has nothing to draw")
    rect = wintypes.RECT()
    user.GetClientRect(hwnd, ctypes.byref(rect))
    width, height = rect.right, rect.bottom
    if width <= 0 or height <= 0:
        raise ConfigError(f"the window has no client area ({width} x {height})")
    screen = user.GetDC(None)
    memory = gdi.CreateCompatibleDC(screen)
    bitmap = gdi.CreateCompatibleBitmap(screen, width, height)
    old = gdi.SelectObject(memory, bitmap)
    try:
        # PW_CLIENTONLY | PW_RENDERFULLCONTENT: the drawn area, also for windows composed by DirectX.
        user.PrintWindow(hwnd, memory, 3)
        info = _BitmapInfo(ctypes.sizeof(_BitmapInfo), width, -height, 1, 32, 0)
        pixels = ctypes.create_string_buffer(width * height * 4)
        if not gdi.GetDIBits(memory, bitmap, 0, height, pixels, ctypes.byref(info), 0):
            raise ConfigError("GetDIBits failed: could not read the window's picture")
    finally:
        gdi.SelectObject(memory, old)
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(memory)
        user.ReleaseDC(None, screen)
    image = Image.frombuffer("RGBA", (width, height), bytes(pixels), "raw", "BGRA", 0, 1).convert("RGB")
    out.parent.mkdir(parents=True, exist_ok=True)
    image.save(out, "PNG")
    return width, height, image.getbbox() is not None
