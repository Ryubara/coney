// The pane's small set of parts: a tab row, a sub-tab row, a section rule, fixed-width columns, the tip row, and the
// colours. Elements come from the surface ($.ui.resolve), so every part takes them as its first argument. Colours are
// the theme's own keys, so the pane reads the same in a light and a dark theme.
import type { EngineInterface, RenderElement } from 'claude-code'

import type { View } from '../types'

export type El = ReturnType<EngineInterface['ui']['resolve']>

/** What a drawing's handlers may do, made where the hooks are (so $ never crosses an import): change the view, put
 * text in the prompt, and on the Files tab list a folder, preview a file or read git's changes. */
export type Act = {
  edit: (fn: (v: View) => View) => Promise<unknown>
  fill: (text: string) => Promise<unknown>
  list: (rel: string) => Promise<unknown>
  preview: (rel: string) => Promise<unknown>
  changes: () => Promise<unknown>
}

/** The theme's colours, by what they mean here. */
export const C = {
  bad: 'error',
  warn: 'warning',
  good: 'success',
  info: 'suggestion',
  think: 'planMode',
  done: 'merged',
} as const

// What each kind of element is for: shown in the tip row at the pane's foot while the pointer rests on one (a hover
// group per kind, so no hook runs) or the keyboard's focus is on one.
export const TIPS: Record<string, string> = {
  'tab-work': 'Work: what waits for you, the open issues and pull requests, and the release queue.',
  'tab-machine': 'Machine: the PCSX2 copies, the build slots and queues, and the tracks.',
  'tab-files': "Files: this session's folder, its git changes, and file previews.",
  'sub-waiting': 'Every id that waits for the coordinator; the ones Tether owns are marked and wake no one here.',
  'sub-issues': 'Open issues by number: status, priority, area and age (read every two minutes through gh).',
  'sub-prs': 'Open pull requests: checks, mergeability, auto-merge and what pr-health says of each.',
  'sub-release': "Each PR*.md in a branch's scratch folder: queued, or the pull request that released it.",
  'sub-pcsx2': 'The PCSX2 copies: free or held, by whom and since when, and whether PCSX2 runs.',
  'sub-builds': 'The build slots and queues of both lanes, and each worktree’s last detached build.',
  'sub-tracks': "Each track's STATUS.md, newest first: when it changed and whether its worktree is busy.",
  'sub-tree': "The session's folder as a tree; folders open as you click them.",
  'sub-changes': 'Files git sees as changed (git status), read again every 30 s.',
  'sub-preview': 'The file you picked last, with its first lines.',
  'all-issues': 'Also list the issues that are triaged (every open issue).',
  id: 'A waiting id. Click to put it into the prompt.',
  unit: 'A unit in the release queue. Click to put its file into the prompt.',
  track: "A track. Click to put its STATUS.md's path into the prompt.",
  dir: 'A folder. Click to open or close it.',
  file: 'A file. Click to preview it.',
  change: 'A changed file: its git status mark, then its path. Click to preview it.',
  'insert-path': "Puts this file's path into the prompt at the cursor.",
  'close-preview': 'Closes the preview and goes back to the tree.',
}

// The kind of a drawn element, from its key: the prefix before its id, or the whole key.
const PREFIXES = ['id', 'unit', 'track', 'dir', 'file', 'change']
export function kindOf(key: string | undefined): string | null {
  if (!key) return null
  if (TIPS[key]) return key
  const p = PREFIXES.find(x => key.startsWith(`${x}-`))
  return p ?? null
}

/** The hover group of an element of this kind: hovering it shows its tip. */
export const tip = (kind: string) => ({ scope: `tip-${kind}` })

const IDLE_TIP = 'Point at anything here, or Tab through it (ctrl+x tab), to see what it is for.'

/** The tip row: the focused element's tip (else a hint), and over it, revealed by hover, the hovered one's. */
export function TipRow(el: El, width: number, focused: string | null) {
  const { Box, Text } = el
  const base = (focused && TIPS[focused]) || IDLE_TIP
  return (
    <Box flexDirection="column" marginTop={1}>
      <Text dimColor>{'─'.repeat(width)}</Text>
      <Box position="relative" height={1}>
        <Text dimColor italic={!focused}>
          {fit(base, width)}
        </Text>
        {Object.entries(TIPS).map(([kind, text]) => (
          <Box
            key={`tipbox-${kind}`}
            position="absolute"
            top={0}
            left={0}
            display="none"
            hover={{ ...tip(kind), display: 'flex' }}
          >
            <Text color={C.info}>{fit(text, width)}</Text>
          </Box>
        ))}
      </Box>
    </Box>
  )
}

/** Text cut or padded to exactly w cells (an ellipsis marks a cut). */
export function fit(s: string, w: number): string {
  if (w <= 0) return ''
  const t = s.replace(/\s+/g, ' ')
  return t.length > w ? `${t.slice(0, Math.max(0, w - 1))}…` : t.padEnd(w)
}

/** One row of fixed-width columns as one string, for a Button or a Text. The last column takes what is left. */
export function columns(cells: [string, number][], total: number): string {
  let used = 0
  return cells
    .map(([s, w], i) => {
      const width = i === cells.length - 1 ? Math.max(1, total - used) : w
      used += width + 1
      return i === cells.length - 1 ? fit(s, width).trimEnd() : fit(s, width)
    })
    .join(' ')
}

export type TabSpec = { key: string; label: string; active: boolean; onPress: () => unknown }

/** The top row: one button per tab, the active one filled. Plain labels: no counts, no hotkeys. */
export function Tabs(el: El, tabs: TabSpec[]) {
  const { Box, Button } = el
  return (
    <Box flexDirection="row" columnGap={1} flexWrap="wrap">
      {tabs.map(t => (
        <Button
          key={`tab-${t.key}`}
          label={t.label}
          variant={t.active ? 'primary' : 'secondary'}
          hover={tip(`tab-${t.key}`)}
          onPress={() => void t.onPress()}
        />
      ))}
    </Box>
  )
}

/** The row under a tab: plain words, the active one in the primary look. */
export function SubTabs(el: El, tabs: TabSpec[]) {
  const { Box, Button } = el
  return (
    <Box flexDirection="row" columnGap={2} marginTop={1} flexWrap="wrap">
      {tabs.map(t =>
        t.active ? (
          <Button
            key={`sub-${t.key}`}
            label={t.label}
            variant="primary"
            hover={tip(`sub-${t.key}`)}
            onPress={() => void t.onPress()}
          />
        ) : (
          <Button key={`sub-${t.key}`} plain label={t.label} hover={tip(`sub-${t.key}`)} onPress={() => void t.onPress()} />
        ),
      )}
    </Box>
  )
}

/** A row that opens or picks something: a one-glyph Button (a Button draws its label alone, in no colour of its
 * own), then the row's words in their colours. The Button's key is the row's; its kind names its tip. */
export function Row(
  el: El,
  key: string,
  kind: string,
  glyph: string,
  onPress: () => unknown,
  words: RenderElement | RenderElement[],
) {
  const { Box, Button, Text } = el
  return (
    <Box key={`row-${key}`} flexDirection="row">
      <Button key={key} plain label={glyph} hover={tip(kind)} onPress={() => void onPress()} />
      <Text> </Text>
      {words}
    </Box>
  )
}

/** A dim rule with a title, the pane's width across. */
export function Rule(el: El, title: string, width: number, note = '') {
  const { Text } = el
  const head = `── ${title} ${note ? `· ${note} ` : ''}`
  return <Text dimColor>{fit(head + '─'.repeat(Math.max(0, width - head.length)), width)}</Text>
}

/** A section of the board that could not be read: said in red, never drawn as empty. */
export function NotRead(el: El, what: string, error: string | null | undefined, width: number) {
  const { Text } = el
  return <Text color={C.bad}>{fit(`${what}: not read (${error ?? 'not read yet'})`, width).trimEnd()}</Text>
}

/** "and N more" under a capped list, or nothing. */
export function More(el: El, shown: number, total: number) {
  const { Text } = el
  return total > shown ? <Text dimColor>… and {total - shown} more</Text> : null
}
