// SPDX-License-Identifier: GPL-3.0-or-later
// The boundary check of the Conclave mod, from the TypeScript compiler's own tree and types (docs/guides/conclave.md#boundary).
// python/tests/conclave_boundary.py runs it with the lists it keeps and tests it; this file holds the rules only.
//
// It reads a JSON request on stdin, builds one program over the mod (its tsconfig, the engine's generated types and
// the snippets it is given as extra files) and answers, per file, what crosses the line and which events it hooks.
//
// The rules rest on types, not spellings, so a renamed `$`, an alias, a spread or a cast cannot hide a call:
//  - a value of type EngineInterface may only be the object of `$.noun.method` where the pair is in the mod's list,
//    called; or an argument to a function of the mod's own that takes an EngineInterface (or to `read` / `update`);
//  - a value of type On (the `on` of `register`) may only be called, with a plain string literal that names one of the
//    events the mod lists;
//  - `$.process.run` takes only the snapshot command; `any`, type-error suppression, run-time code loading, globals the
//    mod has no business with, and anything Tether keeps (its events and its words) are refused.
'use strict'

const fs = require('node:fs')
const path = require('node:path')
const ts = require(process.env.CONEY_TYPESCRIPT)

const req = JSON.parse(fs.readFileSync(0, 'utf8'))
const norm = file => path.resolve(file).replace(/\\/g, '/').toLowerCase()
const mod = path.resolve(req.mod).replace(/\\/g, '/')
const EVENTS = new Set(req.events)
const TETHER = new Set(req.tether_events)
const CALLS = new Set(req.api.calls)
const PROPERTIES = new Set(req.api.properties)
const ENGINE_FUNCTIONS = new Set(req.engine_functions)
const WORDS = req.words
const BANNED = new Set(req.banned_globals)

// The program: the mod's tsconfig, its snippets as virtual files beside the hooks, and nothing of the tests.
const parsed = ts.getParsedCommandLineOfConfigFile(path.join(mod, 'tsconfig.json'), {}, {
  ...ts.sys,
  onUnRecoverableConfigFileDiagnostic: d => {
    throw new Error(ts.flattenDiagnosticMessageText(d.messageText, '\n'))
  },
})
const virtual = new Map()
const snippetNames = new Map()
const prelude = req.prelude.join('\n') + '\n'
Object.entries(req.snippets).forEach(([name, text], index) => {
  const file = `${mod}/hooks/boundary-snippet-${index}.tsx`
  virtual.set(file, prelude + text)
  snippetNames.set(file, name)
})
const host = ts.createCompilerHost(parsed.options)
const realExists = host.fileExists.bind(host)
const realRead = host.readFile.bind(host)
const realSource = host.getSourceFile.bind(host)
host.fileExists = file => virtual.has(file) || realExists(file)
host.readFile = file => (virtual.has(file) ? virtual.get(file) : realRead(file))
host.getSourceFile = (file, ...rest) =>
  virtual.has(file) ? ts.createSourceFile(file, virtual.get(file), parsed.options.target, true, ts.ScriptKind.TSX) : realSource(file, ...rest)
const roots = [...parsed.fileNames.filter(file => !/\/tests\//.test(file)), ...virtual.keys()]
const program = ts.createProgram({ rootNames: roots, options: parsed.options, host })
const checker = program.getTypeChecker()

// The engine's own types: `EngineInterface` and `On`, from the module the engine writes beside the mod.
function claudeCodeModule() {
  for (const file of program.getSourceFiles()) {
    if (!/\/claude-code\/index\.d\.ts$/.test(file.fileName.replace(/\\/g, '/'))) continue
    for (const statement of file.statements) {
      if (ts.isModuleDeclaration(statement) && statement.name.text === 'claude-code') {
        return { file, symbol: checker.getSymbolAtLocation(statement.name) }
      }
    }
  }
  return null
}
const engineModule = claudeCodeModule()
if (engineModule === null || engineModule.symbol === undefined) {
  process.stderr.write("the engine's types for 'claude-code' are not beside the mod (run claude plugin validate on it)\n")
  process.exit(2)
}
const exported = name => checker.getExportsOfModule(engineModule.symbol).find(symbol => symbol.name === name)
const ENGINE = checker.getDeclaredTypeOfSymbol(exported('EngineInterface'))
const ON = checker.getDeclaredTypeOfSymbol(exported('On'))
const ON_ALIAS = exported('On')

const SKIP = ts.TypeFlags.Any | ts.TypeFlags.Never | ts.TypeFlags.Unknown | ts.TypeFlags.Void | ts.TypeFlags.Undefined | ts.TypeFlags.Null
const cache = new Map()

/** Whether a value of type `t` is (or can be used as) the engine: EngineInterface, a subtype, or a union with one. */
function isEngine(t, seen = new Set()) {
  if (t === undefined || seen.has(t) || t.flags & SKIP) return false
  if (cache.has(t)) return cache.get(t)
  seen.add(t)
  let answer
  if (t.isUnion()) answer = t.types.some(one => isEngine(one, seen))
  else if (t.flags & ts.TypeFlags.TypeParameter) answer = isEngine(checker.getBaseConstraintOfType(t), seen)
  else answer = checker.isTypeAssignableTo(t, ENGINE)
  cache.set(t, answer)
  return answer
}

/** Whether `t` is the engine's `On` (the registrar `register` is given). */
function isOn(t) {
  if (t === undefined || t.flags & SKIP) return false
  if (t.isUnion()) return t.types.some(isOn)
  return t === ON || t.aliasSymbol === ON_ALIAS
}

const isWrapper = n => n.kind === ts.SyntaxKind.ParenthesizedExpression || n.kind === ts.SyntaxKind.NonNullExpression
/** `n` or, when it sits in parentheses or a `!`, the outermost of those (they keep what the value is). */
function top(n) {
  while (n.parent && isWrapper(n.parent)) n = n.parent
  return n
}

const runtimeFiles = new Set([...req.files.map(norm), ...[...virtual.keys()].map(norm)])
const isOwn = node => runtimeFiles.has(norm(node.getSourceFile().fileName))
const isEngineFile = node => /\/claude-code\/index\.d\.ts$/.test(node.getSourceFile().fileName.replace(/\\/g, '/'))

/** The command arrays the check accepts: `uv run -q --project <dir> coney-tools conclave snapshot --json`. */
function isSnapshotCommand(array) {
  const items = array.elements
  if (items.length !== req.run.head.length + 1 + req.run.tail.length || items.some(ts.isSpreadElement)) return false
  const same = (item, text) => ts.isStringLiteralLike(item) && item.text === text
  return (
    req.run.head.every((text, i) => same(items[i], text)) &&
    req.run.tail.every((text, i) => same(items[req.run.head.length + 1 + i], text))
  )
}

/** Whether `call` is `<engine>.process.run(...)`. */
function isProcessRun(call) {
  const callee = call.expression
  return (
    ts.isPropertyAccessExpression(callee) &&
    callee.name.text === 'run' &&
    ts.isPropertyAccessExpression(top(callee.expression)) &&
    top(callee.expression).name.text === 'process' &&
    isEngine(checker.getTypeAtLocation(top(callee.expression).expression))
  )
}

function checkFile(sf, label, errors, events) {
  const err = (node, message) => {
    const line = label === null ? '' : `${label}:${sf.getLineAndCharacterOfPosition(node.getStart(sf)).line + 1}: `
    errors.push(line + message)
  }

  // `$.process.run` takes the snapshot command and nothing else.
  function checkRun(call) {
    const first = call.arguments[0]
    if (first !== undefined && ts.isArrayLiteralExpression(first)) {
      if (!isSnapshotCommand(first)) err(call, 'runs a command other than `coney-tools conclave snapshot --json` through $.process.run')
      return
    }
    const symbol = first !== undefined && ts.isIdentifier(first) ? checker.getSymbolAtLocation(first) : undefined
    const decl = symbol?.valueDeclaration
    const isFixed =
      decl !== undefined &&
      ts.isVariableDeclaration(decl) &&
      decl.initializer !== undefined &&
      ts.isArrayLiteralExpression(decl.initializer) &&
      (ts.getCombinedNodeFlags(decl) & ts.NodeFlags.Const) !== 0 &&
      (ts.getCombinedModifierFlags(decl) & ts.ModifierFlags.Export) === 0 &&
      isSnapshotCommand(decl.initializer)
    if (!isFixed) {
      err(call, 'runs a command other than `coney-tools conclave snapshot --json` through $.process.run')
      return
    }
    // The array may only be run, or grow by the flags the snapshot command takes.
    const visit = node => {
      if (ts.isIdentifier(node) && node !== decl.name && checker.getSymbolAtLocation(node) === symbol) {
        const parent = node.parent
        const isRun = ts.isCallExpression(parent) && parent.arguments[0] === node && isProcessRun(parent)
        const grow = ts.isPropertyAccessExpression(parent) && parent.name.text === 'push' && ts.isCallExpression(parent.parent)
        const isFlag =
          grow &&
          parent.parent.arguments.length === 1 &&
          ts.isStringLiteralLike(parent.parent.arguments[0]) &&
          req.run.flags.includes(parent.parent.arguments[0].text)
        if (!isRun && !isFlag) err(node, 'changes the snapshot command array other than by adding a flag the command takes')
      }
      ts.forEachChild(node, visit)
    }
    visit(sf)
  }

  // `X.noun` where X is the engine: the pair `noun.method` must be in the mod's list, and called.
  function checkMember(access) {
    const noun = access.name.text
    const outer = top(access)
    const next = outer.parent
    if (ts.isPropertyAccessExpression(next) && next.expression === outer) {
      const key = `${noun}.${next.name.text}`
      const callee = top(next)
      if (CALLS.has(key)) {
        if (!(ts.isCallExpression(callee.parent) && callee.parent.expression === callee)) {
          err(next, `uses $.${key} other than by calling it`)
        } else if (key === 'process.run') checkRun(callee.parent)
      } else if (!PROPERTIES.has(key)) {
        err(next, `uses $.${key}, which is not in the mod's list (no $.session. or $.model. at all)`)
      }
      return
    }
    err(access, `uses $.${noun}, which is not in the mod's list (the mod calls $.noun.method only)`)
  }

  // An engine passed on: only to a function of the mod's own whose parameter is an EngineInterface, or `read` / `update`.
  function checkPassed(call, arg) {
    const callee = call.expression.getText(sf)
    const signature = checker.getResolvedSignature(call)
    const decl = signature?.declaration
    const index = call.arguments.indexOf(arg)
    const param = decl?.parameters?.[index]
    if (decl !== undefined && param !== undefined && !param.dotDotDotToken) {
      if (isOwn(decl) && isEngine(checker.getTypeAtLocation(param))) return
      let symbol = checker.getSymbolAtLocation(call.expression)
      if (symbol !== undefined && symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol)
      const isEngineFunction = symbol !== undefined && ENGINE_FUNCTIONS.has(symbol.name) && symbol.declarations?.some(isEngineFile)
      if (isEngineFunction && index === 0) return
    }
    err(arg, `passes \`$\` to ${callee}, which is not a function of the mod's own that takes an EngineInterface`)
  }

  // Any other use of an engine value hides what is called on it.
  function checkEngine(n) {
    const parent = n.parent
    if (isWrapper(parent)) return
    if (ts.isPropertyAccessExpression(parent) && parent.expression === n) return checkMember(parent)
    if (ts.isElementAccessExpression(parent) && parent.expression === n) return err(n, 'reads `$` by a computed name')
    if ((ts.isCallExpression(parent) || ts.isNewExpression(parent)) && parent.arguments?.includes(n)) {
      return ts.isCallExpression(parent) ? checkPassed(parent, n) : err(n, 'passes `$` to a constructor')
    }
    if (ts.isVariableDeclaration(parent) && parent.initializer === n) {
      return err(n, ts.isIdentifier(parent.name) ? 'copies `$` into another name, which hides what is called on it' : 'destructures `$`, which hides what is called on it')
    }
    if (ts.isAsExpression(parent) || ts.isTypeAssertionExpression(parent) || ts.isSatisfiesExpression(parent)) {
      return err(n, 'casts `$`, which hides what it is from this check')
    }
    if (ts.isSpreadElement(parent) || ts.isSpreadAssignment(parent)) return err(n, 'spreads `$` into another value')
    return err(n, 'uses `$` other than as $.noun.method: it is copied, stored, returned or compared')
  }

  // The `on` of `register`: called with a plain event literal the mod lists, never anything else.
  function checkOn(n) {
    const parent = n.parent
    if (isWrapper(parent)) return
    if (ts.isCallExpression(parent) && parent.expression === n) {
      const event = parent.arguments[0]
      if (event === undefined || !(ts.isStringLiteral(event) || ts.isNoSubstitutionTemplateLiteral(event))) {
        return err(parent, 'an on(...) call whose event is not a plain string literal')
      }
      events.push(event.text)
      if (EVENTS.has(event.text)) return
      return err(event, TETHER.has(event.text) ? `hooks the event ${event.text}, which is Tether's` : `hooks the event ${event.text}, which is not one of the five the mod lists`)
    }
    return err(n, '`on` is used other than as on(...): it is renamed, passed on, cast or called indirectly')
  }

  const isNameOnly = n =>
    (ts.isPropertyAccessExpression(n.parent) || ts.isQualifiedName(n.parent)) && (n.parent.name === n || n.parent.right === n)

  function walk(n) {
    // Words and names that are Tether's.
    if (ts.isStringLiteralLike(n) || ts.isTemplateHead(n) || ts.isTemplateMiddle(n) || ts.isTemplateTail(n) || ts.isIdentifier(n)) {
      const text = n.text
      if (!ts.isIdentifier(n) && TETHER.has(text)) err(n, `names the event ${text}, which is Tether's`)
      for (const word of WORDS) if (text.includes(word)) err(n, `names ${JSON.stringify(word)}: Tether's, not Conclave's`)
    }
    if (ts.isImportDeclaration(n) || ts.isExportDeclaration(n)) {
      const spec = n.moduleSpecifier
      if (spec !== undefined && ts.isStringLiteral(spec) && spec.text !== 'claude-code' && !spec.text.startsWith('.')) {
        err(spec, `imports '${spec.text}': the mod uses only claude-code and its own files`)
      }
      // A file of its own is one this check reads: an import into the tests or anywhere else would hide code from it.
      if (spec !== undefined && ts.isStringLiteral(spec) && spec.text.startsWith('.')) {
        const found = ts.resolveModuleName(spec.text, sf.fileName, parsed.options, host).resolvedModule
        const file = found === undefined ? undefined : norm(found.resolvedFileName)
        if (file !== undefined && !runtimeFiles.has(file) && !file.endsWith('.d.ts')) {
          err(spec, `imports '${spec.text}', which is not one of the mod's checked sources`)
        }
      }
    }
    if (ts.isImportEqualsDeclaration(n) && ts.isExternalModuleReference(n.moduleReference)) err(n, 'loads code at run time (import = require)')
    if (ts.isCallExpression(n)) {
      if (n.expression.kind === ts.SyntaxKind.ImportKeyword) err(n, 'loads code at run time: import(...)')
      const name = ts.isIdentifier(n.expression) ? n.expression.text : ts.isPropertyAccessExpression(n.expression) ? n.expression.name.text : ''
      if (name === 'usage') err(n, "calls usage(...): the session's usage is Tether's, not Conclave's")
    }
    if (n.kind === ts.SyntaxKind.AnyKeyword) err(n, 'uses `any`, which hides what a value is from this check')
    if (ts.isIdentifier(n) && BANNED.has(n.text) && ts.isExpressionNode(n) && !isNameOnly(n)) {
      const symbol = checker.getSymbolAtLocation(n)
      if (symbol === undefined || !symbol.declarations?.some(isOwn)) err(n, `uses the global ${n.text}, which the mod has no business with`)
    }
    if (ts.isNewExpression(n) && ts.isIdentifier(n.expression) && BANNED.has(n.expression.text)) {
      err(n, `builds ${n.expression.text}, which loads code at run time`)
    }
    // Types decide the rest: what is an engine value, what is `on`.
    const isValue = (ts.isExpressionNode(n) && !(ts.isIdentifier(n) && isNameOnly(n))) || (ts.isIdentifier(n) && ts.isShorthandPropertyAssignment(n.parent) && n.parent.name === n)
    if (isValue) {
      const type = checker.getTypeAtLocation(n)
      if (isEngine(type)) checkEngine(n)
      else if (isOn(type)) checkOn(n)
    }
    if ((ts.isParameter(n) || ts.isVariableDeclaration(n)) && (ts.isObjectBindingPattern(n.name) || ts.isArrayBindingPattern(n.name))) {
      if (isEngine(checker.getTypeAtLocation(n.name))) err(n, 'destructures `$`, which hides what is called on it')
    }
    ts.forEachChild(n, walk)
  }

  if (/@ts-(?:ignore|expect-error|nocheck)/.test(sf.text)) errors.push((label === null ? '' : `${label}: `) + 'suppresses a type error, which hides what a value is from this check')
  walk(sf)
}

const answer = { files: {}, snippets: {}, diagnostics: {} }
for (const file of req.files) {
  const sf = program.getSourceFile(file) ?? program.getSourceFile(file.replace(/\\/g, '/'))
  if (sf === undefined) {
    answer.files[file] = { errors: ['is not part of the mod program (tsconfig include)'], events: [] }
    continue
  }
  const errors = []
  const events = []
  checkFile(sf, path.relative(mod, sf.fileName).replace(/\\/g, '/'), errors, events)
  const diagnostics = [...program.getSyntacticDiagnostics(sf), ...program.getSemanticDiagnostics(sf)]
  for (const d of diagnostics) {
    const at = d.start === undefined ? 1 : sf.getLineAndCharacterOfPosition(d.start).line + 1
    errors.push(`${path.relative(mod, sf.fileName).replace(/\\/g, '/')}:${at}: type error TS${d.code}: ${ts.flattenDiagnosticMessageText(d.messageText, ' ')}`)
  }
  answer.files[file] = { errors, events }
}
for (const [file, name] of snippetNames) {
  const errors = []
  const events = []
  checkFile(program.getSourceFile(file), null, errors, events)
  answer.snippets[name] = { errors, events }
}
process.stdout.write(JSON.stringify(answer))
