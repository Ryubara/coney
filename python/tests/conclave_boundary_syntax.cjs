// SPDX-License-Identifier: GPL-3.0-or-later
// The tree-reading helpers of the Conclave boundary check (conclave_boundary.cjs holds the rules): what an expression is
// made of, what a hook passes on and how, whether a command array is the snapshot command, what a string of constants
// comes to.
'use strict'

/**
 * The helpers, over one TypeScript program: `ts` and its `checker`, the request `req`, the mod's folder `mod`, the file
 * name normaliser `norm`, and the check's own tests: `isEngine` (a value of the engine's type), `isOwn` (a node in
 * one of the mod's checked sources) and `isEngineFunction` (a callee that is one of claude-code's own functions); and
 * the `program` with the names of the files (`runtimeFiles`) that make up the mod, for the checks that follow a function
 * to every place it is used.
 */
module.exports = function syntax({ ts, checker, req, mod, norm, program, runtimeFiles, isEngine, isOwn, isEngineFunction }) {
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

  /** Whether `node` is `[]`, or `[<flag>]` for the one optional flag of the command (`want`: which of the two). */
  function isFlagList(node, want) {
    node = unwrap(node)
    if (!ts.isArrayLiteralExpression(node)) return false
    if (!want) return node.elements.length === 0
    return node.elements.length === 1 && ts.isStringLiteralLike(node.elements[0]) && node.elements[0].text === req.run.optional.flag
  }

  /**
   * Whether `item` is the optional flag in its place: the string itself, or a spread of a choice between the flag and
   * nothing (`...(isSync ? [] : ['--no-sync'])`), whatever decides it: the choice adds nothing else.
   */
  function isOptionalFlag(item) {
    if (ts.isStringLiteralLike(item)) return item.text === req.run.optional.flag
    if (!ts.isSpreadElement(item)) return false
    const choice = unwrap(item.expression)
    if (!ts.isConditionalExpression(choice)) return false
    const { whenTrue: yes, whenFalse: no } = choice
    return (isFlagList(yes, false) && isFlagList(no, true)) || (isFlagList(yes, true) && isFlagList(no, false))
  }

  /** What is wrong with a command array, or null when it is `uv run -q [--no-sync] --project <dir> coney-tools conclave snapshot --json`. */
  function snapshotProblem(array) {
    const items = [...array.elements]
    const { at } = req.run.optional
    if (items[at] !== undefined && isOptionalFlag(items[at])) items.splice(at, 1)
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

  /**
   * The first statement of a hook that keeps the answer beneath it whole: `const r = await next(e)`. It returns the
   * declaration and the awaited call, or null when the hook does not begin so.
   */
  function answerDeclaration(body, ev, next) {
    if (ev === undefined || next === undefined || !ts.isBlock(body)) return null
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
    return isFirst ? { decl, call: unwrap(awaited.expression) } : null
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
    const first = answerDeclaration(body, ev, next)
    if (first === null) return (bad(), problems)
    const { decl } = first
    for (const [node, why] of hookMisuses(fn, ev, next, [first.call])) err(node, `classic.SessionStart ${why}`)
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

  // `classic.Stop` is the stop guard on pull request problems nobody owns. It keeps the answer beneath it (Tether's own
  // guard for stopped agents, with its block) whole and adds one thing, the reason a function of the mod's own gives,
  // asked only when no block is set beneath, so a block already there is never replaced or counted:
  //   const r = await next(e)
  //   const reason = r.block === undefined ? await guard($) : null
  //   return reason === null ? r : { ...r, block: reason }
  // `e` goes nowhere but `next`, nothing else is returned, and the guard is asked with the engine alone.
  function stopGuardProblems(fn) {
    const problems = []
    const err = (node, why) => problems.push([node, why])
    const host = paramSymbol(fn, 0)
    const ev = paramSymbol(fn, 1)
    const next = paramSymbol(fn, 2)
    const bad = () =>
      err(
        fn,
        "classic.Stop must begin with `const r = await next(e)`, ask one function of the mod's own for the reason only when no block is set beneath (`const reason = r.block === undefined ? await guard($) : null`) and return `reason === null ? r : { ...r, block: reason }`, so the answer beneath stays whole, a block set there is kept and the only thing added is the guard's block",
      )
    const body = fn.body
    const first = answerDeclaration(body, ev, next)
    if (first === null) return (bad(), problems)
    for (const [node, why] of hookMisuses(fn, ev, next, [first.call])) err(node, `classic.Stop ${why}`)
    const answer = checker.getSymbolAtLocation(first.decl.name)
    const [, ask, ending] = body.statements
    const is = (node, symbol) => ts.isIdentifier(unwrap(node)) && checker.getSymbolAtLocation(unwrap(node)) === symbol
    // The second statement asks the guard, with `$` alone, when the answer beneath has no block: `r.block === undefined`.
    const decl = ask !== undefined && ts.isVariableStatement(ask) ? ask.declarationList.declarations[0] : undefined
    const init = decl?.initializer === undefined ? undefined : unwrap(decl.initializer)
    const when = init !== undefined && ts.isConditionalExpression(init) ? unwrap(init.condition) : undefined
    const isNoBlock =
      when !== undefined &&
      ts.isBinaryExpression(when) &&
      when.operatorToken.kind === ts.SyntaxKind.EqualsEqualsEqualsToken &&
      ts.isPropertyAccessExpression(unwrap(when.left)) &&
      unwrap(when.left).name.text === 'block' &&
      is(unwrap(when.left).expression, answer) &&
      ts.isIdentifier(unwrap(when.right)) &&
      unwrap(when.right).text === 'undefined'
    const awaited = isNoBlock ? unwrap(init.whenTrue) : undefined
    const call = awaited !== undefined && ts.isAwaitExpression(awaited) ? unwrap(awaited.expression) : undefined
    const isAsk =
      body.statements.length === 3 &&
      (ask.declarationList.flags & ts.NodeFlags.Const) !== 0 &&
      ask.declarationList.declarations.length === 1 &&
      ts.isIdentifier(decl.name) &&
      isNoBlock &&
      unwrap(init.whenFalse).kind === ts.SyntaxKind.NullKeyword &&
      call !== undefined &&
      ts.isCallExpression(call) &&
      ownFunction(call.expression) !== null &&
      call.arguments.length === 1 &&
      ts.isIdentifier(unwrap(call.arguments[0])) &&
      host !== undefined &&
      checker.getSymbolAtLocation(unwrap(call.arguments[0])) === host
    if (!isAsk) return (bad(), problems)
    const reason = checker.getSymbolAtLocation(decl.name)
    // The last statement returns the choice between the answer beneath and the answer with the guard's block.
    const choice = ts.isReturnStatement(ending) && ending.expression !== undefined ? unwrap(ending.expression) : undefined
    const test = choice !== undefined && ts.isConditionalExpression(choice) ? unwrap(choice.condition) : undefined
    const added = choice !== undefined && ts.isConditionalExpression(choice) ? unwrap(choice.whenFalse) : undefined
    const members = added !== undefined && ts.isObjectLiteralExpression(added) ? added.properties : []
    const isChoice =
      test !== undefined &&
      ts.isBinaryExpression(test) &&
      test.operatorToken.kind === ts.SyntaxKind.EqualsEqualsEqualsToken &&
      is(test.left, reason) &&
      unwrap(test.right).kind === ts.SyntaxKind.NullKeyword &&
      is(choice.whenTrue, answer) &&
      members.length === 2 &&
      ts.isSpreadAssignment(members[0]) &&
      is(members[0].expression, answer) &&
      ts.isPropertyAssignment(members[1]) &&
      ts.isIdentifier(members[1].name) &&
      members[1].name.text === 'block' &&
      is(members[1].initializer, reason)
    if (!isChoice) return (bad(), problems)
    // The answer beneath is used three times, in the test for a block set beneath and in that choice, and nowhere else.
    let uses = 0
    const visit = node => {
      if (ts.isIdentifier(node) && node !== first.decl.name && checker.getSymbolAtLocation(node) === answer) uses += 1
      ts.forEachChild(node, visit)
    }
    visit(body)
    if (uses !== 3) bad()
    return problems
  }

  // A wake is sent from one place only: the event-wake path of the watchers (conclave.md#boundary). `$.prompt.submit` is
  // called as `{ text: <parameter> }` in a function declaration (the wake function) that has exactly one reference in the
  // mod, a call; that call sends the text of the events due, `wakeText(<preamble>, due)`, where `due` is a const that
  // `pending(...)` gave, and sits in the `then` of `if (due.length > 0)` in the function declaration that watches (no
  // closure, so no timer of its own); and the watcher has references that are calls, each in the callback given to
  // `startPolling`, which runs after a read. So no timer, session start or fixed text can send a wake that no event asked for.
  function wakeProblems(submit) {
    const problems = []
    const err = (node, why) => problems.push([node, why])
    const enclosing = n => {
      for (let p = n.parent; p !== undefined; p = p.parent) if (ts.isFunctionLike(p) || ts.isClassLike(p)) return p
      return null
    }
    const symbolOf = id => {
      let symbol = ts.isShorthandPropertyAssignment(id.parent) && id.parent.name === id ? checker.getShorthandAssignmentValueSymbol(id.parent) : checker.getSymbolAtLocation(id)
      if (symbol !== undefined && symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol)
      return symbol
    }
    // Whether `callee` names the function `name` of the mod's own (declared in a checked source, with or without a body).
    const ownNamed = (callee, name) => {
      const id = unwrap(callee)
      if (!ts.isIdentifier(id) || id.text !== name) return false
      const decl = symbolOf(id)?.declarations?.[0]
      return decl !== undefined && isOwn(decl)
    }
    // Every reference in the mod to the function declared by `decl`, but its own name and the import that names it.
    const referencesTo = decl => {
      const target = checker.getSymbolAtLocation(decl.name)
      const found = []
      for (const file of program.getSourceFiles()) {
        if (!runtimeFiles.has(norm(file.fileName))) continue
        const visit = n => {
          const inImport = n.parent !== undefined && (ts.isImportSpecifier(n.parent) || ts.isImportClause(n.parent) || ts.isNamespaceImport(n.parent))
          if (ts.isIdentifier(n) && n !== decl.name && !inImport && n.text === decl.name.text && symbolOf(n) === target) found.push(n)
          ts.forEachChild(n, visit)
        }
        visit(file)
      }
      return found
    }
    const isCallee = id => ts.isCallExpression(id.parent) && id.parent.expression === id

    // 1. The call: `{ text: <the last parameter> }` inside a function declaration.
    const wake = enclosing(submit)
    const props = submit.arguments.length === 1 ? plainProperties(submit.arguments[0]) : null
    const text = props?.get('text')
    const last = wake !== null && ts.isFunctionDeclaration(wake) && wake.name !== undefined ? wake.parameters.at(-1) : undefined
    const isText =
      text !== undefined &&
      last !== undefined &&
      ts.isIdentifier(last.name) &&
      ts.isIdentifier(unwrap(text)) &&
      checker.getSymbolAtLocation(unwrap(text)) === checker.getSymbolAtLocation(last.name)
    if (!isText) {
      err(submit, "$.prompt.submit may only be called as { text: <the text parameter> } inside a function declaration of the mod's own (the wake function) that takes the text, so a wake is never a fixed text or sent from a timer")
      return problems
    }

    // 2. The wake function is called once, and only called.
    const uses = referencesTo(wake)
    if (uses.length !== 1 || !isCallee(uses[0])) {
      err(wake.name, `the wake function ${wake.name.text} must be called exactly once, and only as a call: it is the one place a wake is sent from`)
      return problems
    }

    // 3. That call sends the events due, in the watcher, when something is due.
    const sending = uses[0].parent
    const watcher = enclosing(sending)
    const built = sending.arguments.length > 0 ? unwrap(sending.arguments.at(-1)) : undefined
    const dueId = built !== undefined && ts.isCallExpression(built) && ownNamed(built.expression, 'wakeText') && built.arguments.length === 2 ? unwrap(built.arguments[1]) : undefined
    const dueInit = dueId !== undefined && ts.isIdentifier(dueId) ? constInitializer(dueId) : null
    const isDue = dueInit !== null && ts.isCallExpression(dueInit) && ownNamed(dueInit.expression, 'pending')
    let isGuarded = false
    for (let n = sending.parent; isDue && n !== undefined && n !== watcher; n = n.parent) {
      if (!ts.isIfStatement(n) || !(sending.pos >= n.thenStatement.pos && sending.end <= n.thenStatement.end)) continue
      const test = unwrap(n.expression)
      const length = ts.isBinaryExpression(test) ? unwrap(test.left) : undefined
      if (
        ts.isBinaryExpression(test) &&
        test.operatorToken.kind === ts.SyntaxKind.GreaterThanToken &&
        ts.isNumericLiteral(unwrap(test.right)) &&
        unwrap(test.right).text === '0' &&
        ts.isPropertyAccessExpression(length) &&
        length.name.text === 'length' &&
        ts.isIdentifier(unwrap(length.expression)) &&
        checker.getSymbolAtLocation(unwrap(length.expression)) === checker.getSymbolAtLocation(dueId)
      ) {
        isGuarded = true
      }
    }
    if (!isDue || !isGuarded || watcher === null || !ts.isFunctionDeclaration(watcher) || watcher.name === undefined) {
      err(sending, `the wake must be built from the events due: ${wake.name.text}($, wakeText(<preamble>, due)) in the then of \`if (due.length > 0)\`, with \`const due = pending(...)\`, directly in a function declaration (a closure or a timer inside it is not the watcher)`)
      return problems
    }

    // 4. The watcher runs only from the callback of startPolling, which is called after a read.
    for (const use of referencesTo(watcher)) {
      let inside = null
      if (isCallee(use)) {
        for (let n = use.parent; n !== undefined && inside === null; n = n.parent) {
          const given = ts.isArrowFunction(n) && ts.isCallExpression(n.parent) && n.parent.arguments.includes(n) && ownNamed(n.parent.expression, 'startPolling')
          if (given) inside = n
          else if (ts.isFunctionLike(n) && !ts.isArrowFunction(n)) break
        }
      }
      if (inside === null) err(use, `${watcher.name.text}, which sends the wake, may only be called from the callback of startPolling (it runs after a read), not from a timer, a session start or a value passed on`)
    }
    return problems
  }

  return {
    unwrap, isName, plainProperties, constText, NOT_THE_COMMAND, snapshotProblem, isRunOptions, inlineFunction,
    passingHookProblems, sessionStartProblems, stopGuardProblems, wakeProblems,
  }
}
