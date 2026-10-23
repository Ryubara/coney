// SPDX-License-Identifier: GPL-3.0-or-later
// The tree-reading helpers of the Conclave boundary check (conclave_boundary.cjs holds the rules): what an expression is
// made of, what a hook returns, whether a command array is the snapshot command, what a string of constants comes to.
'use strict'

/**
 * The helpers, over one TypeScript program: `ts` and its `checker`, the request `req`, the mod's folder `mod`, the file
 * name normaliser `norm` and `isEngine`, the check's own test for a value of the engine's type.
 */
module.exports = function syntax({ ts, checker, req, mod, norm, isEngine }) {
  const NAME = req.name
  const isWrapper = n => n.kind === ts.SyntaxKind.ParenthesizedExpression || n.kind === ts.SyntaxKind.NonNullExpression

  /** `n` without the parentheses and `!` around it. */
  function unwrap(n) {
    while (isWrapper(n)) n = n.expression
    return n
  }

  /** Whether `node` is the string 'conclave' by type: the literal itself, or a const that holds it. */
  function isName(node) {
    const t = checker.getTypeAtLocation(node)
    return t.isStringLiteral() && t.value === NAME
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
    const decl = checker.getSymbolAtLocation(id)?.valueDeclaration
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

  /** Whether the snapshot command's project folder is `${checkoutOf(<engine>.plugin.root)}/...`, with the mod's own function. */
  function isProjectFolder(item) {
    if (!ts.isTemplateExpression(item) || item.head.text !== '' || item.templateSpans.length === 0) return false
    const first = unwrap(item.templateSpans[0].expression)
    const call = ts.isIdentifier(first) ? constInitializer(first) : first
    if (call === null || !ts.isCallExpression(call) || !ts.isIdentifier(call.expression)) return false
    if (call.expression.text !== req.run.dir_function || call.arguments.length !== 1 || !isPluginRoot(call.arguments[0])) return false
    let symbol = checker.getSymbolAtLocation(call.expression)
    if (symbol !== undefined && symbol.flags & ts.SymbolFlags.Alias) symbol = checker.getAliasedSymbol(symbol)
    const home = norm(`${mod}/${req.run.dir_file}`)
    return symbol?.declarations?.some(decl => norm(decl.getSourceFile().fileName) === home) === true
  }

  const NOT_THE_COMMAND = 'runs a command other than `coney-tools conclave snapshot --json` through $.process.run'
  const NOT_THE_FOLDER =
    'runs the snapshot command with a project folder other than `${checkoutOf($.plugin.root)}/python`, the checkout the mod sits in'

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
      if (member.name.text !== 'env') return true
      const env = plainProperties(member.initializer)
      return env !== null && [...env.keys()].every(key => req.run.env.includes(key))
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
  function isPassOn(expr, next, ev, refusal) {
    expr = unwrap(expr)
    if (ts.isAwaitExpression(expr)) return isPassOn(expr.expression, next, ev, refusal)
    if (ts.isCallExpression(expr)) {
      const callee = unwrap(expr.expression)
      const arg = expr.arguments[0]
      return (
        ts.isIdentifier(callee) &&
        checker.getSymbolAtLocation(callee) === next &&
        expr.arguments.length === 1 &&
        ts.isIdentifier(arg) &&
        checker.getSymbolAtLocation(arg) === ev
      )
    }
    if (ts.isConditionalExpression(expr)) return isPassOn(expr.whenTrue, next, ev, refusal) && isPassOn(expr.whenFalse, next, ev, refusal)
    if (refusal) {
      const props = plainProperties(expr)
      return props !== null && props.size === 1 && props.get('isOffered')?.kind === ts.SyntaxKind.FalseKeyword
    }
    return false
  }

  return { unwrap, isName, plainProperties, constText, NOT_THE_COMMAND, snapshotProblem, isRunOptions, returnsOf, inlineFunction, paramSymbol, isPassOn }
}
