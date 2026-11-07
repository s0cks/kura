#ifndef KURA_C_HEADER_H
#define KURA_C_HEADER_H

#include <string>

namespace kura {
class Module;

// The C header a host includes to call a module: a prototype for every
// `extern fn` that has a body, and `kura_module_init`. `guard` names the
// include guard (and is derived from the file name by the driver).
auto GenerateCHeader(Module* module, const std::string& guard) -> std::string;
}  // namespace kura

#endif  // KURA_C_HEADER_H
