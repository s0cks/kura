#ifndef KURA_PRINTER_H
#define KURA_PRINTER_H

#include <ostream>

#include "common.h"

namespace kura {
template <typename Stream = std::ostream>
class Printer {
 private:
  Stream& stream_;
  Indent indent_;

 protected:
  Printer(Stream& stream, const Indent indent) :
    stream_(stream),
    indent_(std::move(indent)) {}

 public:
  ~Printer() = default;

  inline auto stream() const -> Stream& {
    return stream_;
  }

  inline auto indent() const -> const Indent& {
    return indent_;
  }

  auto out() const -> Stream& {
    return stream() << indent();
  }
};

template <typename T, typename Stream = std::ostream>
class ToStringPrinter : Printer<Stream>, public TemplateVisitor<T> {
 public:
  ToStringPrinter(Stream& stream, const Indent indent = {}) :
    Printer<Stream>(stream, std::move(indent)),
    TemplateVisitor<T>() {}
  ~ToStringPrinter() override = default;

  auto Visit(T* rhs) -> VisitResult override {
    Printer<Stream>::out() << rhs->ToString() << std::endl;
    return VisitResult::kContinue;
  }
};
}  // namespace kura

#endif  // KURA_PRINTER_H
