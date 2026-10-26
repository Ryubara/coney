// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's own description of the part of Claude Code's plugin API (the module 'claude-code') that the Conclave boundary
// check needs (docs/guides/conclave.md#boundary). It stands in for the types a session that loads the mod writes
// beside it, which only a logged-in Claude Code produces, so that the check runs where there is none (CI).
//
// It is not a copy of anything: it names the handful of types and functions the mod imports and the engine calls the
// mod makes, and gives each the shape the check reasons with:
//  - `EngineInterface` is the `$` of a hook. A hidden member makes it a type nothing else can be assigned to, as the
//    engine's own is, so "is this value the engine?" has one answer.
//  - `On` is the registrar `register` is given: a pattern, an optional filter and a hook of ($, e, next); a hook
//    returns a registration that has a `catch`.
//  - the nouns are the ones the mod calls (clock, command, process, prompt, store, ui, plugin) and nothing else: any other
//    noun is not in the list the check allows, whether or not this stub names it.
// This stub only needs to agree with the engine's types on what the check looks at, which
// python/tests/test_conclave_engine_stub.py verifies against the real ones whenever they are present, so it cannot
// drift unseen; type errors are not the check's to report when it reads this stub.

declare module 'claude-code' {
  /** What a hook is given as `$`; the hidden member keeps every other value from being one. */
  const engine: unique symbol

  export interface EngineInterface {
    readonly [engine]: true
    readonly clock: {
      every(milliseconds: number, run: () => void): unknown
      now(): number
    }
    readonly command: {
      register(command: { name: string; description?: string }): Promise<void>
    }
    readonly process: {
      run(
        argv: string[],
        options?: { cwd?: string; timeoutMs?: number; env?: Record<string, string> },
      ): Promise<{ exitCode: number; stdout: string; stderr: string }>
    }
    readonly prompt: {
      read(): Promise<{ text: string; cursor: number }>
      submit(input: { text: string }): Promise<{ text?: string; drop?: string }>
    }
    readonly store: {
      get(key: string): Promise<unknown>
      set(key: string, value: unknown): Promise<void>
      delete(key: string): Promise<void>
    }
    readonly ui: {
      open(pane: { id: string; title?: string }): Promise<unknown>
      panes(): Promise<unknown[]>
      resolve(input: unknown): Promise<unknown>
      status(text: string): Promise<void>
      toast(text: string): void
    }
    readonly plugin: { readonly root: string }
  }

  /** What a hook gets besides `$`: the event, as the engine describes it. */
  type Event = Record<string, unknown>

  /** Calls the hooks beneath this one and answers what they answered. */
  type Next = (event: Event) => Promise<unknown>

  /** A hook: it may answer for itself or pass the event on with `next`. */
  type Hook = ($: EngineInterface, event: Event, next: Next) => unknown

  /** What `on(...)` returns: the registration, whose `catch` is the hook that runs when this one fails. */
  type Registration = { catch(hook: Hook): Registration }

  /** The registrar: a pattern, an optional filter on the event's inputs, and the hook. */
  export type On = {
    (pattern: string, hook: Hook): Registration
    (pattern: string, filter: object, hook: Hook): Registration
  }

  /** The options a plugin is given when it is activated. */
  type PluginOptions = Record<string, unknown>

  /** The entry of a hooks module: `export const register: Register = on => { ... }`. */
  export type Register = (on: On, options: PluginOptions) => unknown

  /** A node of what a `ui.render` hook draws. */
  export type RenderElement = string | { type: string; props?: Record<string, unknown>; children?: unknown[] }

  /** What a `ui.render` hook is given, for a component on a surface. */
  export type RenderInputOf<Component extends string = string, Surface extends string = string> = {
    surface: Surface
    component: Component
    requestId: string
    [key: string]: unknown
  }

  /** What a `ui.render` hook is given, for any component and surface. */
  export type RenderInput<Component extends string = string, Surface extends string = string> = RenderInputOf<
    Component,
    Surface
  >

  /** What `atom` declares: one entry of the mod's state, with the value it holds before anything is stored. */
  type Atom<Value> = { readonly initial: Value }

  /** Declares one entry of the mod's state under its plugin and key, starting from `initial`. */
  export function atom<Value>(name: { plugin: string; key: string }, initial: Value): Atom<Value>

  /** The current value of an entry of the mod's state, through `$`. */
  export function read<Value>($: EngineInterface, atom: Atom<Value>): Promise<Value>

  /** Replaces an entry of the mod's state with what `change` makes of its current value. */
  export function update<Value>($: EngineInterface, atom: Atom<Value>, change: (value: Value) => Value): Promise<void>
}
