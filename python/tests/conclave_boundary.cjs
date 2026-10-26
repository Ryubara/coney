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
//  - what the five hooks do is held too: `command.run` and `ui.render` carry the filters that keep them off Tether's
//    commands and panes, `session.start`, `agent.offer` and the `.catch` of a hook end every path in `next(e)`,
//    `classic.SessionStart` passes the event on first and returns that answer whole, and `$.command.register` and
//    `$.ui.open` take only Conclave's own name;
//  - `$.process.run` takes only the snapshot command (its project folder built from the checkout the mod sits in, its
//    options only `cwd`, `timeoutMs` and an `env` of the private Python environment); `any`, type-error suppression,
//    run-time code loading, globals the mod has no business with, and anything Tether keeps (its events and its words,
//    also split over a `+` or a template) are refused.
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
const EXACT_WORDS = new Set(req.exact_words)
const BANNED = new Set(req.banned_globals)
const NAME = req.name

// The program: the mod's tsconfig, its snippets as virtual files beside the hooks, and nothing of the tests.
const parsed = ts.getParsedCommandLineOfConfigFile(path.join(mod, 'tsconfig.json'), {}, {
  ...ts.sys,
  onUnRecoverableConfigFileDiagnostic: d => {
    throw new Error(ts.flattenDiagnosticMessageText(d.messageText, '\n'))
  },
})
// With `stub` the types are Coney's own small description of the engine instead of the generated ones (which only a
// logged-in Claude Code writes); the stub then stands alone, and type errors are not this check's to report.
const stub = req.stub === null ? null : path.resolve(req.stub).replace(/\\/g, '/')
const isGenerated = file => /\/\.claude-plugin\/types\//.test(file.replace(/\\/g, '/'))
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
const roots = [
  ...parsed.fileNames.filter(file => !/\/tests\//.test(file) && (stub === null || !isGenerated(file))),
  ...(stub === null ? [] : [stub]),
  ...virtual.keys(),
]
const program = ts.createProgram({ rootNames: roots, options: parsed.options, host })
const checker = program.getTypeChecker()

/** Whether `file` is where the types of `claude-code` come from: the engine's generated ones, or the stub. */
const isEngineTypes = file =>
  stub === null ? /\/claude-code\/index\.d\.ts$/.test(file.replace(/\\/g, '/')) : norm(file) === norm(stub)

// The engine's own types: `EngineInterface` and `On`, from the module the engine writes beside the mod (or the stub).
function claudeCodeModule() {
  for (const file of program.getSourceFiles()) {
    if (!isEngineTypes(file.fileName)) continue
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
  process.stderr.write("the engine's types for 'claude-code' are neither beside the mod nor given as a stub\n")
  process.exit(2)
}
const exported = name => checker.getExportsOfModule(engineModule.symbol).find(symbol => symbol.name === name)
const ENGINE = checker.getDeclaredTypeOfSymbol(exported('EngineInterface'))
const ON = checker.getDeclaredTypeOfSymbol(exported('On'))
const ON_ALIAS = exported('On')

// `describe`: what the types say of the names and nouns this check relies on, so the stub can be held against the
// engine's own (python/tests/test_conclave_engine_stub.py).
if (req.describe) {
  const kinds = {}
  for (const name of req.describe) {
    const symbol = exported(name)
    kinds[name] = symbol === undefined ? null : symbol.flags & ts.SymbolFlags.Value ? 'value' : 'type'
  }
  const nouns = {}
  for (const prop of checker.getPropertiesOfType(ENGINE)) {
    if (prop.name.startsWith('__@')) continue
    const type = checker.getTypeOfSymbolAtLocation(prop, prop.valueDeclaration ?? prop.declarations[0])
    nouns[prop.name] = checker.getPropertiesOfType(type).map(member => member.name).sort()
  }
  process.stdout.write(JSON.stringify({ kinds, nouns }))
  process.exit(0)
}

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

const runtimeFiles = new Set([...req.runtime.map(norm), ...[...virtual.keys()].map(norm)])
const isOwn = node => runtimeFiles.has(norm(node.getSourceFile().fileName))
const isEngineFile = node => isEngineTypes(node.getSourceFile().fileName)

/** Whether `callee` names one of claude-code's own functions that take `$` (`read`, `update`). */
function isEngineFunction(callee) {
  let symbol = ts.isIdentifier(callee) ? checker.getSymbolAtLocation(callee) : undefined
  if (symbol !== undefined && symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol)
  return symbol !== undefined && ENGINE_FUNCTIONS.has(symbol.name) && symbol.declarations?.some(isEngineFile) === true
}

const {
  unwrap, isName, plainProperties, constText, snapshotProblem, isRunOptions, inlineFunction, passingHookProblems,
  sessionStartProblems, stopGuardProblems, NOT_THE_COMMAND,
} = require('./conclave_boundary_syntax.cjs')({ ts, checker, req, mod, norm, isEngine, isOwn, isEngineFunction })

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

  // `$.process.run` takes the snapshot command, in the checkout the mod sits in, with the options of the private
  // environment, and nothing else.
  function checkRun(call) {
    if (call.arguments.length > 2 || (call.arguments[1] !== undefined && !isRunOptions(call.arguments[1]))) {
      err(call, `passes options to $.process.run other than ${req.run.options.join(', ')}, with cwd the checkout and env only ${req.run.env.join(', ')} of venvOf(<checkout>)`)
    }
    const first = call.arguments[0]
    if (first !== undefined && ts.isArrayLiteralExpression(first)) {
      const problem = snapshotProblem(first)
      if (problem !== null) err(call, problem)
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
      (ts.getCombinedModifierFlags(decl) & ts.ModifierFlags.Export) === 0
    const problem = isFixed ? snapshotProblem(decl.initializer) : NOT_THE_COMMAND
    if (problem !== null) {
      err(call, problem)
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
        else if (key === 'command.register') checkNamed(callee.parent, key, 'name', 'description')
        else if (key === 'ui.open') checkNamed(callee.parent, key, 'id', 'title')
        else if (key === 'prompt.submit') checkSubmit(callee.parent)
        else if (key === 'ui.toast' && callee.parent.arguments.length !== 1) err(callee.parent, '$.ui.toast takes only the text to show')
        else if (key === 'prompt.read' && callee.parent.arguments.length !== 0) err(callee.parent, '$.prompt.read takes no argument')
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

  // `$.command.register` and `$.ui.open` take only Conclave's own name, so Tether's command and pane stay Tether's.
  // Each takes its name and a text to show, and no option that changes what the engine does with them.
  function checkNamed(call, key, property, text) {
    const props = call.arguments.length === 1 ? plainProperties(call.arguments[0]) : null
    const value = props?.get(property)
    if (value === undefined || !isName(value)) err(call, `$.${key} takes only the ${property} '${NAME}' (Tether's names stay Tether's)`)
    else if ([...props.keys()].some(one => one !== property && one !== text)) err(call, `$.${key} takes only ${property} and ${text}`)
  }

  // A wake is the text alone: no `asUser`, no other option that changes how the engine takes it.
  function checkSubmit(call) {
    const props = call.arguments.length === 1 ? plainProperties(call.arguments[0]) : null
    if (props === null || props.size !== 1 || !props.has('text')) err(call, '$.prompt.submit takes only { text }')
  }

  // The filter of a hook: `command.run` is Conclave's own command only, `ui.render` its band and its pane only.
  function checkFilter(call, event, matcher) {
    const props = matcher === undefined ? null : plainProperties(matcher)
    if (event === 'command.run') {
      const ok = props !== null && props.size === 1 && props.has('command') && isName(props.get('command'))
      if (!ok) err(matcher ?? call, `command.run must be filtered by { command: '${NAME}' }, or it answers Tether's commands too`)
    }
    if (event === 'ui.render') {
      const component = props?.get('component')
      const which = component !== undefined && ts.isStringLiteral(component) ? component.text : null
      const ok =
        props !== null &&
        ((which === 'AbovePrompt' && props.size === 1) ||
          (which === 'Pane' && props.size === 2 && props.has('requestId') && isName(props.get('requestId'))))
      if (!ok) {
        err(matcher ?? call, `ui.render must be filtered by { component: 'AbovePrompt' } or { component: 'Pane', requestId: '${NAME}' }, or it draws over Tether's panes`)
      }
    }
  }

  // The two hooks read in conclave_boundary_syntax.cjs: what each passes on, and how.
  const checkPassingHook = (fn, label, refusal) => passingHookProblems(fn, label, refusal).forEach(([node, why]) => err(node, why))
  const checkSessionStartHook = fn => sessionStartProblems(fn).forEach(([node, why]) => err(node, why))
  const checkStopGuardHook = fn => stopGuardProblems(fn).forEach(([node, why]) => err(node, why))

  // What an on(...) call does: its filter, its handler, and the `.catch` it may carry (which also passes the event on).
  function checkHook(call, event) {
    const args = call.arguments
    if (args.length < 2 || args.length > 3) return err(call, `on('${event}') takes a pattern, a filter (optional) and a hook`)
    checkFilter(call, event, args.length === 3 ? args[1] : undefined)
    // An arrow function of at most ($, e, next), each a plain name: it has no `arguments`, `this`, name, default, rest
    // or destructured parameter to reach `e` and `next` by another way.
    const arrow = node => {
      const fn = inlineFunction(node)
      const isPlain = param => ts.isIdentifier(param.name) && param.initializer === undefined && param.dotDotDotToken === undefined
      return fn !== null && ts.isArrowFunction(fn) && fn.parameters.length <= 3 && fn.parameters.every(isPlain) ? fn : null
    }
    const hook = arrow(args[args.length - 1])
    if (hook === null) err(args[args.length - 1], `the hook of ${event} must be an arrow function written in place with at most ($, e, next), each a plain name, which this check reads`)
    else if (event === 'classic.SessionStart') checkSessionStartHook(hook)
    else if (event === 'classic.Stop') checkStopGuardHook(hook)
    else if (event === 'session.start' || event === 'agent.offer') checkPassingHook(hook, event, event === 'agent.offer')
    const outer = top(call)
    const chained = outer.parent
    if (ts.isExpressionStatement(chained)) return
    const isCatch =
      ts.isPropertyAccessExpression(chained) &&
      chained.expression === outer &&
      chained.name.text === 'catch' &&
      ts.isCallExpression(chained.parent) &&
      chained.parent.expression === chained &&
      chained.parent.arguments.length === 1 &&
      ts.isExpressionStatement(chained.parent.parent)
    const handler = isCatch ? arrow(chained.parent.arguments[0]) : null
    if (handler === null) {
      err(call, 'uses the registration that on(...) returns other than as a statement, or with a .catch of an arrow function that passes the event on')
    }
    else checkPassingHook(handler, `the .catch of ${event}`, false)
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
      if (EVENTS.has(event.text)) return checkHook(parent, event.text)
      return err(event, TETHER.has(event.text) ? `hooks the event ${event.text}, which is Tether's` : `hooks the event ${event.text}, which is not one of the six the mod lists`)
    }
    return err(n, '`on` is used other than as on(...): it is renamed, passed on, cast or called indirectly')
  }

  const isNameOnly = n =>
    (ts.isPropertyAccessExpression(n.parent) || ts.isQualifiedName(n.parent)) && (n.parent.name === n || n.parent.right === n)

  // Tether's words and event names, and the one name that reaches the `Function` constructor.
  function checkWords(n, text, isString) {
    if (isString && TETHER.has(text)) err(n, `names the event ${text}, which is Tether's`)
    for (const word of WORDS) if (text.includes(word)) err(n, `names ${JSON.stringify(word)}: Tether's, not Conclave's`)
    if (EXACT_WORDS.has(text)) err(n, `names ${JSON.stringify(text)}, which reaches code built at run time`)
  }

  function walk(n) {
    // Words and names that are Tether's.
    if (ts.isStringLiteralLike(n) || ts.isTemplateHead(n) || ts.isTemplateMiddle(n) || ts.isTemplateTail(n) || ts.isIdentifier(n)) {
      checkWords(n, n.text, !ts.isIdentifier(n))
    }
    // The same words split over a `+` or a template of constants.
    const isPlus = one => ts.isBinaryExpression(one) && one.operatorToken.kind === ts.SyntaxKind.PlusToken
    if (isPlus(n) || ts.isTemplateExpression(n)) {
      const outer = top(n).parent
      const folded = isPlus(outer) || ts.isTemplateSpan(outer) ? null : constText(n)
      if (folded !== null) checkWords(n, folded, true)
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
    if (ts.isAsExpression(n) && ts.isAsExpression(unwrap(n.expression)) && unwrap(n.expression).type.kind === ts.SyntaxKind.UnknownKeyword) {
      err(n, 'casts through `as unknown as`, which hides what a value is from this check')
    }
    // A name computed at run time reaches what no written name may (a constructor), whatever the value's type says.
    if (ts.isElementAccessExpression(n) && constText(n.argumentExpression) === null) {
      err(n, 'reads a property by a computed name, which reaches code built at run time (use a switch or a Map)')
    }
    // The same in a destructuring (`const { [k]: G } = f`, a parameter, an assignment) or an object's key.
    if (ts.isComputedPropertyName(n) && constText(n.expression) === null) {
      err(n, 'names a property by a computed name, which reaches code built at run time (use a switch or a Map)')
    }
    // Loops and declarations that wait without an `await` the hook checks can see.
    if (ts.isForOfStatement(n) && n.awaitModifier !== undefined) err(n, 'uses `for await`, a wait this check does not follow')
    if (ts.isVariableDeclarationList(n) && (n.flags & ts.NodeFlags.Using) !== 0) {
      err(n, 'declares with `using` or `await using`, which runs code (and may wait) when the block ends')
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
  const diagnostics = [...program.getSyntacticDiagnostics(sf), ...(stub === null ? program.getSemanticDiagnostics(sf) : [])]
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
