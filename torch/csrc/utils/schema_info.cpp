#include <torch/csrc/utils/schema_info.h>

namespace torch {
namespace utils {
void SchemaInfo::addArgumentValue(
    const std::string& name,
    const at::IValue& value) {
  c10::optional<int> index = schema_.argumentIndexWithName(name);
  TORCH_INTERNAL_ASSERT(
      index != c10::nullopt, "Schema has no argument named ", name);
  value_map_[name] = flattenZeroDimIValue(value);
  updated_ = false;
}

void SchemaInfo::addArgumentValues(
    const std::vector<c10::optional<at::IValue>>& value_list) {
  for (size_t i = 0; i < value_list.size(); i++) {
    if (i < schema_.arguments().size() && value_list[i] != c10::nullopt) {
      value_map_[schema_.arguments()[i].name()] =
          flattenZeroDimIValue(*(value_list[i]));
      updated_ = false;
    }
  }
}

void SchemaInfo::addArgumentValues(
    const std::unordered_map<std::string, at::IValue>& values) {
  for (const auto& key_pair : values) {
    addArgumentValue(key_pair.first, key_pair.second);
  }
}

bool SchemaInfo::is_mutable() {
  for (size_t i = 0; i < schema_.arguments().size(); i++) {
    if (is_mutable(i)) {
      return true;
    }
  }
  return false;
}

bool SchemaInfo::is_mutable(size_t index) {
  TORCH_INTERNAL_ASSERT(
      index < schema_.arguments().size(), "Invalid index for schema.");
  if (!updated_) {
    generateAliasMaps();
  }
  return std::any_of(
      input_alias_map_[index].begin(),
      input_alias_map_[index].end(),
      [this](size_t index) { return this->schema_.is_mutable(index); });
}

bool SchemaInfo::is_mutable(c10::string_view name) {
  c10::optional<int> index = schema_.argumentIndexWithName(name);
  TORCH_INTERNAL_ASSERT(
      index != c10::nullopt, "Schema has no argument named ", name);

  return is_mutable(*index);
}

bool SchemaInfo::is_non_deterministic() const {
  static const std::vector<const char*> nondeterministic_ops = {
      "aten::dropout(Tensor input, float p, bool train) -> Tensor",
      "aten::_fused_dropout(Tensor self, float p, Generator? generator) -> (Tensor, Tensor)",
      "aten::_standard_gamma(Tensor self, Generator? generator) -> Tensor",
      "aten::bernoulli(Tensor self, *, Generator? generator) -> Tensor",
      "aten::bernoulli(Tensor self, float p, *, Generator? generator) -> Tensor",
      "aten::multinomial(Tensor self, int num_samples, bool replacement, *, Generator? generator) -> Tensor",
      "aten::native_dropout(Tensor input, float p, bool? train) -> (Tensor, Tensor)",
      "aten::normal(Tensor mean, Tensor std, *, Generator? generator) -> Tensor",
      "aten::normal(float mean, Tensor std, *, Generator? generator) -> Tensor",
      "aten::normal(Tensor mean, float std, *, Generator? generator) -> Tensor",
      "aten::poisson(Tensor self, Generator? generator) -> Tensor",
      "aten::binomial(Tensor count, Tensor prob, Generator? generator=None) -> Tensor",
      "aten::rrelu(Tensor self, Scalar lower, Scalar upper, bool training, Generator? generator) -> Tensor",
      "aten::rrelu_with_noise(Tensor self, Tensor noise, Scalar lower, Scalar upper, bool training, Generator? generator) -> Tensor",
      "aten::rand(int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::rand_like(Tensor self, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randint(int high, int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::randint(int low, int high, int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::randint_like(Tensor self, int high, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randint_like(Tensor self, int low, int high, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randn(int[] size, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor",
      "aten::randn_like(Tensor self, *, int? dtype=None, int? layout=None, Device? device=None, bool? pin_memory=None, MemoryFormat? memory_format=None) -> Tensor",
      "aten::randperm(int n, *, int? dtype, int? layout, Device? device, bool? pin_memory) -> Tensor"};
  return std::any_of(
      nondeterministic_ops.begin(),
      nondeterministic_ops.end(),
      [this](const char* nondeterministic_op) {
        return torch::jit::parseSchema(nondeterministic_op) == this->schema_;
      });
}

bool SchemaInfo::may_alias(
    const c10::SchemaArgument& lhs,
    const c10::SchemaArgument& rhs) {
  bool basic_check = schema_.may_alias(lhs, rhs);
  if (!updated_) {
    generateAliasMaps();
  }
  if (lhs.type == c10::SchemaArgType::input &&
      rhs.type == c10::SchemaArgType::input) {
    return input_alias_map_[lhs.index].count(rhs.index) || basic_check;
  } else if (
      lhs.type == c10::SchemaArgType::output &&
      rhs.type == c10::SchemaArgType::output) {
    for (size_t lhs_alias_input : output_alias_map_[lhs.index]) {
      for (size_t rhs_alias_input : output_alias_map_[rhs.index]) {
        if (lhs_alias_input == rhs_alias_input) {
          return true;
        }
      }
    }
    return basic_check;
  } else if (lhs.type == c10::SchemaArgType::output) {
    return output_alias_map_[lhs.index].count(rhs.index) || basic_check;
  } else {
    return output_alias_map_[rhs.index].count(lhs.index) || basic_check;
  }
}

at::IValue SchemaInfo::flattenZeroDimIValue(const at::IValue& value) const {
  if (value.isList()) {
    c10::List<at::IValue> value_list = value.toList();
    if (value_list.size() == 1) {
      return value_list[0];
    }
  }
  return value;
}

void SchemaInfo::generateAliasMaps() {
  updated_ = true;
  input_alias_map_ = std::vector<std::unordered_set<size_t>>(
      schema_.arguments().size(), std::unordered_set<size_t>());
  output_alias_map_ = std::vector<std::unordered_set<size_t>>(
      schema_.returns().size(), std::unordered_set<size_t>());
  for (size_t i = 0; i < schema_.arguments().size(); i++) {
    for (size_t j = i; j < schema_.arguments().size(); j++) {
      if (i == j) {
        input_alias_map_[i].insert(i);
      } else if (
          value_map_.count(schema_.arguments()[i].name()) &&
          value_map_.count(schema_.arguments()[j].name())) {
        if (value_map_[schema_.arguments()[i].name()].isAliasOf(
                value_map_[schema_.arguments()[j].name()])) {
          input_alias_map_[i].insert(j);
          input_alias_map_[j].insert(i);
        }
      }
    }
  }
  for (size_t i = 0; i < schema_.arguments().size(); i++) {
    for (size_t j = 0; j < schema_.returns().size(); j++) {
      if (schema_.may_alias(
              {c10::SchemaArgType::input, i},
              {c10::SchemaArgType::output, j})) {
        output_alias_map_[j].insert(
            input_alias_map_[i].begin(), input_alias_map_[i].end());
      }
    }
  }
}

} // namespace utils
} // namespace torch
