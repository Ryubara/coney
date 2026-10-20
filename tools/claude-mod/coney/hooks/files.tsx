// The Files tab: the session's folder (the coordinator's checkout) as a tree that opens folder by folder, the git
// changes, and a preview of the file picked in either (a sub-tab that appears once a file is picked).
import type { RenderElement } from 'claude-code'

import type { Changes, Listing, Preview, View } from '../types'
import { C, columns, fit, Row, SubTabs, tip } from './kit'
import type { Act, El } from './kit'

const join = (rel: string, name: string) => (rel ? `${rel}\\${name}` : name)

function size(n: number): string {
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} K`
  return `${(n / 1024 / 1024).toFixed(1)} M`
}

// A change's mark in colour: staged green, unstaged yellow, untracked dim, conflicts red.
function markColor(mark: string): string | undefined {
  if (mark === '??') return 'subtle'
  if (mark.includes('U')) return C.bad
  if (mark[0] !== ' ') return C.good
  return C.warn
}

export function FilesTab(
  act: Act,
  el: El,
  root: string,
  t: Record<string, Listing>,
  c: Changes,
  p: Preview | null,
  v: View,
  width: number,
  rows: number,
) {
  const { Box, Button, Text, Code, Link } = el
  const set = (change: Partial<View>) => () => act.edit(x => ({ ...x, ...change }))
  const marks = new Map(c.lines.map(l => [l.path, l.mark]))
  const pickFile = (rel: string) => async () => {
    await act.edit(x => ({ ...x, file: rel, filesSub: 'preview' }))
    await act.preview(rel)
  }
  const tabs = [
    { key: 'tree', label: 'Tree', active: v.filesSub === 'tree', onPress: set({ filesSub: 'tree' }) },
    {
      key: 'changes',
      label: 'Changes',
      active: v.filesSub === 'changes',
      onPress: async () => {
        await set({ filesSub: 'changes' })()
        await act.changes()
      },
    },
  ]
  if (v.file) {
    const name = v.file.split('\\').pop() ?? v.file
    tabs.push({
      key: 'preview',
      label: `Preview: ${fit(name, 24).trimEnd()}`,
      active: v.filesSub === 'preview',
      onPress: set({ filesSub: 'preview' }),
    })
  }
  const cap = Math.max(5, rows - 4)

  let body
  if (v.filesSub === 'tree') {
    const out: RenderElement[] = []
    const walk = (rel: string, depth: number) => {
      const listing = t[rel]
      if (!listing) {
        out.push(
          <Text key={`wait-${rel}`} dimColor>
            {'  '.repeat(depth)}reading…
          </Text>,
        )
        return
      }
      if (listing.error)
        out.push(
          <Text key={`err-${rel}`} color={C.bad}>
            {'  '.repeat(depth)}
            {listing.error}
          </Text>,
        )
      for (const e of listing.entries) {
        const path = join(rel, e.name)
        const isOpen = v.open.includes(path)
        const dirty = e.dir && [...marks.keys()].some(k => k.startsWith(`${path}\\`))
        const mark = marks.get(path) ?? (dirty ? '•' : '')
        const label = `${'  '.repeat(depth)}${e.name}${e.dir ? '\\' : ''}`
        const press = e.dir
          ? async () => {
              await act.edit(x => ({
                ...x,
                open: isOpen ? x.open.filter(o => o !== path && !o.startsWith(`${path}\\`)) : [...x.open, path],
              }))
              if (!isOpen) await act.list(path)
            }
          : pickFile(path)
        out.push(
          Row(el, `${e.dir ? 'dir' : 'file'}-${path}`, e.dir ? 'dir' : 'file', e.dir ? (isOpen ? '▾' : '▸') : '·', press, [
            <Text bold={e.dir} color={v.file === path ? C.info : undefined}>
              {fit(label, Math.max(8, width - 16))}
            </Text>,
            <Text color={mark ? markColor(mark) : undefined}>{fit(mark, 3)}</Text>,
            <Text dimColor>{e.dir ? '' : size(e.size).padStart(9)}</Text>,
          ]),
        )
        if (e.dir && isOpen) walk(path, depth + 1)
      }
    }
    walk('', 0)
    body = (
      <Box flexDirection="column">
        <Text dimColor>{fit(root, width)}</Text>
        {out.slice(0, cap)}
        {out.length > cap && <Text dimColor>… {out.length - cap} more rows: close a folder</Text>}
      </Box>
    )
  } else if (v.filesSub === 'changes') {
    body = (
      <Box flexDirection="column">
        <Text dimColor>git status · {c.at ? c.at.slice(11, 19) : 'not read'} UTC</Text>
        {c.error && <Text color={C.bad}>{c.error}</Text>}
        {!c.error && c.lines.length === 0 && <Text dimColor>No changes.</Text>}
        {c.lines.slice(0, cap).map(l => (
          Row(el, `change-${l.path}`, 'change', '›', pickFile(l.path), [
            <Text color={markColor(l.mark)}>{l.mark} </Text>,
            <Text>{fit(l.path, width - 6).trimEnd()}</Text>,
          ])
        ))}
        {c.lines.length > cap && <Text dimColor>… and {c.lines.length - cap} more</Text>}
      </Box>
    )
  } else if (p) {
    const ext = p.path.split('.').pop() ?? ''
    const lines = (p.text ?? '').split('\n')
    const room = Math.max(5, rows - 8)
    body = (
      <Box flexDirection="column">
        <Text>
          <Text bold>{fit(p.path, Math.max(8, width - 30)).trimEnd()}</Text>
          <Text dimColor>
            {' '}
            · {size(p.size)}
            {p.mtimeMs ? ` · ${new Date(p.mtimeMs).toISOString().slice(0, 16).replace('T', ' ')} UTC` : ''}
            {marks.get(p.path) ? ` · git ${marks.get(p.path)}` : ''}
          </Text>
        </Text>
        <Box flexDirection="row" columnGap={1} flexWrap="wrap">
          <Button
            key="insert-path"
            hover={tip('insert-path')}
            label="Path to prompt"
            onPress={() => void act.fill(`${p.path} `)}
          />
          <Link href={`vscode://file/${encodeURI(`${root}\\${p.path}`.replace(/\\/g, '/'))}`} label="Open in VS Code" />
          <Button
            key="close-preview"
            hover={tip('close-preview')}
            label="Close"
            role="dismiss"
            onPress={set({ file: null, filesSub: 'tree' })}
          />
        </Box>
        {p.error ? (
          <Text color={C.bad}>{p.error}</Text>
        ) : (
          <Code
            source={lines.slice(0, room).join('\n')}
            path={p.path}
            language={ext}
            startLine={1}
            wrap="truncate-end"
          />
        )}
        {lines.length > room && <Text dimColor>{columns([[`… ${lines.length - room} more lines`, 0]], width)}</Text>}
      </Box>
    )
  } else {
    body = <Text dimColor>Reading…</Text>
  }

  return (
    <Box flexDirection="column">
      {SubTabs(el, tabs)}
      <Box marginTop={1} flexDirection="column">
        {body}
      </Box>
    </Box>
  )
}
