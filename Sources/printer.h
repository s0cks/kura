#ifndef KURA_PRINTER_H
#define KURA_PRINTER_H

#include <ostream>

#include "common.h"

namespace kura {
class Printer {
 private:
  std::ostream& stream_;
  Indent indent_;

 protected:
  Printer(std::ostream& stream, const Indent indent) :
    stream_(stream),
    indent_(std::move(indent)) {}

 public:
  ~Printer() = default;

  inline auto stream() const -> std::ostream& {
    return stream_;
  }

  inline auto indent() const -> const Indent& {
    return indent_;
  }

  auto out() const -> std::ostream& {
    return stream() << indent();
  }
};

template <typename T>
class ToStringPrinter : Printer, public TemplateVisitor<T> {
 public:
  ToStringPrinter(std::ostream& stream, const Indent indent = {}) :
    Printer(stream, std::move(indent)),
    TemplateVisitor<T>() {}
  ~ToStringPrinter() override = default;

  auto Visit(T* rhs) -> VisitResult override {
    Printer::out() << rhs->ToString() << std::endl;
    return VisitResult::kContinue;
  }
};
}  // namespace kura

#endif  // KURA_PRINTER_H
