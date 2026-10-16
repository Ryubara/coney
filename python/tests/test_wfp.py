"""Tests for the port wall (wfp.py), `zones ports` and the full check's filter comparison.

Adding filters needs administrator rights, so the filtering engine is replaced: by a fake engine for the operations,
and by a fake fwpuclnt.dll for the native layer. Accounts, SIDs and ports are made up."""

import ctypes
import io
import os
import sys
import unittest
import uuid
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import wfp, zones
from coney_tools.cli import main

ANA, REV = "S-1-5-21-7-7-7-2001", "S-1-5-21-7-7-7-2002"
PORTS = [4101, 4102]
WINDOWS = unittest.skipUnless(os.name == "nt", "Windows security descriptors and fwpuclnt structures")


class Specs(unittest.TestCase):
    def test_expected_filters(self):
        specs = wfp.expected(PORTS, [ANA, REV.lower()])
        self.assertEqual(len(specs), 12)  # per port: permit and block on both connect layers, block on both accept
        for port in PORTS:
            mine = [s for s in specs if s.port == port]
            permits = [s for s in mine if s.action == "permit"]
            self.assertEqual({s.layer for s in permits}, {"connect-v4", "connect-v6"})
            self.assertTrue(all(s.users == {ANA, REV} for s in permits))
            blocks = [s for s in mine if s.action == "block"]
            self.assertEqual({s.layer for s in blocks}, set(wfp.LAYERS))
            self.assertTrue(all(s.users is None for s in blocks))
            for s in blocks:
                accept = s.layer.startswith("accept")
                self.assertEqual((s.local_port, s.not_loopback), (accept, accept))
            self.assertTrue(all(s.persistent for s in mine))
            self.assertGreater(permits[0].weight, max(b.weight for b in blocks))

    def test_no_allowed_account_means_no_permit(self):
        self.assertEqual({s.action for s in wfp.expected(PORTS, [])}, {"block"})

    def test_keys_are_fixed_and_distinct(self):
        a, b = wfp.expected(PORTS, [ANA]), wfp.expected(PORTS, [REV])
        self.assertEqual([s.key for s in a], [s.key for s in b])
        self.assertEqual(len({s.key for s in a}), len(a))

    def test_weight_is_not_compared(self):
        s = wfp.expected(PORTS, [ANA])[0]
        self.assertEqual(s, replace(s, weight=1))
        self.assertNotEqual(s, replace(s, persistent=False))

    def test_program_blocks(self):
        app = "\\device\\harddiskvolume9\\made\\up\\tool.exe"
        a, b = wfp.program_blocks(ANA.lower(), app), wfp.program_blocks(REV, app)
        self.assertEqual([s.layer for s in a], ["accept-v4", "accept-v6"])
        for s in a:
            self.assertEqual((s.action, s.port, s.users, s.app, s.loopback), ("block", None, {ANA}, app, True))
            self.assertFalse(s.not_loopback)
            self.assertEqual(s.text(), f"block {s.layer}: application {app} and user in {{{ANA}}} and loopback")
            self.assertEqual(s.target, f"program of {ANA}")
        keys = [s.key for s in a + b + wfp.expected(PORTS, [ANA])]
        self.assertEqual(len(set(keys)), len(keys))
        self.assertEqual([s.key for s in a], [s.key for s in wfp.program_blocks(ANA, app + "x")])

    def test_user_sddl(self):
        sddl = wfp.user_sddl([REV, ANA])
        self.assertEqual(sddl, f"O:SYG:SYD:(A;;CC;;;{ANA})(A;;CC;;;{REV})")
        self.assertEqual(wfp.users_from_sddl(sddl), {ANA, REV})
        odd = wfp.users_from_sddl(f"D:(A;;CC;;;{ANA})(D;;CC;;;{REV})(A;;CCDC;;;WD)")
        self.assertIn(ANA, odd)
        self.assertEqual(len([u for u in odd if u.startswith("?")]), 2)
        self.assertEqual(wfp.users_from_sddl("unreadable"), frozenset())


@WINDOWS
class Native(unittest.TestCase):
    def test_app_id(self):
        app = wfp.app_id(sys.executable)
        self.assertTrue(app.startswith("\\device\\"), app)
        self.assertTrue(app.endswith(Path(sys.executable).name.lower()), app)
        self.assertEqual(app, app.lower())
        self.assertIsNone(wfp.app_id(Path(sys.executable).parent / "no-such-dir" / "x.exe"))

    def test_round_trip(self):
        for s in wfp.expected(PORTS, [ANA, REV]) + wfp.program_blocks(ANA, "\\device\\harddiskvolume9\\t.exe"):
            f, _keep = wfp.native_filter(s)
            back = wfp.spec_of(f)
            self.assertEqual(back, s)
            self.assertEqual(back.weight, s.weight)
            self.assertEqual(f.filterKey.uuid(), s.key)
            self.assertEqual(f.subLayerKey.uuid(), wfp.SUBLAYER_KEY)
            self.assertEqual(f.providerKey.contents.uuid(), wfp.PROVIDER_KEY)

    def test_layout(self):
        self.assertEqual(ctypes.sizeof(wfp.FWP_VALUE0), 16 if ctypes.sizeof(ctypes.c_void_p) == 8 else 8)
        if ctypes.sizeof(ctypes.c_void_p) == 8:
            self.assertEqual(ctypes.sizeof(wfp.FWPM_FILTER0), 200)
            self.assertEqual(ctypes.sizeof(wfp.FWPM_FILTER_CONDITION0), 40)

    def test_unknown_condition_is_kept(self):
        s = wfp.expected(PORTS, [])[0]
        f, _keep = wfp.native_filter(s)
        extra = (wfp.FWPM_FILTER_CONDITION0 * 2)(f.filterCondition[0], wfp.FWPM_FILTER_CONDITION0())
        extra[1].fieldKey = wfp.GUID.of(uuid.UUID(int=1))
        f.numFilterConditions, f.filterCondition = 2, ctypes.cast(extra, ctypes.POINTER(wfp.FWPM_FILTER_CONDITION0))
        back = wfp.spec_of(f)
        self.assertNotEqual(back, s)
        self.assertEqual(len(back.other), 1)

    def test_descriptor(self):
        data = wfp.sd_from_sddl(wfp.user_sddl([ANA]))
        self.assertEqual(wfp.users_from_sddl(wfp.sddl_from_sd(data)), {ANA})


OTHER = uuid.UUID("00000000-0000-4000-8000-000000000001")  # a sublayer that is not Coney's
LOW = uuid.UUID("00000000-0000-4000-8000-000000000002")


class FakeEngine:
    """Coney's filters by key, with transactions."""

    def __init__(self, filters=(), fail_on=None):
        self.filters_ = {s.key: s for s in filters}
        self.sublayer, self.provider = (wfp.SUBLAYER_WEIGHT, True) if filters else (None, False)
        self.log, self.fail_on, self.saved = [], fail_on, None

    def begin(self):
        self.saved = (dict(self.filters_), self.sublayer, self.provider)
        self.log.append("begin")

    def commit(self):
        self.log.append("commit")

    def abort(self):
        self.filters_, self.sublayer, self.provider = self.saved
        self.log.append("abort")

    def all_filters(self):
        return list(self.filters_.items())

    def delete_filter(self, key):
        self.log.append("delete")
        del self.filters_[key]

    def delete_sublayer(self):
        self.sublayer = None

    def delete_provider(self):
        self.provider = False

    def add_provider(self):
        self.provider = True

    def add_sublayer(self):
        self.sublayer = wfp.SUBLAYER_WEIGHT

    def add_filter(self, spec):
        if self.fail_on is not None and spec.port == self.fail_on:
            raise wfp.WfpError(0x80320009, "adding a filter")
        self.log.append("add")
        self.filters_[spec.key] = spec

    def sublayer_weight(self):
        return self.sublayer

    def sublayers(self):
        mine = [(wfp.SUBLAYER_KEY, wfp.NAME, self.sublayer)] if self.sublayer is not None else []
        return mine + list(getattr(self, "others", []))

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        pass


class Operations(unittest.TestCase):
    def test_apply_replaces_in_one_transaction(self):
        old = [wfp.FilterSpec("connect-v4", "permit", 9999)]
        e = FakeEngine(old)
        removed, added = wfp.apply(e, wfp.expected(PORTS, [ANA]))
        self.assertEqual(removed, old)
        self.assertEqual(len(added), 12)
        self.assertEqual(e.log[0], "begin")
        self.assertEqual(e.log[-1], "commit")
        self.assertEqual(e.log.count("delete"), 1)
        self.assertEqual(wfp.read_state(e), (wfp.SUBLAYER_WEIGHT, added))
        # again: the same state
        removed, again = wfp.apply(e, wfp.expected(PORTS, [ANA]))
        self.assertEqual((removed, again), (added, added))

    def test_failed_apply_changes_nothing(self):
        before = wfp.expected([4000], [ANA])
        e = FakeEngine(before, fail_on=PORTS[1])
        with self.assertRaises(wfp.WfpError):
            wfp.apply(e, wfp.expected(PORTS, [ANA]))
        self.assertEqual(e.log[-1], "abort")
        self.assertNotIn("commit", e.log)
        self.assertEqual(wfp.read_state(e), (wfp.SUBLAYER_WEIGHT, before))

    def test_remove(self):
        e = FakeEngine(wfp.expected(PORTS, [ANA]))
        self.assertEqual(len(wfp.remove(e)), 12)
        self.assertEqual(wfp.read_state(e), (None, []))
        self.assertEqual(wfp.remove(e), [])


class FakeDll:
    """fwpuclnt.dll in miniature: what the native layer passes in, and filters handed back through out-pointers."""

    def __init__(self, rc=None):
        self.rc = rc or {}  # function name -> return code
        self.added, self.keep, self.calls = {}, [], []
        self.layer = None

    def __getattr__(self, name):
        def call(*args):
            self.calls.append(name)
            rc = self.rc.get(name, 0)
            handler = type(self).__dict__.get("_" + name)
            if not rc and handler:
                handler(self, *args)
            return rc

        return call

    def _FwpmEngineOpen0(self, server, authn, ident, session, handle):
        handle._obj.value = 7

    def _FwpmFilterAdd0(self, h, f, sd, fid):
        spec = wfp.spec_of(f._obj)
        self.added[spec.key] = spec
        fid._obj.value = len(self.added)

    def _FwpmFilterDeleteByKey0(self, h, key):
        del self.added[key._obj.uuid()]

    def _FwpmFilterCreateEnumHandle0(self, h, template, eh):
        self.layer = wfp.LAYER_NAMES[template._obj.layerKey.uuid()]
        assert template._obj.providerKey.contents.uuid() == wfp.PROVIDER_KEY
        eh._obj.value = 9

    def _FwpmFilterEnum0(self, h, eh, n, entries, count):
        natives = [wfp.native_filter(s) for s in self.added.values() if s.layer == self.layer]
        self.keep.append(natives)
        arr = (ctypes.POINTER(wfp.FWPM_FILTER0) * max(1, len(natives)))(*[ctypes.pointer(f) for f, _ in natives])
        self.keep.append(arr)
        address = ctypes.c_void_p(ctypes.addressof(arr))
        ctypes.memmove(ctypes.addressof(entries._obj), ctypes.addressof(address), ctypes.sizeof(address))
        count._obj.value = len(natives)


@WINDOWS
class NativeEngine(unittest.TestCase):
    def test_apply_and_read_back(self):
        dll = FakeDll()
        with wfp.Engine(dll) as e:
            wfp.apply(e, wfp.expected(PORTS, [ANA]))
            self.assertEqual(sorted(dll.added.values(), key=str), sorted(wfp.expected(PORTS, [ANA]), key=str))
            got = [s for _, s in e.all_filters()]
        self.assertEqual(sorted(got, key=str), sorted(wfp.expected(PORTS, [ANA]), key=str))
        self.assertEqual(dll.calls[:2], ["FwpmEngineOpen0", "FwpmTransactionBegin0"])
        self.assertIn("FwpmTransactionCommit0", dll.calls)
        self.assertEqual(dll.calls[-1], "FwpmEngineClose0")

    def test_errors(self):
        with self.assertRaises(wfp.WfpError) as cm:
            wfp.Engine(FakeDll({"FwpmEngineOpen0": 5}))
        self.assertTrue(cm.exception.denied)
        self.assertIn("run it elevated", str(cm.exception))
        dll = FakeDll({"FwpmFilterAdd0": 0x80320009})
        with wfp.Engine(dll) as e, self.assertRaises(wfp.WfpError):
            wfp.apply(e, wfp.expected(PORTS, [ANA]))
        self.assertIn("FwpmTransactionAbort0", dll.calls)
        self.assertNotIn("FwpmTransactionCommit0", dll.calls)

    def test_missing_objects_are_not_errors(self):
        dll = FakeDll(
            {
                "FwpmSubLayerGetByKey0": wfp.FWP_E_SUBLAYER_NOT_FOUND,
                "FwpmSubLayerDeleteByKey0": wfp.FWP_E_SUBLAYER_NOT_FOUND,
                "FwpmProviderDeleteByKey0": wfp.FWP_E_PROVIDER_NOT_FOUND,
            }
        )
        with wfp.Engine(dll) as e:
            self.assertEqual(wfp.remove(e), [])
            self.assertIsNone(e.sublayer_weight())

    def test_real_engine_opens(self):
        # Opening needs no rights; listing filters may be refused without elevation, which is reported, not raised
        # as anything but WfpError.
        with wfp.Engine() as e:
            try:
                e.all_filters()
            except wfp.WfpError as err:
                self.assertTrue(err.denied, err)


class WallChecks(unittest.TestCase):
    def setUp(self):
        self.ws = mock.Mock()
        self.ws.analyst_side_accounts.return_value = {"zt-analyst", "zt-reviewer"}
        self.ws.ports = [{"port": p} for p in PORTS]
        self.sids = {"zt-analyst": ANA, "zt-reviewer": REV}
        self.want = wfp.expected(PORTS, [ANA, REV])

    def run_checks(self, filters, weight=wfp.SUBLAYER_WEIGHT, others=(), risky=()):
        checks = zones.wall_filter_checks(
            self.ws,
            self.sids.__getitem__,
            lambda: (weight, filters),
            lambda: [(OTHER, n, w) for n, w in others],
            lambda w: list(risky),
        )
        return [c for c in checks if not c.ok], checks

    def test_clean(self):
        bad, checks = self.run_checks(list(self.want))
        self.assertEqual(bad, [])
        self.assertEqual(len(checks), 13)

    def test_missing_block_is_a_breach(self):
        bad, _ = self.run_checks([s for s in self.want if not (s.action == "block" and s.port == PORTS[0])])
        self.assertEqual(len(bad), 4)
        self.assertTrue(all(c.widens and c.got == "missing" for c in bad))

    def test_missing_permit_fails_without_breach(self):
        bad, _ = self.run_checks([s for s in self.want if s.action == "block"])
        self.assertEqual(len(bad), 4)
        self.assertFalse(any(c.widens for c in bad))

    def test_changed_and_extra_filters(self):
        have = list(self.want)
        i = next(i for i, s in enumerate(have) if s.action == "permit")
        have[i] = replace(have[i], users=frozenset({ANA}))  # narrower: shuts a reviewer out, lets no one in
        j = next(j for j, s in enumerate(have) if s.action == "permit" and j != i)
        have[j] = replace(have[j], users=frozenset({ANA, "S-1-5-21-7-7-7-3000"}))  # wider: lets another in
        have.append(wfp.FilterSpec("connect-v4", "permit", 4103))
        have.append(wfp.FilterSpec("connect-v4", "block", 4104))
        bad, _ = self.run_checks(have)
        got = sorted((c.want == "absent", c.widens) for c in bad)
        self.assertEqual(got, [(False, False), (False, True), (True, False), (True, True)])
        k = next(k for k, s in enumerate(self.want) if s.action == "permit")
        bad, _ = self.run_checks([replace(s, users=None) if n == k else s for n, s in enumerate(self.want)])
        self.assertEqual([c.widens for c in bad], [True])

    def test_changed_block_is_a_breach(self):
        have = list(self.want)
        i = next(i for i, s in enumerate(have) if s.layer == "accept-v6")
        have[i] = replace(have[i], not_loopback=False, other=("condition x",))
        (c,) = self.run_checks(have)[0]
        self.assertTrue(c.widens)
        self.assertIn("condition x", c.got)

    def test_program_filters(self):
        programs = [("zt-analyst", ANA, "C:\\made\\up\\tool.exe", wfp.program_blocks(ANA, "\\device\\x\\tool.exe"))]
        run = lambda have: zones.wall_filter_checks(
            self.ws, self.sids.__getitem__, lambda: (wfp.SUBLAYER_WEIGHT, have), programs=programs
        )
        checks = run(self.want + programs[0][3])
        self.assertTrue(all(c.ok for c in checks), [c for c in checks if not c.ok])
        self.assertEqual(sum(c.path.endswith(f"program of {ANA}") for c in checks), 2)
        bad = [c for c in run(self.want + programs[0][3][:1]) if not c.ok]
        self.assertEqual([(c.got, c.widens) for c in bad], [("missing", True)])
        changed = replace(programs[0][3][1], app="\\device\\x\\other.exe")
        bad = [c for c in run(self.want + [programs[0][3][0], changed]) if not c.ok]
        self.assertEqual(len(bad), 1)
        self.assertTrue(bad[0].widens)
        self.assertIn("other.exe", bad[0].got)

    def test_not_persistent_fails_without_breach(self):
        have = [replace(s, persistent=False) if s.action == "block" else s for s in self.want]
        bad, _ = self.run_checks(have)
        self.assertEqual(len(bad), 8)
        self.assertFalse(any(c.widens for c in bad))

    def test_sublayer(self):
        # the engine may store less than the weight asked for: what counts is that nothing weighs more
        bad, checks = self.run_checks(list(self.want), weight=0xFFFE, others=[("Windows", 0x8000)])
        self.assertEqual(bad, [])
        # a sublayer above Coney's is fine unless one of its filters could overrule the wall
        bad, checks = self.run_checks(list(self.want), weight=0xFFFE, others=[("a VPN", 0xFFFF)])
        self.assertEqual(bad, [])
        self.assertIn("above it: a VPN 0xffff; none of their filters could overrule it", checks[0].got)
        risky = [("open door", "a VPN", "connect-v4", PORTS[0])]
        bad, _ = self.run_checks(list(self.want), weight=0xFFFE, others=[("a VPN", 0xFFFF)], risky=risky)
        self.assertEqual([(c.what, c.widens) for c in bad], [("port wall", True)])
        self.assertIn(f"could overrule it: open door in a VPN (connect-v4, port {PORTS[0]})", bad[0].got)
        bad, checks = self.run_checks(list(self.want), weight=0x100, others=[("twin", 0x100)])
        self.assertEqual(bad, [])
        self.assertIn("level with it: twin 0x100", checks[0].got)
        bad, _ = self.run_checks([], weight=None)
        self.assertEqual(len(bad), 13)
        self.assertTrue(all(c.widens for c in bad if c.what == "port wall"))

    def test_could_override(self):
        hard, soft, callout = (wfp.FWP_ACTION_PERMIT, 0x8), (wfp.FWP_ACTION_PERMIT, 0), (0x5003, 0)
        block = (wfp.FWP_ACTION_BLOCK, 0x8)
        eq = lambda port: (wfp.FWP_MATCH_EQUAL, wfp.FWP_UINT16, port)
        ne = lambda port: (wfp.FWP_MATCH_NOT_EQUAL, wfp.FWP_UINT16, port)
        for (action, flags), conds, want in [
            (hard, [], True),  # a hard permit on every port
            (soft, [], False),  # a soft permit cannot overrule a block in another sublayer
            (block, [], False),
            (callout, [], True),  # a callout may clear the action right itself
            ((0x6004, 0), [], False),  # an inspection callout decides nothing
            (hard, [eq(9999)], False),
            (hard, [eq(9999), eq(PORTS[0])], True),  # alternatives on one field
            (hard, [ne(PORTS[0])], False),
            (hard, [ne(9999)], True),
            (hard, [(5, 0x102, None)], True),  # a range it does not read is taken to match
        ]:
            self.assertEqual(wfp.could_override(action, flags, conds, PORTS[0]), want, (action, flags, conds))

    def test_overriding_looks_only_at_or_above_coneys_weight(self):
        class Eng:
            def sublayers(self):
                return [(wfp.SUBLAYER_KEY, wfp.NAME, 0xFFFE), (OTHER, "high", 0xFFFF), (LOW, "low", 0x10)]

            def layer_filters(self, layer):
                if layer != "connect-v4":
                    return []
                return [
                    ("hard high", OTHER, wfp.FWP_ACTION_PERMIT, 0x8, {}),
                    ("hard low", LOW, wfp.FWP_ACTION_PERMIT, 0x8, {}),
                    ("own", wfp.SUBLAYER_KEY, wfp.FWP_ACTION_PERMIT, 0x8, {}),
                    ("other port", OTHER, wfp.FWP_ACTION_PERMIT, 0x8, {wfp.COND_REMOTE_PORT: [(0, 2, 1)]}),
                ]

        got = wfp.overriding(Eng(), 0xFFFE, [PORTS[0]])
        self.assertEqual(got, [("hard high", "high", "connect-v4", PORTS[0], [], "any")])

    def test_a_filter_limited_to_another_interface(self):
        ethernet, loopback = (6 << 48) | (5 << 24), (24 << 48) | (1 << 24)
        self.assertTrue(wfp.off_loopback([(wfp.FWP_MATCH_EQUAL, ethernet)]))
        self.assertFalse(wfp.off_loopback([(wfp.FWP_MATCH_EQUAL, loopback)]))
        self.assertFalse(wfp.off_loopback([(wfp.FWP_MATCH_EQUAL, ethernet), (wfp.FWP_MATCH_EQUAL, loopback)]))
        self.assertFalse(wfp.off_loopback([(wfp.FWP_MATCH_NOT_EQUAL, ethernet)]))
        self.assertFalse(wfp.off_loopback([]))
        risky = [("vpn allow", "a VPN", "accept-v4", PORTS[0], ["local interface == 5"], "off-loopback")]
        bad, checks = self.run_checks(list(self.want), weight=0xFFFE, others=[("a VPN", 0xFFFF)], risky=risky)
        self.assertEqual(bad, [])
        self.assertIn("limited to a non-loopback interface", checks[0].got)

    def test_outranking_ignores_coneys_own_sublayer(self):
        subs = [(wfp.SUBLAYER_KEY, wfp.NAME, 0xFFFE), (OTHER, "x", 0xFFFF), (OTHER, "y", 0xFFFE), (OTHER, "z", 1)]
        self.assertEqual(wfp.outranking(0xFFFE, subs), ([("x", 0xFFFF)], [("y", 0xFFFE)]))

    def test_unreadable(self):
        def denied():
            raise wfp.WfpError(5, "listing filters")

        (c,) = zones.wall_filter_checks(self.ws, self.sids.__getitem__, denied)
        self.assertFalse(c.ok)
        self.assertIn("run it elevated", c.got)

        def no_sid(name):
            raise OSError(None, "does not resolve")

        (c,) = zones.wall_filter_checks(self.ws, no_sid, denied)
        self.assertEqual(c.want, "every analyst-side account resolves")


class Cli(unittest.TestCase):
    def ports(self, *argv, elevated=True, engine=None, program=None):
        out, err = io.StringIO(), io.StringIO()
        ws = mock.Mock()
        ws.analyst_side_accounts.return_value = {"zt-analyst"}
        ws.ports = [{"port": p} for p in PORTS]
        ws.claude_program.return_value = program
        with (
            redirect_stdout(out),
            redirect_stderr(err),
            mock.patch.object(zones, "is_elevated", return_value=elevated),
            mock.patch.object(zones, "sid_of", return_value=ANA),
            mock.patch("coney_tools.workspace.load", return_value=ws),
            mock.patch.object(wfp, "Engine", return_value=engine or FakeEngine()),
            mock.patch.object(wfp, "app_id", lambda path: f"\\device\\v\\{path[3:].lower()}"),
        ):
            code = main(["zones", "ports", *argv])
        return code, out.getvalue(), err.getvalue()

    def test_needs_elevation(self):
        for act in ("--apply", "--remove"):
            code, out, err = self.ports(act, elevated=False)
            self.assertEqual((code, out), (2, ""))
            self.assertIn("elevat", err)

    def test_apply_remove_list(self):
        e = FakeEngine()
        code, out, _ = self.ports("--apply", engine=e)
        self.assertEqual(code, 0)
        self.assertIn("added 12 filters", out)
        self.assertIn(f"zt-analyst ({ANA})", out)
        code, out, _ = self.ports("--list", elevated=False, engine=e)
        self.assertEqual(code, 0)
        self.assertIn("12 filters", out)
        code, out, _ = self.ports("--remove", engine=e)
        self.assertEqual(code, 0)
        self.assertIn("removed 12 filters", out)

    def test_apply_walls_the_program(self):
        e = FakeEngine()
        code, out, _ = self.ports("--apply", engine=e, program="C:\\Homes\\zt-analyst\\Tool.exe")
        self.assertEqual(code, 0)
        self.assertIn("added 14 filters", out)
        self.assertIn("programs taking no loopback connection: zt-analyst (C:\\Homes\\zt-analyst\\Tool.exe)", out)
        self.assertIn(
            f"block accept-v4: application \\device\\v\\homes\\zt-analyst\\tool.exe and user in {{{ANA}}}", out
        )

    def test_list_names_what_could_overrule(self):
        e = FakeEngine()
        self.ports("--apply", engine=e)
        e.others = [(OTHER, "a VPN", 0xFFFF)]
        e.layer_filters = lambda layer: (
            [("let it in", OTHER, wfp.FWP_ACTION_PERMIT, 0x8, {}, ["local interface == 7"])]
            if layer == "connect-v4"
            else []
        )
        code, out, _ = self.ports("--list", engine=e)
        self.assertEqual(code, 0)
        self.assertIn("other sublayer level with it: a VPN 0xffff", out)
        self.assertIn(f"COULD OVERRULE the wall: filter let it in in a VPN (connect-v4, port {PORTS[0]})", out)
        self.assertIn("      condition: local interface == 7", out)

    def test_describe_condition(self):
        v = wfp.FWP_VALUE0()
        v.type, v.uint32 = wfp.FWP_UINT32, 0x1A000001
        key = next(k for k, n in wfp.COND_NAMES.items() if n == "remote address")
        self.assertEqual(wfp.describe_condition(key, wfp.FWP_MATCH_EQUAL, v), "remote address == 26.0.0.1")
        pair = (ctypes.c_uint32 * 2)(0x1A000000, 0xFF000000)
        v.type, v.ptr = 0x100, ctypes.addressof(pair)
        self.assertEqual(wfp.describe_condition(key, 0, v), "remote address == 26.0.0.0/255.0.0.0")
        v.type = 99
        self.assertIn("(type 99)", wfp.describe_condition(OTHER, 10, v))

    def test_list_refused(self):
        class Denied(FakeEngine):
            def all_filters(self):
                raise wfp.WfpError(5, "listing filters")

        code, out, err = self.ports("--list", elevated=False, engine=Denied())
        self.assertEqual((code, out), (2, ""))
        self.assertIn("listing filters: access denied: run it elevated", err)

    def test_one_action_required(self):
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as cm:
            main(["zones", "ports"])
        self.assertEqual(cm.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
