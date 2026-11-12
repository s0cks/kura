#ifndef KURA_FRONTEND_BUILTINS_H
#define KURA_FRONTEND_BUILTINS_H

#include <string_view>
#include <vector>

#include "frontend/type_expr.h"

namespace kura {
// The types and messages every program has without declaring them, until there is a standard library to hold
// them. They are what the runtime and the host send to a UI about the world around it, and they are written
// here as the type declarations they would be in a Kura source file, so moving them into one is a copy:
//
//   type Size = { width: Float, height: Float }
//   type SystemMessage = Resized(Size) | Focused(Bool)
//
//   Resized(Size)   the viewport changed size, in the pixels the UI is drawn in
//   Focused(Bool)   the UI gained (true) or lost (false) the host's input focus
//
// A program receives the ones it asks for by listing them in its own Message type, either by name
// (`type Message = Increment | Resized(Size)`) or as a group (`type Message = Increment | SystemMessage`).
// One it doesn't list is not an error for the host to send: `update` just never sees it.
//
// A program can't declare a type of the same name with a different meaning, and can't give a built-in
// message a different payload: the host builds these values, and a program that expected something else would
// read them wrongly.
auto BuiltinTypeDecls() -> const std::vector<expr::TypeDecl*>&;

// The built-in variants, by name, with the name of the payload type each carries.
struct BuiltinMessage {
  std::string_view name;
  std::string_view payload;
};
auto BuiltinMessages() -> const std::vector<BuiltinMessage>&;
}  // namespace kura

#endif  // KURA_FRONTEND_BUILTINS_H
