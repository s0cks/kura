# Kura

> A declarative, functional DSL for real-time game engine UIs

![Brand](assets/brand.png)

Kura combines functional composition and expressive layout primitives with a compiler-driven
UI architecture and deterministic rendering pipeline.

***Warning***: Under active development. Expect breaking changes and evolving ABIs

Kura treats UI as a compile-time data structure that gets transformed into a deterministic and reactive
render graph for execution in game engines at native speeds.

## Why Kura?

Designed for declarative construction of UIs in real-time game engines
where performance and predictability matter.

## Hello Kura

A "Hello World" in Kura looks like this:

```kura
fn init() => { message: "Hello World" }

fn view(msg, state) =>
  #text(state.message)
  #button(on-click: Close){ // Send a Close Message to the runtime to close the UI
    #text("Close")
  }
```

## Examples

See [examples/](examples/)

## Wiki

For Language Design, Architecture Details and Compiler Design, see the [wiki](https://github.com/s0cks/kura/wiki).

## Blog

Check the development [blog](https://kura.tazz.codes) for updates, architectural discussions and more information.

## Influenced By

- Functional languages like: Elm, Haskell and Jsonnet
- UI Frameworks like: Flutter, React, ImGui
- Typesetting languages like: Typst

## License

See [LICENSE](LICENSE).
