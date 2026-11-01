// SPDX-License-Identifier: GPL-3.0-or-later
// Written by python/tests/test_conclave_fixture.py from the Python tests' synthetic units: do not edit.
// Run that test with CONEY_UPDATE_FIXTURES=1 to write it again after a change to the snapshot.
export default {
 "schema": 3,
 "time": "2026-10-23T14:00:00Z",
 "checkout": "/work/repos/coney",
 "github": true,
 "config": {
  "error": null,
  "roles": [
   "coordinator",
   "reviewer",
   "engineer",
   "tooling"
  ],
  "helpers": [
   "Explore",
   "Plan",
   "fork"
  ],
  "require_role": false,
  "source": ".claude/hooks/hooks.toml",
  "claude_code": "2.1.309",
  "intervals": {
   "local": 15,
   "github": 60,
   "github_idle": 300
  },
  "pane_min_columns": 30,
  "pane_compact_columns": 60,
  "pane_wide_columns": 110,
  "band_narrow_columns": 100,
  "stale_after_reads": 3,
  "merged_visible_minutes": 10,
  "claim_red_hours": 6,
  "watch": {
   "roles": [
    "coordinator"
   ],
   "preamble": "Coordinator: new work. Handle it or say why it can wait:",
   "github_seconds": 120,
   "wake_gap_seconds": 60,
   "defer_typing_minutes": 10,
   "resend_minutes": 2,
   "stop_max_blocks": 3,
   "stop_reset_minutes": 30,
   "stop_preamble": "Open pull requests are in trouble and nobody holds the fix. Deal with each, or record who does:"
  }
 },
 "builds": {
  "error": null,
  "lanes": {
   "main": {
    "slots": [
     {
      "slot": 1,
      "stale": false,
      "claim": {
       "slot": 1,
       "agent": "engineer-2",
       "worktree": "/work/worktrees/fix-frame-812",
       "preset": "dev",
       "step": "build",
       "pid": 4,
       "started": "2026-10-23T13:57:00Z",
       "lane": "main"
      }
     }
    ],
    "queue": [
     {
      "position": 1,
      "stale": false,
      "name": "t1",
      "agent": "x",
      "worktree": "fix-new",
      "preset": "dev",
      "pid": 5,
      "priority": false,
      "time": 0.0
     }
    ]
   },
   "check": {
    "slots": [
     {
      "slot": 1,
      "stale": false,
      "claim": {
       "slot": 1,
       "agent": "engineer-2",
       "worktree": "/work/worktrees/fix-frame-812",
       "preset": "pytest",
       "step": "pytest",
       "pid": 4,
       "started": "2026-10-23T13:10:00Z",
       "lane": "check"
      }
     },
     {
      "slot": 2,
      "stale": false,
      "claim": null
     }
    ],
    "queue": [
     {
      "position": 1,
      "stale": false,
      "name": "t2",
      "agent": "x",
      "worktree": "fix-new",
      "preset": "pytest",
      "pid": 6,
      "priority": false,
      "time": 0.0
     }
    ]
   }
  }
 },
 "units": {
  "error": null,
  "units": [
   {
    "task": "add-task-start",
    "error": null,
    "issues": [
     799
    ],
    "kind": "fix",
    "claimant": "example-maintainer-who-owns-the-project",
    "claimed_at": "2026-10-21T14:00:00Z",
    "scope": [
     "python/src/coney_tools/task_cli.py"
    ],
    "goal": "Fix the uncuff steer.",
    "branch": null,
    "worktree": null,
    "busy": false,
    "stage": "finished",
    "stage_since": "2026-10-23T13:00:00Z",
    "failed": null,
    "steps": [
     {
      "step": "assigned",
      "state": "pass",
      "time": "2026-10-23T12:52:00Z"
     },
     {
      "step": "claimed",
      "state": "pass",
      "time": "2026-10-23T12:53:00Z"
     },
     {
      "step": "studied",
      "state": "pass",
      "time": "2026-10-23T12:54:00Z"
     },
     {
      "step": "fails-first",
      "state": "pass",
      "time": "2026-10-23T12:55:00Z"
     },
     {
      "step": "compared",
      "state": "pass",
      "time": "2026-10-23T12:56:00Z"
     },
     {
      "step": "checked",
      "state": "pass",
      "time": "2026-10-23T12:57:00Z"
     },
     {
      "step": "reviewed",
      "state": "pass",
      "time": "2026-10-23T12:58:00Z"
     },
     {
      "step": "pr-opened",
      "state": "pass",
      "time": "2026-10-23T12:59:00Z"
     },
     {
      "step": "finished",
      "state": "pass",
      "time": "2026-10-23T13:00:00Z"
     }
    ],
    "agent": {
     "id": "engineer-2",
     "type": "engineer"
    },
    "stamp_problems": 0,
    "build": null,
    "last_build": null,
    "check": null,
    "overlaps": [],
    "pr": {
     "number": 799,
     "state": "merged",
     "ci": null,
     "causes": [],
     "merged_at": "2026-10-23T13:56:00Z"
    }
   },
   {
    "task": "fix-frame-812",
    "error": null,
    "issues": [
     741
    ],
    "kind": "fix",
    "claimant": "example-maintainer-who-owns-the-project",
    "claimed_at": "2026-10-21T14:00:00Z",
    "scope": [
     "src/platform/frame.cpp",
     "src/ai/"
    ],
    "goal": "Fix the uncuff steer.",
    "branch": "fix-frame-812",
    "worktree": "/work/worktrees/fix-frame-812",
    "busy": false,
    "stage": "compared",
    "stage_since": "2026-10-23T09:00:00Z",
    "failed": "compared",
    "steps": [
     {
      "step": "assigned",
      "state": "pass",
      "time": "2026-10-23T08:56:00Z"
     },
     {
      "step": "claimed",
      "state": "pass",
      "time": "2026-10-23T08:57:00Z"
     },
     {
      "step": "studied",
      "state": "pass",
      "time": "2026-10-23T08:58:00Z"
     },
     {
      "step": "fails-first",
      "state": "pass",
      "time": "2026-10-23T08:59:00Z"
     },
     {
      "step": "compared",
      "state": "fail",
      "time": "2026-10-23T09:00:00Z"
     },
     {
      "step": "checked",
      "state": "missing",
      "time": null
     },
     {
      "step": "reviewed",
      "state": "missing",
      "time": null
     },
     {
      "step": "pr-opened",
      "state": "missing",
      "time": null
     },
     {
      "step": "finished",
      "state": "missing",
      "time": null
     }
    ],
    "agent": {
     "id": "engineer-2",
     "type": "engineer"
    },
    "stamp_problems": 0,
    "build": {
     "state": "running",
     "preset": "dev",
     "step": "build",
     "slot": 1,
     "lane": "main",
     "since": "2026-10-23T13:57:00Z"
    },
    "last_build": null,
    "check": {
     "task": "fix-frame-812",
     "pid": 100,
     "started": "2026-10-23T13:10:00Z",
     "check": "pytest",
     "kind": "pytest",
     "index": 6,
     "total": 8,
     "since": "2026-10-23T13:10:00Z",
     "updated": "2026-10-23T14:00:00Z",
     "state": "running",
     "stored": "running",
     "elapsed": 3000,
     "age": 0,
     "done": {
      "done": 64,
      "of": 100,
      "unit": "%",
      "failed": null
     },
     "fraction": 0.64,
     "moved": "2026-10-23T13:20:00Z",
     "idle": 2400,
     "stalled": true,
     "queue": null,
     "short": "pytest 64% 50m stalled",
     "text": "check 6 of 8, pytest: running 50 min, 64% done; stalled: no progress for 40 min"
    },
    "overlaps": [
     {
      "with": "fix-new",
      "kind": "unit",
      "scope": [
       "src/ai/"
      ]
     }
    ],
    "pr": {
     "number": 812,
     "state": "open",
     "ci": "fail",
     "causes": [
      "checks-failed"
     ],
     "merged_at": null
    }
   },
   {
    "task": "fix-new",
    "error": null,
    "issues": [
     803
    ],
    "kind": "fix",
    "claimant": "example-maintainer-who-owns-the-project",
    "claimed_at": "2026-10-21T14:00:00Z",
    "scope": [
     "src/ai/ai_goal.cpp"
    ],
    "goal": "Fix the uncuff steer.",
    "branch": "fix-new",
    "worktree": "/work/worktrees/fix-new",
    "busy": false,
    "stage": "assigned",
    "stage_since": "2026-10-21T14:00:00Z",
    "failed": null,
    "steps": [
     {
      "step": "assigned",
      "state": "missing",
      "time": null
     },
     {
      "step": "claimed",
      "state": "missing",
      "time": null
     },
     {
      "step": "studied",
      "state": "missing",
      "time": null
     },
     {
      "step": "fails-first",
      "state": "missing",
      "time": null
     },
     {
      "step": "compared",
      "state": "missing",
      "time": null
     },
     {
      "step": "checked",
      "state": "missing",
      "time": null
     },
     {
      "step": "reviewed",
      "state": "missing",
      "time": null
     },
     {
      "step": "pr-opened",
      "state": "missing",
      "time": null
     },
     {
      "step": "finished",
      "state": "missing",
      "time": null
     }
    ],
    "agent": null,
    "stamp_problems": 0,
    "build": {
     "state": "waiting",
     "preset": "dev",
     "position": 1,
     "lane": "main"
    },
    "last_build": null,
    "check": {
     "task": "fix-new",
     "pid": 100,
     "started": "2026-10-23T13:56:00Z",
     "check": "pytest",
     "kind": "pytest",
     "index": 6,
     "total": 8,
     "since": "2026-10-23T14:00:00Z",
     "updated": "2026-10-23T14:00:00Z",
     "state": "waiting",
     "stored": "waiting",
     "elapsed": 0,
     "age": 0,
     "done": null,
     "fraction": null,
     "moved": "2026-10-23T13:56:00Z",
     "idle": null,
     "stalled": false,
     "queue": {
      "lane": "check",
      "place": 5,
      "total": 6,
      "minutes": 36.0,
      "held_by": [
       "bob"
      ]
     },
     "short": "queued 5/6 ~36m",
     "text": "check 6 of 8, pytest: waiting for a check slot, place 5 of 6, about 36 min; held by bob"
    },
    "overlaps": [
     {
      "with": "fix-frame-812",
      "kind": "unit",
      "scope": [
       "src/ai/ai_goal.cpp"
      ]
     },
     {
      "with": "#741",
      "kind": "claim",
      "scope": [
       "src/ai/ai_goal.cpp"
      ]
     }
    ],
    "pr": null
   }
  ]
 },
 "machine": {
  "error": null,
  "drive": "C:",
  "free_gb": 61.0,
  "min_free_gb": 30,
  "warn_free_gb": 40,
  "drives": [
   {
    "name": "C:",
    "holds": [
     "the workspace drive"
    ],
    "free_gb": 61.0,
    "min_free_gb": 30,
    "warn_free_gb": 40
   }
  ],
  "problems": []
 },
 "claims": {
  "error": null,
  "account": "example-maintainer-who-owns-the-project",
  "rules": {
   "warn_hours": 48,
   "deadline_hours": 72,
   "enforced": false
  },
  "claims": [
   {
    "issue": 741,
    "title": "gameplay: Fix the uncuff steer",
    "claimant": "example-maintainer-who-owns-the-project",
    "status": "claimed",
    "task": null,
    "scope": [
     "src/ai/"
    ],
    "stage": null,
    "pr": null,
    "claimed_at": "2026-10-23T03:00:00Z",
    "last_activity": "2026-10-23T04:00:00Z",
    "deadline": "2026-10-26T04:00:00Z",
    "waiting": null,
    "warned": false,
    "adopted": false
   },
   {
    "issue": 770,
    "title": null,
    "claimant": "contrib-a",
    "status": "claimed",
    "task": null,
    "scope": [
     "assets/"
    ],
    "stage": null,
    "pr": null,
    "claimed_at": "2026-10-21T11:00:00Z",
    "last_activity": "2026-10-21T12:00:00Z",
    "deadline": "2026-10-24T12:00:00Z",
    "waiting": null,
    "warned": false,
    "adopted": false
   }
  ]
 },
 "prs": {
  "error": null,
  "prs": [
   {
    "number": 812,
    "branch": "fix-frame-812",
    "base": "main",
    "draft": false,
    "mergeable": "MERGEABLE",
    "auto_merge": false,
    "ci": "fail",
    "updated": "2026-10-23T13:00:00Z",
    "head": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    "causes": [
     "checks-failed"
    ]
   }
  ],
  "health": {
   "stuck": [
    {
     "number": 812,
     "branch": "fix-frame-812",
     "cause": "checks-failed",
     "kind": "code",
     "detail": "build-ok (code)",
     "who": "author",
     "checks": [
      "build-ok"
     ]
    }
   ],
   "other": [],
   "notices": []
  },
  "merged": [
   {
    "number": 799,
    "branch": "add-task-start",
    "merged_at": "2026-10-23T13:56:00Z"
   }
  ]
 },
 "issues": {
  "error": null,
  "issues": [
   {
    "number": 741,
    "title": "gameplay: Fix the uncuff steer",
    "status": "S-in-progress",
    "priority": "P-high",
    "assignees": [
     "example-maintainer-who-owns-the-project"
    ]
   },
   {
    "number": 790,
    "title": "core: Fix the crash",
    "status": null,
    "priority": "P-critical",
    "assignees": []
   }
  ]
 },
 "events": {
  "error": null,
  "events": [
   {
    "id": "unit:fix-new:PR.md:cccccccccccc",
    "kind": "unit-ready",
    "state": "open",
    "ref": {
     "pr": null,
     "task": "fix-new",
     "branch": "fix-new"
    },
    "since": "2026-10-23T12:00:00Z",
    "text": "fix-new: PR.md is ready for pr open",
    "do": "coney-tools pr open fix-new",
    "guard": false,
    "handled_by": null
   },
   {
    "id": "pr:812:checks-failed:bbbbbbb:00f533",
    "kind": "check-failed",
    "state": "open",
    "ref": {
     "pr": 812,
     "task": null,
     "branch": "fix-frame-812"
    },
    "since": "2026-10-23T13:00:00Z",
    "text": "#812 fix-frame-812: checks-failed build-ok (code)",
    "do": "fix it or start an agent for it, then record it: coney-tools pr hold 812 --who <agent> (a known wait: no --who, a --note that says why)",
    "guard": true,
    "handled_by": null
   },
   {
    "id": "question:fix-new:QUESTION.md:2026-10-23T13:00:00Z",
    "kind": "question",
    "state": "open",
    "ref": {
     "pr": null,
     "task": "fix-new",
     "branch": "fix-new"
    },
    "since": "2026-10-23T13:00:00Z",
    "text": "fix-new asks: Which step first?",
    "do": "read /work/scratch/fix-new/QUESTION.md, then answer it: write ANSWER.md beside it, or message the agent",
    "guard": false,
    "handled_by": null
   },
   {
    "id": "merged:799",
    "kind": "merged",
    "state": "open",
    "ref": {
     "pr": 799,
     "task": null,
     "branch": "add-task-start"
    },
    "since": "2026-10-23T13:56:00Z",
    "text": "#799 add-task-start merged",
    "do": "bring the issues it affected up to date (issues.md#after-a-merge)",
    "guard": false,
    "handled_by": null
   }
  ],
  "unread": [],
  "holds_problems": []
 }
}
