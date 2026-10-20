// The band above the prompt: one read-only row of counts, where the counts live (the pane's tabs carry none). What it
// says first is what a glance needs: how much waits for the coordinator, a refused wake, what runs, and CI's room. At
// a narrow width it drops its least needed parts first, so it stays one row.
import type { CoordStatus, Watch } from '../types'
import { clock, waiting, wakeable } from './data'
import { C } from './kit'
import type { El } from './kit'

type Part = { text: string; rank: number; color?: string; bold?: boolean; dim?: boolean }

/** Slots held of all slots across both build lanes. */
export function slotsHeld(s: CoordStatus): [number, number] {
  const lanes = Object.values(s.builds.lanes ?? {})
  const slots = lanes.flatMap(l => l.slots)
  return [slots.filter(x => x.claim && !x.stale).length, slots.length]
}

/** The band's parts, each with its rank: the highest rank goes first when the row is too narrow. */
export function parts(s: CoordStatus, w: Watch): Part[] {
  const mine = wakeable(s).length
  const owned = waiting(s).length - mine
  const agents = w.agents ?? 0
  const [held, slots] = slotsHeld(s)
  const copies = s.pcsx2.copies ?? []
  const busy = copies.filter(c => c.state !== 'free').length
  const queued = Object.values(s.builds.lanes ?? {}).reduce((n, l) => n + l.queue.filter(t => !t.stale).length, 0)
  const out: Part[] = [
    { text: 'Coney coordinator', rank: 0, bold: true },
    { text: `${mine} waiting`, rank: 0, color: mine ? C.warn : undefined, bold: mine > 0, dim: !mine },
    { text: `${agents} agent${agents === 1 ? '' : 's'}`, rank: 1, color: agents ? C.info : undefined, dim: !agents },
  ]
  if (owned) out.push({ text: `${owned} PR (Tether)`, rank: 3, dim: true })
  if (s.ci && !s.ci.error) {
    out.push({ text: `CI ${s.ci.queued ?? 0} queued, ${s.ci.in_progress ?? 0} running`, rank: 2, color: C.info })
  }
  if (!s.builds.error) {
    out.push({ text: `builds ${held}/${slots}${queued ? ` +${queued} queued` : ''}`, rank: 2, dim: !held })
  }
  if (!s.pcsx2.error) out.push({ text: `PCSX2 ${busy}/${copies.length}`, rank: 3, dim: !busy })
  out.push({ text: `read ${clock(w.readAt)}`, rank: 4, dim: true })
  if (w.githubFailure) out.splice(2, 0, { text: 'GitHub not read', rank: 1, color: C.warn })
  if (w.refused) out.splice(1, 0, { text: 'wake refused', rank: 0, color: C.warn, bold: true })
  return out
}

const SEP = ' │ '

/** The parts that fit in width cells, dropping the highest rank first. */
export function fitted(all: Part[], width: number): Part[] {
  let kept = [...all]
  const len = (ps: Part[]) => ps.reduce((n, p) => n + p.text.length, 0) + SEP.length * Math.max(0, ps.length - 1)
  while (len(kept) > width && kept.some(p => p.rank > 0)) {
    const worst = Math.max(...kept.map(p => p.rank))
    const at = kept.map(p => p.rank).lastIndexOf(worst)
    kept = kept.filter((_, i) => i !== at)
  }
  return kept
}

export function Band(el: El, ps: Part[]) {
  const { Box, Text } = el
  return (
    <Box flexDirection="row">
      {ps.map((p, i) => (
        <Text key={`band-${i}`} color={p.color} bold={p.bold} dimColor={p.dim}>
          {i ? <Text dimColor>{SEP}</Text> : null}
          {p.text}
        </Text>
      ))}
    </Box>
  )
}

export function drawBand(el: El, d: { s: CoordStatus | null; w: Watch; width: number }) {
  const { Text } = el
  if (!d.s) {
    return d.w.failure ? <Text color={C.bad}>Coney: coord status cannot be read ({d.w.failure})</Text> : null
  }
  return Band(el, fitted(parts(d.s, d.w), d.width))
}
