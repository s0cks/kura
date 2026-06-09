---
title: "Exploring First Class UI"
date: "06/08/2026"
tags: [ "kura", "ImGui", "Nuklear" ]
description: "A discussion about all the ways you can over complicate UI engineering in game engines."
---

> **tldr**: Working on *my first game engine* and decided
> to over complicate how UIs get created - *for funzies*.

## In Over My Head™

Suppose you get working on your first game engine.<br/>
Vulkan exists now and so you think let's use that and then find out exactly how much
code it takes to create a single triangle on the screen and want to ~~immediately switch
back to GL where it's safer~~ learn more of this wonderful framework.

You continue on though.
Eventually, you get to a point where you need to start thinking about the user interfaces (UIs).

What do you do?

***I gave up***

*just kidding.*

Like any good engineer, I decided to find a good solution.

## Existing Solutions

In my case, my engine's stack consisted of:

- **C/C++** *w/ clang*
- **CMake** -- *for building*
- **Lua** -- *for scripting*
- **Vulkan & OpenGL** -- *graphics backends*

So what are some good solutions?

> I should probably say that by **good solution** I mean you
> should just use those frameworks - or methods, rather than
> anything I build.
> 
> ..for now

### ImGui

> [ImGui](https://github.com/ocornut/imgui) -- A self-described *'bloat-free' Graphical User Interface for C++ with minimal dependencies*.

The gold-standard library for C++ applications with a lot of supporters, contributors and industry backing.
I have used it in the past.
Works great.
Does exactly as advertised.

My only real complaint?
Sometimes, building large and complex UIs can become overwhelming.

I disagree on bloated - but maybe, a lot of UI frameworks often feel bloated inherently due to the nature of UIs themselves.

### Nuklear

> [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) -- Another immediate mode UI library.
> This time C flavored.

While not related to ImGui, Nuklear offers much of what ImGui offers but with the added constraints of
ANSI C and the benefit of a lighter footprint.

I often choose this for projects for the simplicity and lighter footprint, but along with those ANSI C constraints
Nuklear can also become overwhelming the more complex you make your UIs.

### EA WebKit

> [EA-WebKit](https://gpl.ea.com/eawebkit.html)) -- A special fork of the WebKit browser engine by Electronic Arts (EA)

To EA's credit, they created a fantastic solution.
Combine the flexibility and interactivity of a web environment with high-performance rendering for your game engines.

This imposes a whole host of things from security to memory implications though.

### A Brief Comparison

ImGui, Nuklear, EA-WebKit and plenty of others that I have missed.
All great options.
All have their benefits and trade-offs.

Naturally, I started thinking about the constraints I wanted my engine's UI to have.

Something like:

- Reasonable memory footprint -- not trying to create another google-chrome situation.
- Expressive with low boilerplate -- like Flutter but without dart and packaging concerns.

ImGui and Nuklear immediately violate the expressive and low-boilerplate constraint.
Both of them require a ton of boilerplate.

EA-WebKit still requires setup, but it also comes with the extra bloat - same with Flutter.

As a last-minute consideration, I started thinking about creating my own.
It would give me time to become more comfortable with Vulkan, improve my GLSL skills and finally move beyond OpenGL -
the whole point of creating my own engine.

### Custom

By creating my own I could tailor it to my needs and learn a ton about ~~how to make poor choices~~ what it takes
to create a good UI framework.

Maybe ~~finally prove to Bethesda that UIs ***are*** in-fact easy to scale on ultra-wide monitors~~
learn the pain that every Bethesda UI engineer feels when someone complains about how their obnoxiously large and
overpriced Samsung Odyssey has too many pixels for their games made in 2010.

With C++ and Vulkan, performance and footprint becomes my problems to solve.
<br/>
With my background in creating custom allocators and garbage collectors I don't fear this as much, but this still adds
significant architectural overhead to anything I decide to build.

This leaves the boilerplate discussion.

If I venture down this road of creating my own UI framework.
How do I manage the boilerplate responsibly?

Frankly, writing UI descriptions directly in C/C++ feels repetitive no matter how much abstraction I pile on top.

Yes macros exist.

You can definitely write more code to make writing code feel like writing less code.

More macros.
More templates.
More meta-programming.
More sugar.
More layers.
More abstractions.

This only adds more complexity, increases compilation time, has its own semantic trappings and most importantly will it ***just work**?

> 🫶 Bethesda.
> <br/>
> Of course your UI ***just works***.
> <br/>
> <br/>
> Can we please finally have *Fallout: New Vegas Remastered*?

## If Not Library Shape, Then Why Not Language Shape?

What if we ~~over-engineer the solution~~ add a description layer to abstract all the UI construction?

This approach has a few different ways of going about it, but ultimately falls short in different ways.

### Json

If we consider something like Json for our description code we get good structure and the ability to create
schema files that can leverage existing utilities to verify our description code.

The downside? We need to solve a couple different problems:

- We need to parse the Json and construct a UI from it.
- We need to able to handle dynamic UI components that change based internal game state only known at runtime.

This leads to weird builder patterns, dialect based parsers and weird evaluation techniques that only add more band-aids.

We could also add the complexity of Jsonnet on top of the Json - if we wanted more band-aids on our band-aids.

### Lua

We could try to use Lua - and this approach has different ways of working and seems perfectly reasonable.

1. We could have Lua as the description language. This becomes kind of the data description language that gets parsed like Json to construct a UI tree.
2. We could also just have Lua draw the UIs themselves and create utilities around that to call back to bridge to the main renderer.

Both perfectly valid - definitely with the existence of luajit and/or you have Lua already for scripting.

*like me*.

Hold on though.

Even if we choose the path of adopting Lua we inevitably start creating a UI vocabulary.
Something where we create primitives and reusable components.

Buttons.
Panels.
Text.
Lists.
Animations.

At some point we stop writing Lua and start writing a UI language disguised as Lua.
You even get the added benefit of adopting existing language conventions.
Gaining other language features, functionalities and constraints that have nothing to do with UIs.

## What Does a "UI Description Language" Even Mean?

***UI***:

- is structurally data -- the UI tree.
- presents data -- health bars, inventories, charts & visualizations, vertices & indices, etc

---

The game industry adopted Lua for data descriptors.
<br/>
Neovim practically invented using Lua as configuration DSL when it adopted Lua.
<br/>
Json is structured data with no control-flow.
<br/>
Jsonnet brought control-flow to Json's structured data for more flexibility.
<br/>
A health bar becomes easy to declare, but a dynamic inventory or menu becomes a nightmare of glue
and anti-patterns to make things work.

All that building on and adopting introduces more overhead, complexity and external influence.
What if the UI framework didn't adopt the language, it became the language?
Modern compilers eventually become effectively data flow optimizers - like clang, any optimizing compiler like Dart or the JVM.

UI at the core gets reduced to data structures.
<br/>
A button is data.
<br/>
Text is data.
<br/>
The render pipeline itself eventually becomes data.

If all that *data* can get analyzed, optimized and compiled.

Why force a UI framework into an existing language?

Why not build a language designed specifically for UIs?
