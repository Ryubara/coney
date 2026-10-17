"""The port wall: Windows Filtering Platform (WFP) filters that keep every account except the analyst side off the
analyst's local ports.

Folder permissions do not cover network ports, and Windows Firewall rules do not filter loopback traffic, so without
this any local account could connect to an analysis tool listening on 127.0.0.1. For each analyst port Coney adds,
under its own provider and sublayer:

- at the ALE connect layers (IPv4 and IPv6, `connect-v4`/`connect-v6`): a PERMIT for remote port == port whose user
  condition matches only the allowed accounts, and below it a BLOCK for remote port == port that matches everyone;
- at the ALE receive/accept layers (`accept-v4`/`accept-v6`): a BLOCK for local port == port when the traffic is not
  loopback, so nothing from outside the machine (another host, a container network) reaches the port either.

For each analyst-side account whose Claude Code program is there, Coney also adds at both accept layers a BLOCK
whose conditions are that account (ALE_USER_ID: the listening process's user), that program (ALE_APP_ID) and
loopback: every interactive session listens on an undocumented loopback port on a random number, so the program
accepts no loopback connection at all, its own account's included (Microsoft documents the remote user only for
IPsec, so "only from its own account" cannot be expressed).

How the user condition works (Microsoft's WFP documentation: "Filtering condition identifiers" gives
FWPM_CONDITION_ALE_USER_ID the data type FWP_SECURITY_DESCRIPTOR_TYPE; "FWP_CONDITION_VALUE0" requires the
descriptor in self-relative form; the sample in "Permitting and blocking applications and users" states: "When
evaluating SECURITY_DESCRIPTOR conditions, the filter engine checks for FWP_ACTRL_MATCH_FILTER access. If the DACL
grants access, it does not mean that the traffic is allowed; it just means that the condition evaluates to true.
Likewise if it denies access, the condition evaluates to false."). FWP_ACTRL_MATCH_FILTER is 0x1, `CC` in SDDL. So
the permit's descriptor grants CC to the allowed accounts' SIDs and to no one else: the condition is true only for a
token holding one of those SIDs. Anything that makes the access check fail (another account, a deny-only SID, a
restricted or app-container token) leaves the permit unmatched and the connection falls to the block: the wall fails
closed. "FWP_MATCH_TYPE" allows only FWP_MATCH_EQUAL for descriptors (NOT_EQUAL is for sortable types), which is
why the exemption is a permit above a block rather than one block with an inverted condition.

How arbitration settles it ("Filter arbitration"): within a sublayer the matching filters are evaluated from the
highest weight down and the first permit or block decides, so the permit (weight range 15) is reached before the
block (weight range 14) and the sublayer holds nothing else. Across sublayers, which are evaluated from the highest
weight down, a plain filter's block is a hard block that no permit in another sublayer overrides; only a hard permit
evaluated earlier, in a sublayer of higher weight, could. Coney's sublayer therefore asks for the highest weight,
0xFFFF. Another product may already hold that weight (a VPN's sublayer did on the first machine, and the engine
then stored Coney's at 0xFFFE). A sublayer above Coney's only matters if it can overrule Coney's block: a hard
permit (FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT) or a callout, which may clear the action right itself, at one of the
wall's layers and able to match one of its ports. The full check looks for exactly those (`overriding`).
Coney's own permit is a soft permit: any other sublayer may still block that traffic.

Provider, sublayer and filters are persistent: the base filtering engine restores them at every start. `apply`
removes every filter of Coney's provider, the sublayer and the provider, then adds them afresh, all in one
transaction: running it twice leaves the same state, and a failure leaves the previous state untouched.

The pure part (filter specs, descriptors as SDDL) runs anywhere; the Windows part loads fwpuclnt.dll through ctypes.
"""

import ctypes
import re
import uuid
from dataclasses import dataclass, field

# Coney's fixed keys. The filter keys derive from FILTER_NAMESPACE, the layer, the action and the port.
PROVIDER_KEY = uuid.UUID("cd0b77b2-1378-460e-9d28-e801c7498f8a")
SUBLAYER_KEY = uuid.UUID("051287b1-4c61-46e8-88d7-3908c27814e2")
FILTER_NAMESPACE = uuid.UUID("94671dbc-a3ee-4726-9dc7-f37aae0817fb")
SUBLAYER_WEIGHT = 0xFFFF
PERMIT_WEIGHT = 15  # weight ranges (FWP_UINT8, 0-15): the permit is evaluated before the block
BLOCK_WEIGHT = 14
NAME = "Coney port wall"

# Layers and conditions (fwpmu.h).
LAYERS = {
    "connect-v4": uuid.UUID("c38d57d1-05a7-4c33-904f-7fbceee60e82"),  # FWPM_LAYER_ALE_AUTH_CONNECT_V4
    "connect-v6": uuid.UUID("4a72393b-319f-44bc-84c3-ba54dcb3b6b4"),  # FWPM_LAYER_ALE_AUTH_CONNECT_V6
    "accept-v4": uuid.UUID("e1cd9fe7-f4b5-4273-96c0-592e487b8650"),  # FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4
    "accept-v6": uuid.UUID("a3b42c97-9f04-4672-b87e-cee9c483257f"),  # FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6
}
LAYER_NAMES = {v: k for k, v in LAYERS.items()}
COND_REMOTE_PORT = uuid.UUID("c35a604d-d22b-4e1a-91b4-68f674ee674b")  # FWPM_CONDITION_IP_REMOTE_PORT
COND_LOCAL_PORT = uuid.UUID("0c1ba1af-5765-453f-af22-a8f791ac775b")  # FWPM_CONDITION_IP_LOCAL_PORT
COND_USER_ID = uuid.UUID("af043a0a-b34d-4f86-979c-c90371af6e66")  # FWPM_CONDITION_ALE_USER_ID
COND_FLAGS = uuid.UUID("632ce23b-5167-435c-86d7-e903684aa80c")  # FWPM_CONDITION_FLAGS
COND_LOCAL_INTERFACE = uuid.UUID("4cd62a49-59c3-4969-b7f3-bda5d32890a4")  # FWPM_CONDITION_IP_LOCAL_INTERFACE
COND_APP_ID = uuid.UUID("d78e1e87-8644-4ea5-9437-d809ecefc971")  # FWPM_CONDITION_ALE_APP_ID
IF_TYPE_SOFTWARE_LOOPBACK = 24  # ipifcons.h; an interface LUID carries its type in bits 48-63
# Names of other conditions, for showing another product's filters (fwpmu.h).
COND_NAMES = {
    COND_REMOTE_PORT: "remote port",
    COND_LOCAL_PORT: "local port",
    COND_USER_ID: "user",
    COND_FLAGS: "flags",
    uuid.UUID("b235ae9a-1d64-49b8-a44c-5ff3d9095045"): "remote address",
    uuid.UUID("d9ee00de-c1ef-4617-bfe3-ffd8f5a08957"): "local address",
    uuid.UUID("4cd62a49-59c3-4969-b7f3-bda5d32890a4"): "local interface",
    COND_APP_ID: "application",
    uuid.UUID("3971ef2b-623e-4f9a-8cb1-6e79b806b9a7"): "protocol",
}
MATCH_NAMES = {
    0: "==",
    1: ">",
    2: "<",
    3: ">=",
    4: "<=",
    5: "in range",
    6: "all flags set",
    7: "any flag set",
    8: "no flag set",
    9: "== (no case)",
    10: "!=",
    11: "prefix",
    12: "not prefix",
}

# fwptypes.h / fwpmtypes.h
FWP_EMPTY, FWP_UINT8, FWP_UINT16, FWP_UINT32 = 0, 1, 2, 3
FWP_BYTE_BLOB_TYPE, FWP_SECURITY_DESCRIPTOR_TYPE = 12, 14
FWP_MATCH_EQUAL, FWP_MATCH_FLAGS_ALL_SET, FWP_MATCH_FLAGS_NONE_SET = 0, 6, 8
FWP_ACTION_BLOCK, FWP_ACTION_PERMIT = 0x1001, 0x1002
FWP_ACTION_FLAG_CALLOUT = 0x4000
FWP_ACTION_FLAG_NON_TERMINATING = 0x2000
FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT = 0x8
FWP_MATCH_NOT_EQUAL = 10
FWP_CONDITION_FLAG_IS_LOOPBACK = 0x1
FWP_ACTRL_MATCH_FILTER = 0x1
FWPM_PROVIDER_FLAG_PERSISTENT = FWPM_SUBLAYER_FLAG_PERSISTENT = FWPM_FILTER_FLAG_PERSISTENT = 0x1
RPC_C_AUTHN_WINNT = 10
ERROR_ACCESS_DENIED = 5
FWP_E_FILTER_NOT_FOUND = 0x80320003
FWP_E_PROVIDER_NOT_FOUND = 0x80320005
FWP_E_SUBLAYER_NOT_FOUND = 0x80320007
FWP_E_NOT_FOUND = 0x80320008
NOT_FOUND = {FWP_E_FILTER_NOT_FOUND, FWP_E_PROVIDER_NOT_FOUND, FWP_E_SUBLAYER_NOT_FOUND, FWP_E_NOT_FOUND}


# -- pure part: what the wall is ---------------------------------------------------------------------------------------


@dataclass(frozen=True)
class FilterSpec:
    """One filter, as Coney writes it and as it reads one back."""

    layer: str  # a LAYERS name, or the layer GUID for a layer Coney does not use
    action: str  # "block", "permit", or the action number for anything else
    port: int | None
    local_port: bool = False  # the port condition is the local port (accept layers), else the remote port
    users: frozenset | None = None  # SIDs the user condition grants (None: no user condition)
    not_loopback: bool = False  # the condition "traffic is not loopback"
    persistent: bool = True
    other: tuple = ()  # conditions Coney never writes, as text
    weight: int | None = field(default=None, compare=False)  # weight range; swapping weights cannot widen access
    app: str | None = None  # the application condition: an app id (lower-case device path), no port condition
    loopback: bool = False  # the condition "traffic is loopback"

    @property
    def key(self):
        if self.app is not None:  # one per account: its program's filters carry its SID
            return uuid.uuid5(FILTER_NAMESPACE, f"{self.layer}/{self.action}/app/{','.join(sorted(self.users or ()))}")
        return uuid.uuid5(FILTER_NAMESPACE, f"{self.layer}/{self.action}/{self.port}")

    @property
    def target(self):
        """What the filter guards, for reports: its port, or its program's account."""
        if self.port is None and self.app is not None:
            return f"program of {', '.join(sorted(self.users or ())) or 'every user'}"
        return f"port {self.port}"

    def text(self):
        what = [] if self.port is None else [f"{'local' if self.local_port else 'remote'} port {self.port}"]
        if self.app is not None:
            what.append(f"application {self.app}")
        if self.users is not None:
            what.append(f"user in {{{', '.join(sorted(self.users))}}}")
        if self.not_loopback:
            what.append("not loopback")
        if self.loopback:
            what.append("loopback")
        what += list(self.other)
        flags = "" if self.persistent else " (not persistent)"
        return f"{self.action} {self.layer}: {' and '.join(what)}{flags}"


def user_sddl(sids):
    """The user condition's descriptor: FWP_ACTRL_MATCH_FILTER (CC) granted to these SIDs only."""
    return "O:SYG:SYD:" + "".join(f"(A;;CC;;;{s})" for s in sorted(sids))


def users_from_sddl(sddl):
    """The SIDs a user-condition descriptor grants CC to. Any other entry is kept as '?<entry>' so that it never
    compares equal to what Coney writes."""
    m = re.search(r"D:[A-Z_]*((?:\([^()]*\))*)", sddl or "")
    out = set()
    for body in re.findall(r"\(([^()]*)\)", m.group(1) if m else ""):
        parts = body.split(";")
        if len(parts) >= 6 and parts[0] == "A" and parts[1] == "" and parts[2].upper() in ("CC", "0X1"):
            out.add(parts[5].upper())
        else:
            out.add(f"?({body})")
    return frozenset(out)


def expected(ports, allowed_sids):
    """Every filter of the wall for these ports, the same SIDs allowed through each."""
    return expected_per_port({int(p): allowed_sids for p in ports})


def expected_per_port(allowed_by_port):
    """Every filter of the wall: {port: SIDs allowed through it}. A port with no SID gets no permit."""
    out = []
    for port in sorted(allowed_by_port):
        allowed = frozenset(s.upper() for s in allowed_by_port[port])
        for v in ("v4", "v6"):
            if allowed:
                out.append(FilterSpec(f"connect-{v}", "permit", port, users=allowed, weight=PERMIT_WEIGHT))
            out.append(FilterSpec(f"connect-{v}", "block", port, weight=BLOCK_WEIGHT))
            out.append(
                FilterSpec(f"accept-{v}", "block", port, local_port=True, not_loopback=True, weight=BLOCK_WEIGHT)
            )
    return out


def program_blocks(sid, app):
    """The filters that keep one account's program from accepting any loopback connection, from any account (its own
    included): at both accept layers, for that account (the listening process's user) and that program's app id."""
    users = frozenset({sid.upper()})
    return [
        FilterSpec(f"accept-{v}", "block", None, users=users, app=app, loopback=True, weight=BLOCK_WEIGHT)
        for v in ("v4", "v6")
    ]


# -- Windows part: structures --------------------------------------------------------------------------------------


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", ctypes.c_uint32),
        ("Data2", ctypes.c_uint16),
        ("Data3", ctypes.c_uint16),
        ("Data4", ctypes.c_uint8 * 8),
    ]

    @classmethod
    def of(cls, u):
        return cls.from_buffer_copy(u.bytes_le)

    def uuid(self):
        return uuid.UUID(bytes_le=bytes(self))


class FWPM_DISPLAY_DATA0(ctypes.Structure):
    _fields_ = [("name", ctypes.c_wchar_p), ("description", ctypes.c_wchar_p)]


class FWP_BYTE_BLOB(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("data", ctypes.POINTER(ctypes.c_uint8))]


class _Value(ctypes.Union):
    # The pointer members of FWP_VALUE0 / FWP_CONDITION_VALUE0 that Coney uses; the union is pointer-sized.
    _fields_ = [
        ("uint8", ctypes.c_uint8),
        ("uint16", ctypes.c_uint16),
        ("uint32", ctypes.c_uint32),
        ("sd", ctypes.POINTER(FWP_BYTE_BLOB)),
        ("ptr", ctypes.c_void_p),
    ]


class FWP_VALUE0(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_int), ("u", _Value)]


FWP_CONDITION_VALUE0 = FWP_VALUE0  # same layout


class FWPM_FILTER_CONDITION0(ctypes.Structure):
    _fields_ = [("fieldKey", GUID), ("matchType", ctypes.c_int), ("conditionValue", FWP_CONDITION_VALUE0)]


class _ActionU(ctypes.Union):
    _fields_ = [("filterType", GUID), ("calloutKey", GUID)]


class FWPM_ACTION0(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_uint32), ("u", _ActionU)]


class _ContextU(ctypes.Union):
    _fields_ = [("rawContext", ctypes.c_uint64), ("providerContextKey", GUID)]


class FWPM_FILTER0(ctypes.Structure):
    _anonymous_ = ("ctx",)
    _fields_ = [
        ("filterKey", GUID),
        ("displayData", FWPM_DISPLAY_DATA0),
        ("flags", ctypes.c_uint32),
        ("providerKey", ctypes.POINTER(GUID)),
        ("providerData", FWP_BYTE_BLOB),
        ("layerKey", GUID),
        ("subLayerKey", GUID),
        ("weight", FWP_VALUE0),
        ("numFilterConditions", ctypes.c_uint32),
        ("filterCondition", ctypes.POINTER(FWPM_FILTER_CONDITION0)),
        ("action", FWPM_ACTION0),
        ("ctx", _ContextU),
        ("reserved", ctypes.POINTER(GUID)),
        ("filterId", ctypes.c_uint64),
        ("effectiveWeight", FWP_VALUE0),
    ]


class FWPM_PROVIDER0(ctypes.Structure):
    _fields_ = [
        ("providerKey", GUID),
        ("displayData", FWPM_DISPLAY_DATA0),
        ("flags", ctypes.c_uint32),
        ("providerData", FWP_BYTE_BLOB),
        ("serviceName", ctypes.c_wchar_p),
    ]


class FWPM_SUBLAYER0(ctypes.Structure):
    _fields_ = [
        ("subLayerKey", GUID),
        ("displayData", FWPM_DISPLAY_DATA0),
        ("flags", ctypes.c_uint32),
        ("providerKey", ctypes.POINTER(GUID)),
        ("providerData", FWP_BYTE_BLOB),
        ("weight", ctypes.c_uint16),
    ]


class FWPM_FILTER_ENUM_TEMPLATE0(ctypes.Structure):
    _fields_ = [
        ("providerKey", ctypes.POINTER(GUID)),
        ("layerKey", GUID),
        ("enumType", ctypes.c_int),
        ("flags", ctypes.c_uint32),
        ("providerContextTemplate", ctypes.c_void_p),
        ("numFilterConditions", ctypes.c_uint32),
        ("filterCondition", ctypes.POINTER(FWPM_FILTER_CONDITION0)),
        ("actionMask", ctypes.c_uint32),
        ("calloutKey", ctypes.POINTER(GUID)),
    ]


# -- Windows part: descriptors ---------------------------------------------------------------------------------------


def sd_from_sddl(sddl):
    """A self-relative security descriptor's bytes from SDDL (advapi32)."""
    from ctypes import wintypes

    adv = ctypes.WinDLL("advapi32", use_last_error=True)
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LocalFree.argtypes = [ctypes.c_void_p]
    k32.LocalFree.restype = ctypes.c_void_p
    fn = adv.ConvertStringSecurityDescriptorToSecurityDescriptorW
    fn.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.ULONG)]
    fn.restype = wintypes.BOOL
    sd, size = ctypes.c_void_p(), wintypes.ULONG()
    if not fn(sddl, 1, ctypes.byref(sd), ctypes.byref(size)):
        raise WfpError(ctypes.get_last_error(), "building the user condition's security descriptor")
    try:
        return ctypes.string_at(sd, size.value)
    finally:
        k32.LocalFree(sd)


def sddl_from_sd(data):
    """The DACL of a self-relative security descriptor as SDDL."""
    from ctypes import wintypes

    adv = ctypes.WinDLL("advapi32", use_last_error=True)
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LocalFree.argtypes = [ctypes.c_void_p]
    fn = adv.ConvertSecurityDescriptorToStringSecurityDescriptorW
    fn.argtypes = [ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(wintypes.LPWSTR), ctypes.c_void_p]
    fn.restype = wintypes.BOOL
    text = wintypes.LPWSTR()
    if not fn(data, 1, 0x4, ctypes.byref(text), None):  # DACL_SECURITY_INFORMATION
        return "unreadable"
    try:
        return text.value
    finally:
        k32.LocalFree(ctypes.cast(text, ctypes.c_void_p))


# -- Windows part: specs to and from native filters -------------------------------------------------------------------


def native_filter(spec):
    """(FWPM_FILTER0, objects that must stay alive while it is used) for a spec."""
    keep = []
    conds = []

    def cond(key, match, vtype, **value):
        c = FWPM_FILTER_CONDITION0()
        c.fieldKey = GUID.of(key)
        c.matchType = match
        c.conditionValue.type = vtype
        for k, v in value.items():
            setattr(c.conditionValue, k, v)
        conds.append(c)

    if spec.port is not None:
        cond(COND_LOCAL_PORT if spec.local_port else COND_REMOTE_PORT, FWP_MATCH_EQUAL, FWP_UINT16, uint16=spec.port)
    if spec.app is not None:
        data = (spec.app + "\x00").encode("utf-16-le")  # as FwpmGetAppIdFromFileName0 returns it: with its null
        buf = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        blob = FWP_BYTE_BLOB(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_uint8)))
        keep += [buf, blob]
        cond(COND_APP_ID, FWP_MATCH_EQUAL, FWP_BYTE_BLOB_TYPE, ptr=ctypes.cast(ctypes.pointer(blob), ctypes.c_void_p))
    if spec.users is not None:
        data = sd_from_sddl(user_sddl(spec.users))
        buf = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        blob = FWP_BYTE_BLOB(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_uint8)))
        keep += [buf, blob]
        cond(COND_USER_ID, FWP_MATCH_EQUAL, FWP_SECURITY_DESCRIPTOR_TYPE, sd=ctypes.pointer(blob))
    if spec.not_loopback:
        cond(COND_FLAGS, FWP_MATCH_FLAGS_NONE_SET, FWP_UINT32, uint32=FWP_CONDITION_FLAG_IS_LOOPBACK)
    if spec.loopback:
        cond(COND_FLAGS, FWP_MATCH_FLAGS_ALL_SET, FWP_UINT32, uint32=FWP_CONDITION_FLAG_IS_LOOPBACK)
    arr = (FWPM_FILTER_CONDITION0 * len(conds))(*conds)
    provider = GUID.of(PROVIDER_KEY)
    keep += [arr, provider]
    f = FWPM_FILTER0()
    f.filterKey = GUID.of(spec.key)
    f.displayData.name = f"{NAME}: {spec.action} {spec.layer} {spec.target}"
    f.displayData.description = spec.text()
    f.flags = FWPM_FILTER_FLAG_PERSISTENT if spec.persistent else 0
    f.providerKey = ctypes.pointer(provider)
    f.layerKey = GUID.of(LAYERS[spec.layer])
    f.subLayerKey = GUID.of(SUBLAYER_KEY)
    if spec.weight is None:
        f.weight.type = FWP_EMPTY
    else:
        f.weight.type = FWP_UINT8
        f.weight.uint8 = spec.weight
    f.numFilterConditions = len(conds)
    f.filterCondition = ctypes.cast(arr, ctypes.POINTER(FWPM_FILTER_CONDITION0))
    f.action.type = FWP_ACTION_BLOCK if spec.action == "block" else FWP_ACTION_PERMIT
    return f, keep


def spec_of(f, sddl_of=sddl_from_sd):
    """The FilterSpec a native filter amounts to; conditions Coney never writes land in `other`."""
    layer = LAYER_NAMES.get(f.layerKey.uuid(), str(f.layerKey.uuid()))
    action = {FWP_ACTION_BLOCK: "block", FWP_ACTION_PERMIT: "permit"}.get(f.action.type, f"action 0x{f.action.type:x}")
    port, local, users, not_loop, other = None, False, None, False, []
    app, loop = None, False
    for i in range(f.numFilterConditions):
        c = f.filterCondition[i]
        key, v = c.fieldKey.uuid(), c.conditionValue
        shape = (key, c.matchType, v.type)
        if shape in ((COND_REMOTE_PORT, FWP_MATCH_EQUAL, FWP_UINT16), (COND_LOCAL_PORT, FWP_MATCH_EQUAL, FWP_UINT16)):
            if port is None:
                port, local = v.uint16, key == COND_LOCAL_PORT
                continue
        elif shape == (COND_USER_ID, FWP_MATCH_EQUAL, FWP_SECURITY_DESCRIPTOR_TYPE):
            if users is None and v.sd:
                users = users_from_sddl(sddl_of(ctypes.string_at(v.sd.contents.data, v.sd.contents.size)))
                continue
        elif shape == (COND_FLAGS, FWP_MATCH_FLAGS_NONE_SET, FWP_UINT32) and not not_loop:
            not_loop = v.uint32 == FWP_CONDITION_FLAG_IS_LOOPBACK
            if not_loop:
                continue
        elif shape == (COND_FLAGS, FWP_MATCH_FLAGS_ALL_SET, FWP_UINT32) and not loop:
            loop = v.uint32 == FWP_CONDITION_FLAG_IS_LOOPBACK
            if loop:
                continue
        elif shape == (COND_APP_ID, FWP_MATCH_EQUAL, FWP_BYTE_BLOB_TYPE) and app is None and v.ptr:
            blob = FWP_BYTE_BLOB.from_address(v.ptr)
            app = ctypes.string_at(blob.data, blob.size).decode("utf-16-le", "replace").rstrip("\x00")
            continue
        other.append(f"condition {key} match {c.matchType} type {v.type}")
    weight = f.weight.uint8 if f.weight.type == FWP_UINT8 else None
    persistent = bool(f.flags & FWPM_FILTER_FLAG_PERSISTENT)
    return FilterSpec(
        layer, action, port, local, users, not_loop, persistent, tuple(other), weight, app=app, loopback=loop
    )


# -- Windows part: the engine ----------------------------------------------------------------------------------------


class WfpError(OSError):
    def __init__(self, code, what):
        self.code = code & 0xFFFFFFFF
        why = "access denied: run it elevated" if self.code == ERROR_ACCESS_DENIED else f"error 0x{self.code:08x}"
        super().__init__(None, f"{what}: {why}")

    def __str__(self):
        return self.args[1]

    @property
    def denied(self):
        return self.code == ERROR_ACCESS_DENIED


P_GUID = ctypes.POINTER(GUID)


def load_dll():
    if not hasattr(ctypes, "WinDLL"):
        raise OSError(None, "the port wall is made of Windows Filtering Platform filters: this needs Windows")
    from ctypes import wintypes

    dll = ctypes.WinDLL("fwpuclnt")
    H, P, G = wintypes.HANDLE, ctypes.POINTER, P_GUID
    sigs = {
        "FwpmEngineOpen0": [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p, P(H)],
        "FwpmEngineClose0": [H],
        "FwpmTransactionBegin0": [H, ctypes.c_uint32],
        "FwpmTransactionCommit0": [H],
        "FwpmTransactionAbort0": [H],
        "FwpmProviderAdd0": [H, P(FWPM_PROVIDER0), ctypes.c_void_p],
        "FwpmProviderDeleteByKey0": [H, G],
        "FwpmSubLayerAdd0": [H, P(FWPM_SUBLAYER0), ctypes.c_void_p],
        "FwpmSubLayerDeleteByKey0": [H, G],
        "FwpmSubLayerGetByKey0": [H, G, P(P(FWPM_SUBLAYER0))],
        "FwpmFilterAdd0": [H, P(FWPM_FILTER0), ctypes.c_void_p, P(ctypes.c_uint64)],
        "FwpmFilterDeleteByKey0": [H, G],
        "FwpmFilterCreateEnumHandle0": [H, P(FWPM_FILTER_ENUM_TEMPLATE0), P(H)],
        "FwpmFilterEnum0": [H, H, ctypes.c_uint32, P(P(P(FWPM_FILTER0))), P(ctypes.c_uint32)],
        "FwpmFilterDestroyEnumHandle0": [H, H],
        "FwpmSubLayerCreateEnumHandle0": [H, ctypes.c_void_p, P(H)],
        "FwpmSubLayerEnum0": [H, H, ctypes.c_uint32, P(P(P(FWPM_SUBLAYER0))), P(ctypes.c_uint32)],
        "FwpmSubLayerDestroyEnumHandle0": [H, H],
    }
    for name, args in sigs.items():
        fn = getattr(dll, name)
        fn.argtypes, fn.restype = args, wintypes.DWORD
    dll.FwpmFreeMemory0.argtypes, dll.FwpmFreeMemory0.restype = [P(ctypes.c_void_p)], None
    dll.FwpmGetAppIdFromFileName0.argtypes = [ctypes.c_wchar_p, P(P(FWP_BYTE_BLOB))]
    dll.FwpmGetAppIdFromFileName0.restype = wintypes.DWORD
    return dll


ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND = 2, 3


def app_id(path, dll=None):
    """A program's app id, as the ALE_APP_ID condition matches it (FwpmGetAppIdFromFileName0: the lower-case device
    path), or None when the file is not there. The file must be readable: other accounts' programs need elevation."""
    dll = dll or load_dll()
    out = ctypes.POINTER(FWP_BYTE_BLOB)()
    rc = dll.FwpmGetAppIdFromFileName0(str(path), ctypes.byref(out)) & 0xFFFFFFFF
    if rc in (ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND):
        return None
    if rc:
        raise WfpError(rc, f"reading the app id of {path}")
    try:
        b = out.contents
        return ctypes.string_at(b.data, b.size).decode("utf-16-le", "replace").rstrip("\x00")
    finally:
        dll.FwpmFreeMemory0(ctypes.byref(ctypes.cast(out, ctypes.c_void_p)))


class Engine:
    """An open session with the base filtering engine (not dynamic: what it adds outlives it)."""

    def __init__(self, dll=None):
        from ctypes import wintypes

        self.dll = dll or load_dll()
        self.handle = wintypes.HANDLE()
        self._call(
            "FwpmEngineOpen0",
            "opening the filtering engine",
            None,
            RPC_C_AUTHN_WINNT,
            None,
            None,
            ctypes.byref(self.handle),
        )

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self.handle:
            self.dll.FwpmEngineClose0(self.handle)
            self.handle = None

    def _call(self, name, what, *args, missing_ok=False):
        rc = getattr(self.dll, name)(*args) & 0xFFFFFFFF
        if rc and not (missing_ok and rc in NOT_FOUND):
            raise WfpError(rc, what)
        return rc

    def begin(self):
        self._call("FwpmTransactionBegin0", "starting a transaction", self.handle, 0)

    def commit(self):
        self._call("FwpmTransactionCommit0", "committing the transaction", self.handle)

    def abort(self):
        self.dll.FwpmTransactionAbort0(self.handle)

    def add_provider(self):
        p = FWPM_PROVIDER0()
        p.providerKey = GUID.of(PROVIDER_KEY)
        p.displayData.name = "Coney"
        p.displayData.description = "Coney's zones: the port wall around the analyst's local ports"
        p.flags = FWPM_PROVIDER_FLAG_PERSISTENT
        self._call("FwpmProviderAdd0", "adding Coney's provider", self.handle, ctypes.byref(p), None)

    def delete_provider(self):
        g = GUID.of(PROVIDER_KEY)
        return (
            self._call(
                "FwpmProviderDeleteByKey0", "removing Coney's provider", self.handle, ctypes.byref(g), missing_ok=True
            )
            == 0
        )

    def add_sublayer(self):
        s = FWPM_SUBLAYER0()
        s.subLayerKey = GUID.of(SUBLAYER_KEY)
        s.displayData.name = NAME
        s.displayData.description = "Blocks the analyst's local ports for every account except the analyst side"
        s.flags = FWPM_SUBLAYER_FLAG_PERSISTENT
        provider = GUID.of(PROVIDER_KEY)
        s.providerKey = ctypes.pointer(provider)
        s.weight = SUBLAYER_WEIGHT
        self._call("FwpmSubLayerAdd0", "adding Coney's sublayer", self.handle, ctypes.byref(s), None)

    def delete_sublayer(self):
        g = GUID.of(SUBLAYER_KEY)
        return (
            self._call(
                "FwpmSubLayerDeleteByKey0", "removing Coney's sublayer", self.handle, ctypes.byref(g), missing_ok=True
            )
            == 0
        )

    def sublayer_weight(self):
        """The weight of Coney's sublayer, or None when it is not there."""
        g = GUID.of(SUBLAYER_KEY)
        out = ctypes.POINTER(FWPM_SUBLAYER0)()
        rc = self._call(
            "FwpmSubLayerGetByKey0",
            "reading Coney's sublayer",
            self.handle,
            ctypes.byref(g),
            ctypes.byref(out),
            missing_ok=True,
        )
        if rc:
            return None
        try:
            return out.contents.weight
        finally:
            self.dll.FwpmFreeMemory0(ctypes.byref(ctypes.cast(out, ctypes.c_void_p)))

    def add_filter(self, spec):
        f, _keep = native_filter(spec)
        fid = ctypes.c_uint64()
        self._call(
            "FwpmFilterAdd0", f"adding filter '{spec.text()}'", self.handle, ctypes.byref(f), None, ctypes.byref(fid)
        )
        return fid.value

    def delete_filter(self, key):
        g = GUID.of(key)
        self._call("FwpmFilterDeleteByKey0", f"removing filter {key}", self.handle, ctypes.byref(g), missing_ok=True)

    def filters(self, layer):
        """[(key, FilterSpec)] for every filter of Coney's provider at a layer (a LAYERS name)."""
        from ctypes import wintypes

        provider = GUID.of(PROVIDER_KEY)
        t = FWPM_FILTER_ENUM_TEMPLATE0()
        t.providerKey = ctypes.pointer(provider)
        t.layerKey = GUID.of(LAYERS[layer])
        t.enumType = 1  # FWP_FILTER_ENUM_OVERLAPPING: no conditions given, so every filter of the provider
        t.actionMask = 0xFFFFFFFF
        eh = wintypes.HANDLE()
        self._call("FwpmFilterCreateEnumHandle0", "listing filters", self.handle, ctypes.byref(t), ctypes.byref(eh))
        out = []
        try:
            while True:
                entries = ctypes.POINTER(ctypes.POINTER(FWPM_FILTER0))()
                n = ctypes.c_uint32()
                self._call(
                    "FwpmFilterEnum0", "listing filters", self.handle, eh, 64, ctypes.byref(entries), ctypes.byref(n)
                )
                try:
                    for i in range(n.value):
                        f = entries[i].contents
                        out.append((f.filterKey.uuid(), spec_of(f)))
                finally:
                    if entries:
                        self.dll.FwpmFreeMemory0(ctypes.byref(ctypes.cast(entries, ctypes.c_void_p)))
                if n.value < 64:
                    return out
        finally:
            self.dll.FwpmFilterDestroyEnumHandle0(self.handle, eh)

    def all_filters(self):
        return [item for layer in LAYERS for item in self.filters(layer)]

    def layer_filters(self, layer):
        """Every filter at a layer (a LAYERS name), from any provider, as (name, sublayer key, action type, flags,
        {port condition key: [(match type, value type, uint16 value or None)]})."""
        from ctypes import wintypes

        t = FWPM_FILTER_ENUM_TEMPLATE0()
        t.layerKey = GUID.of(LAYERS[layer])
        t.enumType = 1  # FWP_FILTER_ENUM_OVERLAPPING with no conditions: every filter at the layer
        t.actionMask = 0xFFFFFFFF
        eh = wintypes.HANDLE()
        self._call("FwpmFilterCreateEnumHandle0", "listing filters", self.handle, ctypes.byref(t), ctypes.byref(eh))
        out = []
        try:
            while True:
                entries = ctypes.POINTER(ctypes.POINTER(FWPM_FILTER0))()
                n = ctypes.c_uint32()
                self._call(
                    "FwpmFilterEnum0", "listing filters", self.handle, eh, 64, ctypes.byref(entries), ctypes.byref(n)
                )
                try:
                    for i in range(n.value):
                        f = entries[i].contents
                        ports, shown, ifaces = {}, [], []
                        for j in range(f.numFilterConditions):
                            c = f.filterCondition[j]
                            key = c.fieldKey.uuid()
                            v = c.conditionValue
                            if key in (COND_REMOTE_PORT, COND_LOCAL_PORT):
                                value = v.uint16 if v.type == FWP_UINT16 else None
                                ports.setdefault(key, []).append((c.matchType, v.type, value))
                            if key == COND_LOCAL_INTERFACE:
                                luid = ctypes.c_uint64.from_address(v.ptr).value if v.type == 4 and v.ptr else None
                                ifaces.append((c.matchType, luid))
                            shown.append(describe_condition(key, c.matchType, v))
                        name = f.displayData.name or ""
                        out.append((name, f.subLayerKey.uuid(), f.action.type, f.flags, ports, shown, ifaces))
                finally:
                    if entries:
                        self.dll.FwpmFreeMemory0(ctypes.byref(ctypes.cast(entries, ctypes.c_void_p)))
                if n.value < 64:
                    return out
        finally:
            self.dll.FwpmFilterDestroyEnumHandle0(self.handle, eh)

    def sublayers(self):
        """[(key, name, weight)] for every sublayer on the machine, Coney's included."""
        from ctypes import wintypes

        eh = wintypes.HANDLE()
        self._call("FwpmSubLayerCreateEnumHandle0", "listing sublayers", self.handle, None, ctypes.byref(eh))
        out = []
        try:
            while True:
                entries = ctypes.POINTER(ctypes.POINTER(FWPM_SUBLAYER0))()
                n = ctypes.c_uint32()
                self._call(
                    "FwpmSubLayerEnum0",
                    "listing sublayers",
                    self.handle,
                    eh,
                    64,
                    ctypes.byref(entries),
                    ctypes.byref(n),
                )
                try:
                    for i in range(n.value):
                        s = entries[i].contents
                        out.append((s.subLayerKey.uuid(), s.displayData.name or "", s.weight))
                finally:
                    if entries:
                        self.dll.FwpmFreeMemory0(ctypes.byref(ctypes.cast(entries, ctypes.c_void_p)))
                if n.value < 64:
                    return out
        finally:
            self.dll.FwpmSubLayerDestroyEnumHandle0(self.handle, eh)


# -- operations (engine-agnostic, so tests can drive them with a fake engine) ----------------------------------------


def _clear(engine):
    """Removes every filter of Coney's provider, then its sublayer and the provider. Returns the filters removed."""
    gone = engine.all_filters()
    for key, _ in gone:
        engine.delete_filter(key)
    engine.delete_sublayer()
    engine.delete_provider()
    return [s for _, s in gone]


def _in_transaction(engine, work):
    engine.begin()
    try:
        out = work()
    except BaseException:
        engine.abort()
        raise
    engine.commit()
    return out


def apply(engine, specs):
    """Replaces Coney's wall with these filters in one transaction. Returns (removed, added) specs."""

    def work():
        removed = _clear(engine)
        engine.add_provider()
        engine.add_sublayer()
        for s in specs:
            engine.add_filter(s)
        return removed, list(specs)

    return _in_transaction(engine, work)


def remove(engine):
    """Removes Coney's filters, sublayer and provider in one transaction. Returns the filters removed."""
    return _in_transaction(engine, lambda: _clear(engine))


def read_state(engine):
    """(sublayer weight or None, [FilterSpec]) for Coney's wall as it stands."""
    return engine.sublayer_weight(), [s for _, s in engine.all_filters()]


def outranking(weight, sublayers):
    """(above, level): the other sublayers that weigh more than Coney's (any of them could hard-permit before Coney's
    block is reached) and those that weigh the same (their order against Coney's is not defined), as (name, weight)."""
    others = [(name, w) for key, name, w in sublayers if key != SUBLAYER_KEY]
    if weight is None:
        return others, []
    return [o for o in others if o[1] > weight], [o for o in others if o[1] == weight]


def read_others(engine):
    """Every sublayer but Coney's, as (key, name, weight), for `outranking`."""
    return [s for s in engine.sublayers() if s[0] != SUBLAYER_KEY]


def could_override(action, flags, conditions, port):
    """Whether a filter in a sublayer evaluated before Coney's could overrule Coney's block of `port`: it must be a
    hard permit or a callout, and its conditions on the port field (several on one field are alternatives) must not
    rule the port out. A condition it cannot read is taken to match."""
    hard_permit = action == FWP_ACTION_PERMIT and flags & FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT
    # an inspection callout (non-terminating) can neither permit nor block
    decides = action & FWP_ACTION_FLAG_CALLOUT and not action & FWP_ACTION_FLAG_NON_TERMINATING
    if not (hard_permit or decides):
        return False
    if not conditions:
        return True
    for match, vtype, value in conditions:
        if vtype != FWP_UINT16 or match not in (FWP_MATCH_EQUAL, FWP_MATCH_NOT_EQUAL):
            return True
        if (match == FWP_MATCH_EQUAL) == (value == port):
            return True
    return False


def off_loopback(ifaces):
    """Whether a filter's local-interface conditions (alternatives) all name an interface that is not loopback: such
    a filter never sees traffic to 127.0.0.1 or ::1, only traffic on that interface's own addresses."""
    return bool(ifaces) and all(
        m == FWP_MATCH_EQUAL and luid is not None and luid >> 48 != IF_TYPE_SOFTWARE_LOOPBACK for m, luid in ifaces
    )


def overriding(engine, weight, ports):
    """Filters in other sublayers weighing as much as Coney's or more (the order between equal weights is not
    defined) that could overrule the wall on one of `ports`, as (filter name, sublayer name, layer, port, its
    conditions as text, scope): scope "off-loopback" when the filter is limited to interfaces other than loopback
    (it matters only if an analyst port listens on a non-loopback address), else "any"."""
    subs = {key: (name, w) for key, name, w in engine.sublayers() if key != SUBLAYER_KEY}
    out = []
    for layer in LAYERS:
        field = COND_REMOTE_PORT if layer.startswith("connect") else COND_LOCAL_PORT
        for entry in engine.layer_filters(layer):
            name, sub, action, flags, conds = entry[:5]
            shown = list(entry[5]) if len(entry) > 5 else []
            scope = "off-loopback" if off_loopback(entry[6] if len(entry) > 6 else []) else "any"
            if sub not in subs or (weight is not None and subs[sub][1] < weight):
                continue
            for port in ports:
                if could_override(action, flags, conds.get(field, []), port):
                    out.append((name or "(unnamed)", subs[sub][0] or "(unnamed)", layer, port, shown, scope))
    return out


def describe_condition(key, match, v):
    """One condition of a native filter as text: field, match and value (addresses, interfaces and applications
    decoded where the type is known; anything else by its type number)."""
    field = COND_NAMES.get(key, str(key))
    op = MATCH_NAMES.get(match, f"match {match}")
    try:
        if v.type in (FWP_UINT8, FWP_UINT16, FWP_UINT32):
            n = {FWP_UINT8: v.uint8, FWP_UINT16: v.uint16, FWP_UINT32: v.uint32}[v.type]
            if field in ("remote address", "local address") and v.type == FWP_UINT32:
                value = ".".join(str((n >> s) & 0xFF) for s in (24, 16, 8, 0))
            else:
                value = str(n)
        elif v.type == 4 and v.ptr:  # FWP_UINT64, by pointer (an interface LUID)
            value = str(ctypes.c_uint64.from_address(v.ptr).value)
        elif v.type == 0x100 and v.ptr:  # FWP_V4_ADDR_MASK: address, mask
            a, m = (ctypes.c_uint32 * 2).from_address(v.ptr)
            value = "/".join(".".join(str((n >> s) & 0xFF) for s in (24, 16, 8, 0)) for n in (a, m))
        elif v.type == 0x101 and v.ptr:  # FWP_V6_ADDR_MASK: 16 bytes, prefix length
            raw = bytes((ctypes.c_uint8 * 17).from_address(v.ptr))
            value = ":".join(raw[i : i + 2].hex() for i in range(0, 16, 2)) + f"/{raw[16]}"
        elif v.type == 11 and v.ptr:  # FWP_BYTE_ARRAY16_TYPE: an IPv6 address
            raw = bytes((ctypes.c_uint8 * 16).from_address(v.ptr))
            value = ":".join(raw[i : i + 2].hex() for i in range(0, 16, 2))
        elif v.type == 12 and v.ptr:  # FWP_BYTE_BLOB_TYPE: an application id is a UTF-16 path
            blob = FWP_BYTE_BLOB.from_address(v.ptr)
            value = ctypes.string_at(blob.data, blob.size).decode("utf-16-le", "replace").rstrip("\x00")
        else:
            value = f"(type {v.type})"
    except (ValueError, OSError):
        value = f"(type {v.type}, unreadable)"
    return f"{field} {op} {value}"
