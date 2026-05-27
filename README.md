# Kura

> A functional language for game engine UIs.

![Brand](assets/brand.png)

Kura is a declarative, purely functional DSL written for building and scripting user interfaces in game engines.

Inspired by Elm, Typst, Haskell, and Flutter, Kura combines compositional functional programming with
expressive layout primitives and deterministic rendering.

```kui
view Main =
  column [
    text title "Kura"

    button[
      text "Start Game"
    ]

    button[
      text "Settings"
    ]
  ]
```

## Philosophy

Kura aims for the following principles:

- **Purely functional** --- UI is a pure function of state.
- **Engine-friendly** --- integrates cleanly into native game runtimes.
- **Composable layouts** --- interfaces are built from small reusable primitives.
- **Deterministic rendering** --- no hidden mutation or implicit side effects.

## Example

```kui
model = 
  {
    count: 0
  }

update msg model =
  match msg
    Increment ->
      { count: model.count + 1 }

    Decrement ->
      { count: model.count - 1 }

view model =
  column(align: center)[
    text(model.count)
    row(align: center)[
      button(on-click: Decrement)[
        text "-"
      ]
      button(on-click: Increment)[
        text "+"
      ]
    ]
  ]
```

## Influences

Kura draws inspiration from:

- Elm
- Haskell
- Typst
- React
- ImGui

## Goals

- Bring modern functional UI architecture to game engines
- Make UI logic easy to test and reason about
- Enable fast iteration without sacrificing performance
- Provide elegant layout and typography primitives
- Keep the runtime lightweight and embeddable
- Preserve deterministic behavior across platforms

## Non-Goals

Kura is not:

- a general-purpose systems language
- a replacement for engine scripting
- an ECS framework
- a visual node editor
- a browser runtime

## Status

Kura is currently experimental and under active development.

Expect rapid iteration, breaking changes, and evolving APIs.

## Vision

Kura aims to become a modern foundation for UI composition in game engines:

- expressive like Typst
- safe like Elm
- composable like Haskell
- practical for real-time systems
- lightweight enough for native engines

## Wiki

See the [wiki](https://github.com/s0cks/kura/wiki) for more information.

## Blog

Check the development [blog](https://kura.tazz.codes) for updates, architectural discussions and more information.

## License

See [LICENSE](LICENSE).
