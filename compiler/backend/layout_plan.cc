#include "backend/layout_plan.h"

#include <atomic>
#include <cstring>

#include "object.h"

namespace kura {
namespace {
std::atomic<uint64_t> next_index{0};

auto NumberOf(Object* value, double* out) -> bool {
  switch (value->GetType()) {
    case kIntType:
      *out = static_cast<double>(static_cast<Int*>(value)->GetValue());
      return true;
    case kFloatType:
      *out = static_cast<Float*>(value)->GetValue();
      return true;
    case kNumberType:
      *out = static_cast<Number*>(value)->GetValue();
      return true;
    default:
      return false;
  }
}

// A length: `12px`, `50%`, `auto`, or a bare number, which is pixels.
auto LengthOf(Object* value, layout::Length* out) -> bool {
  double number = 0;
  switch (value->GetType()) {
    case kPxType:
      *out = layout::Length::Px(static_cast<float>(static_cast<Px*>(value)->GetValue()));
      return true;
    case kPctType:
      *out = layout::Length::Pct(static_cast<float>(static_cast<Pct*>(value)->GetValue()));
      return true;
    case kStringType:
      if (static_cast<String*>(value)->GetValue() != "auto")
        return false;
      *out = layout::Length::Auto();
      return true;
    default:
      if (!NumberOf(value, &number))
        return false;
      *out = layout::Length::Px(static_cast<float>(number));
      return true;
  }
}

auto StringOf(Object* value) -> std::string {
  return value->GetType() == kStringType ? static_cast<String*>(value)->GetValue() : std::string{};
}

auto AlignOf(const std::string& name, layout::Align* out) -> bool {
  if (name == "start")
    *out = layout::Align::kStart;
  else if (name == "center")
    *out = layout::Align::kCenter;
  else if (name == "end")
    *out = layout::Align::kEnd;
  else if (name == "stretch")
    *out = layout::Align::kStretch;
  else
    return false;
  return true;
}

}  // namespace

auto LayoutPlan::PropertyOf(const std::string_view name) -> std::optional<layout::Property> {
  using P = layout::Property;
  static constexpr std::pair<std::string_view, P> kProperties[] = {
      {"width", P::kWidth},       {"height", P::kHeight},     {"min-width", P::kMinWidth},
      {"min-height", P::kMinHeight}, {"max-width", P::kMaxWidth}, {"max-height", P::kMaxHeight},
      {"basis", P::kBasis},       {"padding", P::kPadding},   {"margin", P::kMargin},
      {"gap", P::kGap},           {"direction", P::kDirection}, {"align", P::kAlign},
      {"align-self", P::kAlignSelf}, {"justify", P::kJustify}, {"wrap", P::kWrap},
      {"grow", P::kGrow},         {"shrink", P::kShrink},
  };
  for (const auto& [candidate, property] : kProperties) {
    if (candidate == name)
      return property;
  }
  return std::nullopt;
}

auto LayoutPlan::Convert(dom::Node* node, layout::Style* style, const layout::NodeId id, std::string* error) -> bool {
  const auto tag = "#" + std::string(node->GetTag());
  for (const auto& property : node->GetProperties()) {
    if (property.def->event)
      continue;

    const std::string name(property.def->name);
    if (name == "size" && node->IsText()) {
      double size = 0;
      if (!property.IsConstant() || !NumberOf(property.constant, &size)) {
        if (property.IsConstant() && property.constant->GetType() == kPxType) {
          font_sizes_[id] = static_cast<Px*>(property.constant)->GetValue();
          continue;
        }
        *error = tag + " 'size' has to be a constant for now: text is measured before the view runs";
        return false;
      }
      font_sizes_[id] = size;
      continue;
    }

    if (name == "font-family" && node->IsText()) {
      // Like `size`, it has to be known when the text is measured, which is before the view runs.
      if (!property.IsConstant() || property.constant->GetType() != kStringType) {
        *error = tag + " 'font-family' has to be a constant string for now: text is measured before the view runs";
        return false;
      }
      font_families_[id] = StringOf(property.constant);
      continue;
    }

    if (!PropertyOf(name))
      continue;

    // A value that's only known at run time is patched into the table before every layout; the table
    // starts with the default.
    if (!property.IsConstant())
      continue;

    const auto value = property.constant;
    const auto reject = [&]() {
      *error = tag + " '" + name + "' can't be laid out: it is " + value->ToString();
      return false;
    };
    layout::Length length{};
    double number = 0;

    if (name == "width" || name == "height" || name == "basis") {
      if (value->GetType() == kFrType) {
        // `1fr` is a share of the free space: it grows from nothing.
        style->grow = static_cast<float>(static_cast<Fr*>(value)->GetValue());
        style->basis = layout::Length::Px(0);
        continue;
      }
      if (!LengthOf(value, &length))
        return reject();
      (name == "width" ? style->width : name == "height" ? style->height : style->basis) = length;
    } else if (name == "min-width" || name == "min-height" || name == "max-width" || name == "max-height") {
      if (!LengthOf(value, &length))
        return reject();
      (name == "min-width"    ? style->min_width
       : name == "min-height" ? style->min_height
       : name == "max-width"  ? style->max_width
                              : style->max_height) = length;
    } else if (name == "padding") {
      if (!LengthOf(value, &length))
        return reject();
      style->padding = layout::Edges::All(length);
    } else if (name == "margin") {
      if (!LengthOf(value, &length))
        return reject();
      style->margin = layout::Edges::All(length);
    } else if (name == "gap") {
      if (!LengthOf(value, &length))
        return reject();
      style->gap = length;
    } else if (name == "grow" || name == "shrink") {
      if (!NumberOf(value, &number))
        return reject();
      (name == "grow" ? style->grow : style->shrink) = static_cast<float>(number);
    } else if (name == "direction") {
      const auto direction = StringOf(value);
      if (direction == "row")
        style->direction = layout::Direction::kRow;
      else if (direction == "column")
        style->direction = layout::Direction::kColumn;
      else if (direction == "row-reverse")
        style->direction = layout::Direction::kRowReverse;
      else if (direction == "column-reverse")
        style->direction = layout::Direction::kColumnReverse;
      else
        return reject();
    } else if (name == "align") {
      if (!AlignOf(StringOf(value), &style->align))
        return reject();
    } else if (name == "align-self") {
      if (!AlignOf(StringOf(value), &style->align_self))
        return reject();
    } else if (name == "justify") {
      const auto justify = StringOf(value);
      if (justify == "start")
        style->justify = layout::Justify::kStart;
      else if (justify == "center")
        style->justify = layout::Justify::kCenter;
      else if (justify == "end")
        style->justify = layout::Justify::kEnd;
      else if (justify == "space-between")
        style->justify = layout::Justify::kSpaceBetween;
      else if (justify == "space-around")
        style->justify = layout::Justify::kSpaceAround;
      else if (justify == "space-evenly")
        style->justify = layout::Justify::kSpaceEvenly;
      else
        return reject();
    } else if (name == "wrap") {
      if (value->GetType() != kBoolType)
        return reject();
      style->wrap = static_cast<Bool*>(value)->GetValue();
    }
  }
  return true;
}

auto LayoutPlan::Add(dom::Node* node, dom::Node* root, const layout::NodeId parent, std::string* error) -> bool {
  // a dynamic hole is a slot: a box-less node that the nodes it makes at run time go under
  if (node->IsDynamic()) {
    const auto id = static_cast<layout::NodeId>(nodes_.size());
    layout::Style style{};
    style.display = layout::Display::kContents;
    layout::AddNode(&nodes_, parent, style, false);
    ids_[node] = id;
    return true;
  }

  if (!node->IsElement())
    return true;

  const auto id = static_cast<layout::NodeId>(nodes_.size());
  layout::Style style{};
  style.display = node->IsFragment() && (node != root || template_) ? layout::Display::kContents : layout::Display::kFlex;
  if (!Convert(node, &style, id, error))
    return false;

  layout::AddNode(&nodes_, parent, style, node->IsText());
  ids_[node] = id;
  for (const auto child : node->GetChildren()) {
    if (!Add(child, root, id, error))
      return false;
  }
  return true;
}

auto LayoutPlan::Build(dom::Node* root, std::string* error) -> std::shared_ptr<LayoutPlan> {
  std::shared_ptr<LayoutPlan> plan(new LayoutPlan(next_index++));
  if (!plan->Add(root, root, layout::kNoNode, error))
    return nullptr;
  return plan;
}

auto LayoutPlan::BuildTemplate(dom::Node* root, std::string* error) -> std::shared_ptr<LayoutPlan> {
  std::shared_ptr<LayoutPlan> plan(new LayoutPlan(next_index++));
  plan->template_ = true;
  if (!plan->Add(root, root, layout::kNoNode, error))
    return nullptr;
  return plan;
}

auto LayoutPlan::GetBytes() const -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes(nodes_.size() * sizeof(layout::Node), 0);
  if (!nodes_.empty())
    std::memcpy(bytes.data(), nodes_.data(), bytes.size());
  return bytes;
}
}  // namespace kura
