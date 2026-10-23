// SPDX-License-Identifier: GPL-3.0-or-later
// The tree-reading helpers of the Conclave boundary check (conclave_boundary.cjs holds the rules): what an expression is
// made of, what a hook passes on and how, whether a command array is the snapshot command, what a string of constants
// comes to.
'use strict'

/**
 * The helpers, over one TypeScript program: `ts` and its `checker`, the request `req`, the mod's folder `mod`, the file
 * name normaliser `norm`, and the check's own tests: `isEngine` (a value of the engine's type), `isOwn` (a node in
 * one of the mod's checked sources) and `isEngineFunction` (a callee that is one of claude-code's own functions).
 */
module.exports = function syntax({ ts, checker, req, mod, norm, isEngine, isOwn, isEngineFunction }) {
  const NAME = req.name
  const isWrapper = n => n.kind === ts.SyntaxKind.ParenthesizedExpression || n.kind === ts.SyntaxKind.NonNullExpression

  /** `n` without the parentheses and `!` around it. */
  function unwrap(n) {
    while (isWrapper(n)) n = n.expression
    return n
  }

  /** Whether `node` is the string 'conclave': the literal itself, or a const that holds it (a cast does not make it one). */
  function isName(node, depth = 0) {
    node = unwrap(node)
    if (depth > 8) return false
    if (ts.isStringLiteralLike(node)) return node.text === NAME
    if (!ts.isIdentifier(node)) return false
    const init = constInitializer(node)
    return init !== null && isName(init, depth + 1)
  }

  /**
   * The members of an object literal made only of plain `key: value` members, as a map, or null when it spreads,
   * computes a key, has a method or a shorthand member (which this check could not follow).
   */
  function plainProperties(node) {
    node = unwrap(node)
    if (!ts.isObjectLiteralExpression(node)) return null
    const found = new Map()
    for (const member of node.properties) {
      if (!ts.isPropertyAssignment(member) || !(ts.isIdentifier(member.name) || ts.isStringLiteral(member.name))) return null
      found.set(member.name.text, member.initializer)
    }
    return found
  }

  /** The initializer of the const that identifier `id` names, or null. */
  function constInitializer(id) {
    let symbol = checker.getSymbolAtLocation(id)
    if (symbol !== undefined && symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol)
    const decl = symbol?.valueDeclaration
    const isConst = decl !== undefined && ts.isVariableDeclaration(decl) && (ts.getCombinedNodeFlags(decl) & ts.NodeFlags.Const) !== 0
    return isConst && decl.initializer !== undefined ? unwrap(decl.initializer) : null
  }

  /** The text a string built of literals, consts, `+` and templates comes to, or null when part of it is not constant. */
  function constText(n, depth = 0) {
    n = unwrap(n)
    if (depth > 8) return null
    if (ts.isStringLiteralLike(n) || ts.isNumericLiteral(n)) return n.text
    if (ts.isAsExpression(n)) return constText(n.expression, depth + 1)
    if (ts.isIdentifier(n)) {
      const init = constInitializer(n)
      return init === null ? null : constText(init, depth + 1)
    }
    if (ts.isBinaryExpression(n) && n.operatorToken.kind === ts.SyntaxKind.PlusToken) {
      const left = constText(n.left, depth + 1)
      const right = constText(n.right, depth + 1)
      return left === null || right === null ? null : left + right
    }
    if (ts.isTemplateExpression(n)) {
      let text = n.head.text
      for (const span of n.templateSpans) {
        const part = constText(span.expression, depth + 1)
        if (part === null) return null
        text += part + span.literal.text
      }
      return text
    }
    return null
  }

  /** Whether `arg` is `<engine>.plugin.root`, the folder the mod was loaded from. */
  function isPluginRoot(arg) {
    const root = unwrap(arg)
    const plugin = ts.isPropertyAccessExpression(root) ? unwrap(root.expression) : null
    return (
      root.name?.text === 'root' &&
      plugin !== null &&
      ts.isPropertyAccessExpression(plugin) &&
      plugin.name.text === 'plugin' &&
      isEngine(checker.getTypeAtLocation(plugin.expression))
    )
  }

  /** The one argument of a call of the mod's function `name` (declared in `req.run.dir_file`), or of a const that holds
   * one, or null when `node` is not such a call. */
  function argumentOfModCall(node, name) {
    node = unwrap(node)
    const call = ts.isIdentifier(node) ? constInitializer(node) : node
    if (call === null || !ts.isCallExpression(call) || !ts.isIdentifier(call.expression)) return null
    if (call.expression.text !== name || call.arguments.length !== 1) return null
    let symbol = checker.getSymbolAtLocation(call.expression)
    if (symbol !== undefined && symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol)
    const home = norm(`${mod}/${req.run.dir_file}`)
    return symbol?.declarations?.some(decl => norm(decl.getSourceFile().fileName) === home) ? call.arguments[0] : null
  }

  /** Whether `node` is the checkout the mod sits in: `checkoutOf(<engine>.plugin.root)`, or a const that holds it. */
  function isCheckout(node) {
    const arg = argumentOfModCall(node, req.run.dir_function)
    return arg !== null && isPluginRoot(arg)
  }

  /** Whether `node` is the private Python environment of that checkout: `venvOf(<checkout>)`, or a const that holds it. */
  function isVenv(node) {
    const arg = argumentOfModCall(node, req.run.venv_function)
    return arg !== null && isCheckout(arg)
  }

  /** Whether the snapshot command's project folder is exactly `${<checkout>}/python`. */
  function isProjectFolder(item) {
    if (!ts.isTemplateExpression(item) || item.head.text !== '' || item.templateSpans.length !== 1) return false
    const [span] = item.templateSpans
    return span.literal.text === '/python' && isCheckout(span.expression)
  }

  const NOT_THE_COMMAND = 'runs a command other than `coney-tools conclave snapshot --json` through $.process.run'
  const NOT_THE_FOLDER =
    'runs the snapshot command with a project folder other than exactly `${checkoutOf($.plugin.root)}/python`, the checkout the mod sits in'

  /** What is wrong with a command array, or null when it is `uv run -q --project <dir> coney-tools conclave snapshot --json`. */
  function snapshotProblem(array) {
    const items = array.elements
    if (items.length !== req.run.head.length + 1 + req.run.tail.length || items.some(ts.isSpreadElement)) return NOT_THE_COMMAND
    const same = (item, text) => ts.isStringLiteralLike(item) && item.text === text
    const isCommand =
      req.run.head.every((text, i) => same(items[i], text)) &&
      req.run.tail.every((text, i) => same(items[req.run.head.length + 1 + i], text))
    if (!isCommand) return NOT_THE_COMMAND
    return isProjectFolder(items[req.run.head.length]) ? null : NOT_THE_FOLDER
  }

  /** Whether `node` is an object literal of the options `$.process.run` may be given (a spread only of a choice of such). */
  function isRunOptions(node) {
    node = unwrap(node)
    if (!ts.isObjectLiteralExpression(node)) return false
    return node.properties.every(member => {
      if (ts.isSpreadAssignment(member)) {
        const choice = unwrap(member.expression)
        return ts.isConditionalExpression(choice) && isRunOptions(choice.whenTrue) && isRunOptions(choice.whenFalse)
      }
      if (!ts.isPropertyAssignment(member) || !(ts.isIdentifier(member.name) || ts.isStringLiteral(member.name))) return false
      if (!req.run.options.includes(member.name.text)) return false
      // The folder is the checkout and the environment its own: no other folder, no other Python.
      if (member.name.text === 'cwd') return isCheckout(member.initializer)
      if (member.name.text !== 'env') return true
      const env = plainProperties(member.initializer)
      return env !== null && [...env].every(([key, value]) => req.run.env.includes(key) && isVenv(value))
    })
  }

  /** The `return` statements of a function body, leaving out those of the functions inside it. */
  function returnsOf(body) {
    const found = []
    const visit = n => {
      if (ts.isFunctionLike(n) || ts.isClassLike(n)) return
      if (ts.isReturnStatement(n)) found.push(n)
      ts.forEachChild(n, visit)
    }
    ts.forEachChild(body, visit)
    return found
  }

  /** The `await` expressions of a function body (or of an expression body), leaving out those of the functions inside it. */
  function awaitsOf(body) {
    const found = []
    const visit = n => {
      if (n !== body && (ts.isFunctionLike(n) || ts.isClassLike(n))) return
      if (ts.isAwaitExpression(n)) found.push(n)
      ts.forEachChild(n, visit)
    }
    visit(body)
    return found
  }

  /** The declaration of the mod's own function that `callee` names (a function, or a const holding one), or null. */
  function ownFunction(callee) {
    callee = unwrap(callee)
    if (!ts.isIdentifier(callee)) return null
    let symbol = checker.getSymbolAtLocation(callee)
    if (symbol !== undefined && symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol)
    const decl = symbol?.valueDeclaration
    if (decl === undefined || !isOwn(decl)) return null
    if (ts.isFunctionDeclaration(decl)) return decl.body === undefined ? null : decl
    const init = constInitializer(callee)
    return init === null ? null : inlineFunction(init)
  }

  /** Whether waiting for `node` could take any time: a promise, or a value whose type is unknown to the check. */
  function isThenable(node) {
    const t = checker.getTypeAtLocation(node)
    const one = part => (part.flags & (ts.TypeFlags.Any | ts.TypeFlags.Unknown)) !== 0 || checker.getPropertyOfType(part, 'then') !== undefined
    return t.isUnion() ? t.types.some(one) : one(t)
  }

  /**
   * Whether awaiting `node` waits only for the engine's own quick calls: an engine call other than `$.process.run`,
   * one of claude-code's functions, or a function of the mod's own whose every await and returned promise is such
   * a call in turn. A promise built in place, a timer or the snapshot command is a wait of unknown length.
   */
  function waitsBriefly(node, depth = 0, seen = new Set()) {
    node = unwrap(node)
    if (!isThenable(node)) return true
    if (depth > 8 || !ts.isCallExpression(node)) return false
    const callee = unwrap(node.expression)
    if (ts.isPropertyAccessExpression(callee)) {
      const noun = unwrap(callee.expression)
      return (
        ts.isPropertyAccessExpression(noun) &&
        isEngine(checker.getTypeAtLocation(noun.expression)) &&
        `${noun.name.text}.${callee.name.text}` !== 'process.run'
      )
    }
    if (isEngineFunction(callee)) return true
    const fn = ownFunction(callee)
    if (fn === null) return false
    if (seen.has(fn)) return true
    seen.add(fn)
    if (!ts.isBlock(fn.body)) return waitsBriefly(fn.body, depth + 1, seen)
    return (
      awaitsOf(fn.body).every(one => waitsBriefly(one.expression, depth + 1, seen)) &&
      returnsOf(fn.body).every(one => one.expression === undefined || waitsBriefly(one.expression, depth + 1, seen))
    )
  }

  /** Whether `id`, a use of a hook's `e`, only reads the event: `e.x` read, or `e` handed to one of the pass-ons. */
  function readsEvent(id, passOns) {
    const parent = id.parent
    if (ts.isCallExpression(parent) && passOns.includes(parent) && parent.arguments.includes(id)) return true
    if (!(ts.isPropertyAccessExpression(parent) && parent.expression === id)) return false
    let outer = parent
    while ((ts.isPropertyAccessExpression(outer.parent) || ts.isElementAccessExpression(outer.parent)) && outer.parent.expression === outer) {
      outer = outer.parent
    }
    const use = outer.parent
    const isAssignment = kind => kind >= ts.SyntaxKind.FirstAssignment && kind <= ts.SyntaxKind.LastAssignment
    if (ts.isBinaryExpression(use) && use.left === outer && isAssignment(use.operatorToken.kind)) return false
    if (ts.isDeleteExpression(use) || ts.isPrefixUnaryExpression(use) || ts.isPostfixUnaryExpression(use)) return false
    if (ts.isCallExpression(use) && use.expression === outer) return false
    // A part of the event goes only to the mod's own functions, which this check reads.
    if ((ts.isCallExpression(use) || ts.isNewExpression(use)) && use.arguments?.includes(outer)) {
      return ts.isCallExpression(use) && ownFunction(use.expression) !== null
    }
    return true
  }

  /**
   * What a hook does with its `e` and `next` besides passing the event on, as [node, why] pairs: `next` may only be
   * the callee of the pass-ons found (`passOns`), so it runs the hooks beneath once, and `e` may only be read or
   * passed to them, so they get the event as it came. Nested functions are read too.
   */
  function hookMisuses(fn, ev, next, passOns) {
    const found = []
    const own = new Set([fn.parameters[1]?.name, fn.parameters[2]?.name])
    const visit = n => {
      if (ts.isIdentifier(n) && !own.has(n)) {
        const shorthand = ts.isShorthandPropertyAssignment(n.parent) && n.parent.name === n
        const symbol = shorthand ? checker.getShorthandAssignmentValueSymbol(n.parent) : checker.getSymbolAtLocation(n)
        if (symbol === next && !(ts.isCallExpression(n.parent) && n.parent.expression === n && passOns.includes(n.parent))) {
          found.push([n, 'uses `next` other than as the one pass-on, next(e): a second call, next.to or a copy runs the hooks beneath again or past them'])
        }
        if (symbol === ev && !readsEvent(n, passOns)) {
          found.push([n, 'changes or hands on `e` before it is passed on: the hooks beneath must get the event as it came'])
        }
      }
      ts.forEachChild(n, visit)
    }
    visit(fn.body)
    return found
  }

  /** Whether `node` is a const of the mod's own holding an object literal of plain `key: literal` members. */
  function isTable(node) {
    node = unwrap(node)
    const init = ts.isIdentifier(node) ? constInitializer(node) : null
    if (init === null || !ts.isObjectLiteralExpression(init) || !isOwn(init)) return false
    const isLiteral = value =>
      ts.isStringLiteralLike(value) ||
      ts.isNumericLiteral(value) ||
      [ts.SyntaxKind.TrueKeyword, ts.SyntaxKind.FalseKeyword, ts.SyntaxKind.NullKeyword].includes(value.kind) ||
      (ts.isIdentifier(value) && value.text === 'undefined')
    return init.properties.every(
      member =>
        ts.isPropertyAssignment(member) &&
        (ts.isIdentifier(member.name) || ts.isStringLiteral(member.name)) &&
        isLiteral(member.initializer),
    )
  }

  /**
   * Whether `access` reads a value out of a table of the mod's own (or a choice of two): `COLORS[tone]`. Whatever the
   * key, it reaches a literal or a member every plain object has, and the value is only used, never read further,
   * called or written.
   */
  function isLookup(access) {
    const object = unwrap(access.expression)
    const tables = ts.isConditionalExpression(object) ? [object.whenTrue, object.whenFalse] : [object]
    if (!tables.every(isTable)) return false
    let outer = access
    while (outer.parent !== undefined && isWrapper(outer.parent)) outer = outer.parent
    const use = outer.parent
    const isAssignment = kind => kind >= ts.SyntaxKind.FirstAssignment && kind <= ts.SyntaxKind.LastAssignment
    if ((ts.isPropertyAccessExpression(use) || ts.isElementAccessExpression(use)) && use.expression === outer) return false
    if ((ts.isCallExpression(use) || ts.isNewExpression(use) || ts.isTaggedTemplateExpression(use)) && (use.expression ?? use.tag) === outer) return false
    if (ts.isBinaryExpression(use) && use.left === outer && isAssignment(use.operatorToken.kind)) return false
    if (ts.isDeleteExpression(use) || ts.isPrefixUnaryExpression(use) || ts.isPostfixUnaryExpression(use)) return false
    return true
  }

  /** The function an argument is, if it is one written in place. */
  function inlineFunction(node) {
    node = unwrap(node)
    return ts.isArrowFunction(node) || ts.isFunctionExpression(node) ? node : null
  }

  /** The symbol of the identifier parameter `index` of `fn`, or undefined. */
  function paramSymbol(fn, index) {
    const param = fn.parameters[index]
    return param !== undefined && ts.isIdentifier(param.name) ? checker.getSymbolAtLocation(param.name) : undefined
  }

  /**
   * Whether `expr` ends in `next(e)`: the call itself, awaited or not, or a choice between such. With `refusal` it may also
   * be the literal `{ isOffered: false }`, the one answer that hides an agent type.
   */
  function isPassOn(expr, next, ev, refusal, found = []) {
    expr = unwrap(expr)
    if (ts.isAwaitExpression(expr)) return isPassOn(expr.expression, next, ev, refusal, found)
    if (ts.isCallExpression(expr)) {
      const callee = unwrap(expr.expression)
      const arg = expr.arguments[0]
      const isCall =
        ts.isIdentifier(callee) &&
        checker.getSymbolAtLocation(callee) === next &&
        expr.arguments.length === 1 &&
        ts.isIdentifier(arg) &&
        checker.getSymbolAtLocation(arg) === ev
      if (isCall) found.push(expr)
      return isCall
    }
    if (ts.isConditionalExpression(expr)) {
      return isPassOn(expr.whenTrue, next, ev, refusal, found) && isPassOn(expr.whenFalse, next, ev, refusal, found)
    }
    if (refusal) {
      const props = plainProperties(expr)
      return props !== null && props.size === 1 && props.get('isOffered')?.kind === ts.SyntaxKind.FalseKeyword
    }
    return false
  }

  // A hook that passes the event on: every path ends in `next(e)` (or, for `agent.offer`, refuses an agent type).
  function passingHookProblems(fn, label, refusal) {
    const problems = []
    const err = (node, why) => problems.push([node, why])
    const ev = paramSymbol(fn, 1)
    const next = paramSymbol(fn, 2)
    const ending = `${label} must end every path in next(e)${refusal ? ' or in { isOffered: false }' : ''}, as a function of ($, e, next)`
    if (ev === undefined || next === undefined) return (err(fn, ending), problems)
    const body = fn.body
    const results = ts.isBlock(body) ? returnsOf(body).map(one => one.expression ?? one) : [body]
    const last = ts.isBlock(body) ? body.statements.at(-1) : undefined
    if (ts.isBlock(body) && !(last !== undefined && (ts.isReturnStatement(last) || ts.isThrowStatement(last)))) err(body, ending)
    const passOns = []
    for (const result of results) if (!isPassOn(result, next, ev, refusal, passOns)) err(result, ending)
    for (const [node, why] of hookMisuses(fn, ev, next, passOns)) err(node, `${label} ${why}`)
    // A session start waits for every hook: before it passes the event on, Conclave waits only for the engine's quick calls.
    if (!label.endsWith('session.start')) return problems
    for (const one of awaitsOf(body)) {
      if (!passOns.includes(unwrap(one.expression)) && !waitsBriefly(one.expression)) {
        err(one, `${label} waits for something other than the engine's quick calls (the snapshot command, a promise of its own) before it passes the event on`)
      }
    }
    return problems
  }

  // `classic.SessionStart` reads the session's agent type and passes the answer of the hooks beneath (Tether's, with the
  // HANDOFF context) on whole: it begins `const r = await next(e)` and only ever returns `r`.
  function sessionStartProblems(fn) {
    const problems = []
    const err = (node, why) => problems.push([node, why])
    const ev = paramSymbol(fn, 1)
    const next = paramSymbol(fn, 2)
    const bad = () => err(fn, 'classic.SessionStart must begin with `const r = await next(e)` and return only r, so the answer beneath it stays whole')
    const body = fn.body
    if (ev === undefined || next === undefined || !ts.isBlock(body)) return (bad(), problems)
    const first = body.statements[0]
    const decl = first !== undefined && ts.isVariableStatement(first) ? first.declarationList.declarations[0] : undefined
    const isConst = first !== undefined && ts.isVariableStatement(first) && (first.declarationList.flags & ts.NodeFlags.Const) !== 0
    const awaited = decl?.initializer === undefined ? undefined : unwrap(decl.initializer)
    const isFirst =
      isConst &&
      first.declarationList.declarations.length === 1 &&
      ts.isIdentifier(decl.name) &&
      awaited !== undefined &&
      ts.isAwaitExpression(awaited) &&
      ts.isCallExpression(unwrap(awaited.expression)) &&
      isPassOn(awaited.expression, next, ev, false)
    if (!isFirst) return (bad(), problems)
    for (const [node, why] of hookMisuses(fn, ev, next, [unwrap(awaited.expression)])) err(node, `classic.SessionStart ${why}`)
    const answer = checker.getSymbolAtLocation(decl.name)
    const last = body.statements.at(-1)
    if (!(ts.isReturnStatement(last) && body.statements.length > 1)) return (bad(), problems)
    const ownReturns = new Set(returnsOf(body))
    for (const one of ownReturns) {
      const value = one.expression === undefined ? undefined : unwrap(one.expression)
      if (value === undefined || !ts.isIdentifier(value) || checker.getSymbolAtLocation(value) !== answer) return (bad(), problems)
    }
    // The answer is only ever returned, by the hook itself: not copied, changed, handed on or returned by a function inside.
    const visit = node => {
      const isOwnReturn = ts.isReturnStatement(node.parent) && ownReturns.has(node.parent)
      if (ts.isIdentifier(node) && node !== decl.name && checker.getSymbolAtLocation(node) === answer && !isOwnReturn) bad()
      ts.forEachChild(node, visit)
    }
    visit(body)
    return problems
  }

  return {
    unwrap, isName, plainProperties, constText, NOT_THE_COMMAND, snapshotProblem, isRunOptions, inlineFunction,
    passingHookProblems, sessionStartProblems, isLookup,
  }
}
