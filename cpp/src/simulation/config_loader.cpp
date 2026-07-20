#include "lobx/simulation/config_loader.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lobx::sim {

namespace {

struct JsonValue {
  enum class Type { Object, Array, String, Number, Bool, Null };

  Type type{Type::Null};
  std::map<std::string, JsonValue> object;
  std::vector<JsonValue> array;
  std::string string;
  std::string number_text;
  long double number{0.0L};
  bool boolean{false};
};

class JsonParser {
public:
  explicit JsonParser(const std::string& input) : input_(input) {}

  bool parse(JsonValue& out, std::string& reason) {
    skip_ws();
    if (!parse_value(out, reason)) return false;
    skip_ws();
    if (pos_ != input_.size()) {
      reason = "trailing garbage after JSON document";
      return false;
    }
    return true;
  }

private:
  bool parse_value(JsonValue& out, std::string& reason) {
    skip_ws();
    if (pos_ >= input_.size()) {
      reason = "unexpected end of JSON";
      return false;
    }
    const char c = input_[pos_];
    if (c == '{') return parse_object(out, reason);
    if (c == '[') return parse_array(out, reason);
    if (c == '"') return parse_string_value(out, reason);
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number(out, reason);
    if (match_literal("true")) {
      out.type = JsonValue::Type::Bool;
      out.boolean = true;
      return true;
    }
    if (match_literal("false")) {
      out.type = JsonValue::Type::Bool;
      out.boolean = false;
      return true;
    }
    if (match_literal("null")) {
      out.type = JsonValue::Type::Null;
      return true;
    }
    reason = "unexpected JSON token at offset " + std::to_string(pos_);
    return false;
  }

  bool parse_object(JsonValue& out, std::string& reason) {
    out = JsonValue{};
    out.type = JsonValue::Type::Object;
    ++pos_;
    skip_ws();
    if (consume('}')) return true;
    while (true) {
      std::string key;
      if (!parse_string(key, reason)) return false;
      skip_ws();
      if (!consume(':')) {
        reason = "expected ':' after object key";
        return false;
      }
      JsonValue value;
      if (!parse_value(value, reason)) return false;
      out.object[key] = std::move(value);
      skip_ws();
      if (consume('}')) return true;
      if (!consume(',')) {
        reason = "expected ',' or '}' in object";
        return false;
      }
      skip_ws();
    }
  }

  bool parse_array(JsonValue& out, std::string& reason) {
    out = JsonValue{};
    out.type = JsonValue::Type::Array;
    ++pos_;
    skip_ws();
    if (consume(']')) return true;
    while (true) {
      JsonValue value;
      if (!parse_value(value, reason)) return false;
      out.array.push_back(std::move(value));
      skip_ws();
      if (consume(']')) return true;
      if (!consume(',')) {
        reason = "expected ',' or ']' in array";
        return false;
      }
      skip_ws();
    }
  }

  bool parse_string_value(JsonValue& out, std::string& reason) {
    out = JsonValue{};
    out.type = JsonValue::Type::String;
    return parse_string(out.string, reason);
  }

  bool parse_string(std::string& out, std::string& reason) {
    if (!consume('"')) {
      reason = "expected string";
      return false;
    }
    out.clear();
    while (pos_ < input_.size()) {
      const char c = input_[pos_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) {
        reason = "control character in string";
        return false;
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= input_.size()) {
        reason = "unterminated escape sequence";
        return false;
      }
      const char esc = input_[pos_++];
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        default:
          reason = "unsupported escape sequence";
          return false;
      }
    }
    reason = "unterminated string";
    return false;
  }

  bool parse_number(JsonValue& out, std::string& reason) {
    const size_t start = pos_;
    if (input_[pos_] == '-') ++pos_;
    if (pos_ >= input_.size()) {
      reason = "invalid number";
      return false;
    }
    if (input_[pos_] == '0') {
      ++pos_;
    } else if (std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    } else {
      reason = "invalid number";
      return false;
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        reason = "invalid number fraction";
        return false;
      }
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }
    if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
      if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        reason = "invalid number exponent";
        return false;
      }
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }

    out = JsonValue{};
    out.type = JsonValue::Type::Number;
    out.number_text = input_.substr(start, pos_ - start);
    errno = 0;
    char* end = nullptr;
    out.number = std::strtold(out.number_text.c_str(), &end);
    if (errno != 0 || end == out.number_text.c_str() || *end != '\0') {
      reason = "invalid number value";
      return false;
    }
    return true;
  }

  bool match_literal(const char* literal) {
    const size_t start = pos_;
    for (const char* p = literal; *p != '\0'; ++p) {
      if (pos_ >= input_.size() || input_[pos_] != *p) {
        pos_ = start;
        return false;
      }
      ++pos_;
    }
    return true;
  }

  bool consume(char expected) {
    if (pos_ < input_.size() && input_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  void skip_ws() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
  }

  const std::string& input_;
  size_t pos_{0};
};

const JsonValue* member(const JsonValue& object, const std::string& key) {
  if (object.type != JsonValue::Type::Object) return nullptr;
  const auto it = object.object.find(key);
  return it == object.object.end() ? nullptr : &it->second;
}

bool is_integer_text(const std::string& text) {
  if (text.empty()) return false;
  size_t pos = text[0] == '-' ? 1 : 0;
  if (pos == text.size()) return false;
  for (; pos < text.size(); ++pos) {
    if (!std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
  }
  return true;
}

bool parse_uint64_text(const std::string& text, uint64_t& out) {
  if (!is_integer_text(text) || text[0] == '-') return false;
  try {
    size_t consumed = 0;
    out = std::stoull(text, &consumed, 10);
    return consumed == text.size();
  } catch (...) {
    return false;
  }
}

bool parse_int64_text(const std::string& text, int64_t& out) {
  if (!is_integer_text(text)) return false;
  try {
    size_t consumed = 0;
    out = std::stoll(text, &consumed, 10);
    return consumed == text.size();
  } catch (...) {
    return false;
  }
}

bool required_object(const JsonValue& object, const std::string& key, const JsonValue*& out, std::string& reason) {
  out = member(object, key);
  if (out == nullptr) {
    reason = "missing required field: " + key;
    return false;
  }
  if (out->type != JsonValue::Type::Object) {
    reason = "field must be object: " + key;
    return false;
  }
  return true;
}

bool required_array(const JsonValue& object, const std::string& key, const JsonValue*& out, std::string& reason) {
  out = member(object, key);
  if (out == nullptr) {
    reason = "missing required field: " + key;
    return false;
  }
  if (out->type != JsonValue::Type::Array) {
    reason = "field must be array: " + key;
    return false;
  }
  return true;
}

bool required_string(const JsonValue& object, const std::string& key, std::string& out, std::string& reason) {
  const JsonValue* value = member(object, key);
  if (value == nullptr) {
    reason = "missing required field: " + key;
    return false;
  }
  if (value->type != JsonValue::Type::String) {
    reason = "field must be string: " + key;
    return false;
  }
  out = value->string;
  return true;
}

bool required_uint64(const JsonValue& object, const std::string& key, uint64_t& out, std::string& reason) {
  const JsonValue* value = member(object, key);
  if (value == nullptr) {
    reason = "missing required field: " + key;
    return false;
  }
  if (value->type != JsonValue::Type::Number || !parse_uint64_text(value->number_text, out)) {
    reason = "field must be uint64: " + key;
    return false;
  }
  return true;
}

bool required_int(const JsonValue& object, const std::string& key, int& out, std::string& reason) {
  int64_t parsed = 0;
  const JsonValue* value = member(object, key);
  if (value == nullptr) {
    reason = "missing required field: " + key;
    return false;
  }
  if (value->type != JsonValue::Type::Number || !parse_int64_text(value->number_text, parsed) ||
      parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
    reason = "field must be int: " + key;
    return false;
  }
  out = static_cast<int>(parsed);
  return true;
}

bool required_double(const JsonValue& object, const std::string& key, double& out, std::string& reason) {
  const JsonValue* value = member(object, key);
  if (value == nullptr) {
    reason = "missing required field: " + key;
    return false;
  }
  if (value->type != JsonValue::Type::Number) {
    reason = "field must be number: " + key;
    return false;
  }
  out = static_cast<double>(value->number);
  return true;
}

bool optional_double(const JsonValue& object, const std::string& key, double& out, std::string& reason) {
  const JsonValue* value = member(object, key);
  if (value == nullptr) return true;
  if (value->type != JsonValue::Type::Number) {
    reason = "field must be number: " + key;
    return false;
  }
  out = static_cast<double>(value->number);
  return true;
}

bool required_timestamp(const JsonValue& object, const std::string& key, lob::Timestamp& out, std::string& reason) {
  int64_t parsed = 0;
  const JsonValue* value = member(object, key);
  if (value == nullptr) {
    reason = "missing required field: " + key;
    return false;
  }
  if (value->type != JsonValue::Type::Number || !parse_int64_text(value->number_text, parsed)) {
    reason = "field must be integer timestamp: " + key;
    return false;
  }
  out = static_cast<lob::Timestamp>(parsed);
  return true;
}

bool parse_document(const std::string& json, JsonValue& root, std::string& reason) {
  JsonParser parser(json);
  if (!parser.parse(root, reason)) return false;
  if (root.type != JsonValue::Type::Object) {
    reason = "top-level JSON value must be object";
    return false;
  }
  return true;
}

bool read_file(const std::string& path, std::string& out, std::string& reason) {
  std::ifstream in(path);
  if (!in.is_open()) {
    reason = "failed to open file: " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

std::string escape_json(const std::string& input) {
  std::ostringstream os;
  for (char c : input) {
    switch (c) {
      case '\\': os << "\\\\"; break;
      case '"': os << "\\\""; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      default: os << c; break;
    }
  }
  return os.str();
}

std::string format_double(double value) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(10) << value;
  return os.str();
}

bool parse_latency(const JsonValue& bot_object, LatencyConfig& out, std::string& reason) {
  const JsonValue* latency = nullptr;
  if (!required_object(bot_object, "latency", latency, reason)) return false;
  return required_timestamp(*latency, "order", out.order_latency, reason) &&
         required_timestamp(*latency, "cancel", out.cancel_latency, reason) &&
         required_timestamp(*latency, "market_data", out.market_data_latency, reason) &&
         required_timestamp(*latency, "private_data", out.private_data_latency, reason);
}

bool parse_params(const JsonValue& bot_object, std::map<std::string, double>& out, std::string& reason) {
  const JsonValue* params = nullptr;
  if (!required_object(bot_object, "params", params, reason)) return false;
  out.clear();
  for (const auto& [key, value] : params->object) {
    if (value.type != JsonValue::Type::Number) {
      reason = "params values must be numeric: " + key;
      return false;
    }
    out[key] = static_cast<double>(value.number);
  }
  return true;
}

bool parse_bot(const JsonValue& bot_value, BotConfig& out, std::string& reason) {
  if (bot_value.type != JsonValue::Type::Object) {
    reason = "bot entry must be object";
    return false;
  }
  uint64_t user = 0;
  if (!required_uint64(bot_value, "user", user, reason)) return false;
  out.user = static_cast<UserId>(user);
  return required_string(bot_value, "name", out.name, reason) &&
         required_string(bot_value, "strategy_type", out.strategy_type, reason) &&
         parse_latency(bot_value, out.latency, reason) &&
         parse_params(bot_value, out.params, reason);
}

bool parse_side(const JsonValue& object, lob::Side& out, std::string& reason) {
  std::string side;
  if (!required_string(object, "side", side, reason)) return false;
  if (side == "bid") {
    out = lob::Side::Bid;
    return true;
  }
  if (side == "ask") {
    out = lob::Side::Ask;
    return true;
  }
  reason = "side must be bid or ask";
  return false;
}

bool parse_market_environment_object(const JsonValue& object,
                                     MarketEnvironmentConfig& out,
                                     std::string& reason) {
  int reference_price = 0;
  if (!required_string(object, "market_symbol", out.market_symbol, reason) ||
      !required_int(object, "reference_price", reference_price, reason) ||
      !required_int(object, "ticks", out.ticks, reason) ||
      !required_int(object, "warmup_ticks", out.warmup_ticks, reason)) {
    return false;
  }
  out.reference_price = static_cast<lob::Tick>(reference_price);
  if (!optional_double(object, "noise_intensity", out.noise_intensity, reason) ||
      !optional_double(object, "liquidity_scale", out.liquidity_scale, reason) ||
      !optional_double(object, "volatility_regime", out.volatility_regime, reason) ||
      !optional_double(object, "spread_regime", out.spread_regime, reason)) {
    return false;
  }

  const JsonValue* levels = nullptr;
  if (!required_array(object, "initial_book", levels, reason)) return false;
  for (const JsonValue& value : levels->array) {
    if (value.type != JsonValue::Type::Object) {
      reason = "initial_book entry must be object";
      return false;
    }
    InitialBookLevel level{};
    int price = 0;
    int qty = 0;
    if (!parse_side(value, level.side, reason) ||
        !required_int(value, "price", price, reason) ||
        !required_int(value, "qty", qty, reason)) {
      return false;
    }
    level.price = static_cast<lob::Tick>(price);
    level.qty = static_cast<lob::Quantity>(qty);
    out.initial_book.push_back(level);
  }
  return true;
}

bool parse_latency_range(const JsonValue& object, LatencyRangeConfig& out, std::string& reason) {
  const JsonValue* latency = nullptr;
  if (!required_object(object, "latency_range", latency, reason)) return false;
  return required_int(*latency, "order_min", out.order_min, reason) &&
         required_int(*latency, "order_max", out.order_max, reason) &&
         required_int(*latency, "cancel_min", out.cancel_min, reason) &&
         required_int(*latency, "cancel_max", out.cancel_max, reason) &&
         required_int(*latency, "market_data_min", out.market_data_min, reason) &&
         required_int(*latency, "market_data_max", out.market_data_max, reason) &&
         required_int(*latency, "private_data_min", out.private_data_min, reason) &&
         required_int(*latency, "private_data_max", out.private_data_max, reason);
}

bool parse_param_ranges(const JsonValue& object,
                        std::map<std::string, DoubleRange>& out,
                        std::string& reason) {
  const JsonValue* ranges = nullptr;
  if (!required_object(object, "param_ranges", ranges, reason)) return false;
  for (const auto& [name, value] : ranges->object) {
    if (value.type != JsonValue::Type::Object) {
      reason = "param range entry must be object: " + name;
      return false;
    }
    DoubleRange range{};
    if (!required_double(value, "min", range.min, reason) ||
        !required_double(value, "max", range.max, reason)) {
      return false;
    }
    out[name] = range;
  }
  return true;
}

bool parse_agent_population_object(const JsonValue& object,
                                   AgentPopulationConfig& out,
                                   std::string& reason) {
  uint64_t first_user = 0;
  if (!required_uint64(object, "seed", out.seed, reason) ||
      !required_uint64(object, "first_user_id", first_user, reason)) {
    return false;
  }
  out.first_user_id = static_cast<UserId>(first_user);

  const JsonValue* groups = nullptr;
  if (!required_array(object, "groups", groups, reason)) return false;
  if (groups->array.empty()) {
    reason = "groups array must not be empty";
    return false;
  }
  for (const JsonValue& value : groups->array) {
    if (value.type != JsonValue::Type::Object) {
      reason = "agent group entry must be object";
      return false;
    }
    AgentGroupConfig group{};
    if (!required_string(value, "strategy_type", group.strategy_type, reason) ||
        !required_int(value, "count", group.count, reason) ||
        !required_string(value, "name_prefix", group.name_prefix, reason) ||
        !parse_latency_range(value, group.latency_range, reason) ||
        !parse_param_ranges(value, group.param_ranges, reason)) {
      return false;
    }
    out.groups.push_back(std::move(group));
  }
  return true;
}

} // namespace

ScenarioConfigLoadResult load_scenario_config_from_json_string(const std::string& json) {
  ScenarioConfigLoadResult out{};
  JsonValue root;
  if (!parse_document(json, root, out.reason)) return out;

  if (!required_uint64(root, "seed", out.config.seed, out.reason)) return out;
  if (!required_int(root, "ticks", out.config.ticks, out.reason)) return out;
  if (!required_string(root, "market_symbol", out.config.market_symbol, out.reason)) return out;

  const JsonValue* bots = nullptr;
  if (!required_array(root, "bots", bots, out.reason)) return out;
  if (bots->array.empty()) {
    out.reason = "bots array must not be empty";
    return out;
  }
  for (const JsonValue& bot_value : bots->array) {
    BotConfig bot;
    if (!parse_bot(bot_value, bot, out.reason)) return out;
    out.config.bots.push_back(std::move(bot));
  }

  const ValidationResult validation = validate_scenario_config(out.config);
  if (!validation.ok) {
    out.reason = validation.reason;
    return out;
  }
  out.ok = true;
  return out;
}

ScenarioConfigLoadResult load_scenario_config_from_json_file(const std::string& path) {
  std::string content;
  std::string reason;
  if (!read_file(path, content, reason)) return ScenarioConfigLoadResult{false, reason, {}};
  return load_scenario_config_from_json_string(content);
}

SweepConfigLoadResult load_sweep_config_from_json_string(const std::string& json) {
  SweepConfigLoadResult out{};
  JsonValue root;
  if (!parse_document(json, root, out.reason)) return out;
  const JsonValue* params = nullptr;
  if (!required_array(root, "params", params, out.reason)) return out;
  if (params->array.empty()) {
    out.reason = "params array must not be empty";
    return out;
  }
  for (const JsonValue& value : params->array) {
    if (value.type != JsonValue::Type::Object) {
      out.reason = "sweep param entry must be object";
      return out;
    }
    SweepParam param;
    if (!required_string(value, "bot_name", param.bot_name, out.reason) ||
        !required_string(value, "param_name", param.param_name, out.reason)) {
      return out;
    }
    const JsonValue* values = nullptr;
    if (!required_array(value, "values", values, out.reason)) return out;
    if (values->array.empty()) {
      out.reason = "sweep values must not be empty";
      return out;
    }
    for (const JsonValue& item : values->array) {
      if (item.type != JsonValue::Type::Number) {
        out.reason = "sweep values must be numeric";
        return out;
      }
      param.values.push_back(static_cast<double>(item.number));
    }
    if (param.bot_name.empty() || param.param_name.empty()) {
      out.reason = "bot_name and param_name must not be empty";
      return out;
    }
    out.params.push_back(std::move(param));
  }
  out.ok = true;
  return out;
}

SweepConfigLoadResult load_sweep_config_from_json_file(const std::string& path) {
  std::string content;
  std::string reason;
  if (!read_file(path, content, reason)) return SweepConfigLoadResult{false, reason, {}};
  return load_sweep_config_from_json_string(content);
}

MultiSeedConfigLoadResult load_seed_config_from_json_string(const std::string& json) {
  MultiSeedConfigLoadResult out{};
  JsonValue root;
  if (!parse_document(json, root, out.reason)) return out;
  const JsonValue* seeds = nullptr;
  if (!required_array(root, "seeds", seeds, out.reason)) return out;
  if (seeds->array.empty()) {
    out.reason = "seeds array must not be empty";
    return out;
  }
  for (const JsonValue& value : seeds->array) {
    uint64_t seed = 0;
    if (value.type != JsonValue::Type::Number || !parse_uint64_text(value.number_text, seed)) {
      out.reason = "seed values must be uint64";
      return out;
    }
    out.seeds.push_back(seed);
  }
  out.ok = true;
  return out;
}

MultiSeedConfigLoadResult load_seed_config_from_json_file(const std::string& path) {
  std::string content;
  std::string reason;
  if (!read_file(path, content, reason)) return MultiSeedConfigLoadResult{false, reason, {}};
  return load_seed_config_from_json_string(content);
}

MarketEnvironmentConfigLoadResult load_market_environment_from_json_string(const std::string& json) {
  MarketEnvironmentConfigLoadResult out{};
  JsonValue root;
  if (!parse_document(json, root, out.reason)) return out;
  const JsonValue* object = member(root, "market_environment");
  if (object == nullptr) object = &root;
  if (object->type != JsonValue::Type::Object) {
    out.reason = "market_environment must be object";
    return out;
  }
  if (!parse_market_environment_object(*object, out.config, out.reason)) return out;
  const MarketEnvironmentValidation validation = validate_market_environment(out.config);
  if (!validation.ok) {
    out.reason = validation.reason;
    return out;
  }
  out.ok = true;
  return out;
}

MarketEnvironmentConfigLoadResult load_market_environment_from_json_file(const std::string& path) {
  std::string content;
  std::string reason;
  if (!read_file(path, content, reason)) return MarketEnvironmentConfigLoadResult{false, reason, {}};
  return load_market_environment_from_json_string(content);
}

AgentPopulationConfigLoadResult load_agent_population_from_json_string(const std::string& json) {
  AgentPopulationConfigLoadResult out{};
  JsonValue root;
  if (!parse_document(json, root, out.reason)) return out;
  const JsonValue* object = member(root, "agent_population");
  if (object == nullptr) object = &root;
  if (object->type != JsonValue::Type::Object) {
    out.reason = "agent_population must be object";
    return out;
  }
  if (!parse_agent_population_object(*object, out.config, out.reason)) return out;
  const AgentPopulationValidation validation = validate_agent_population(out.config);
  if (!validation.ok) {
    out.reason = validation.reason;
    return out;
  }
  out.ok = true;
  return out;
}

AgentPopulationConfigLoadResult load_agent_population_from_json_file(const std::string& path) {
  std::string content;
  std::string reason;
  if (!read_file(path, content, reason)) return AgentPopulationConfigLoadResult{false, reason, {}};
  return load_agent_population_from_json_string(content);
}

EmergenceConfigLoadResult load_emergence_config_from_json_string(const std::string& json) {
  EmergenceConfigLoadResult out{};
  JsonValue root;
  if (!parse_document(json, root, out.reason)) return out;
  const JsonValue* market = nullptr;
  const JsonValue* population = nullptr;
  if (!required_object(root, "market_environment", market, out.reason) ||
      !required_object(root, "agent_population", population, out.reason)) {
    return out;
  }
  if (!parse_market_environment_object(*market, out.config.market_environment, out.reason) ||
      !parse_agent_population_object(*population, out.config.agent_population, out.reason)) {
    return out;
  }
  const MarketEnvironmentValidation market_validation =
      validate_market_environment(out.config.market_environment);
  if (!market_validation.ok) {
    out.reason = market_validation.reason;
    return out;
  }
  const AgentPopulationValidation population_validation =
      validate_agent_population(out.config.agent_population);
  if (!population_validation.ok) {
    out.reason = population_validation.reason;
    return out;
  }
  out.ok = true;
  return out;
}

EmergenceConfigLoadResult load_emergence_config_from_json_file(const std::string& path) {
  std::string content;
  std::string reason;
  if (!read_file(path, content, reason)) return EmergenceConfigLoadResult{false, reason, {}};
  return load_emergence_config_from_json_string(content);
}

std::string scenario_config_to_json(const ScenarioConfig& config) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"seed\": " << config.seed << ",\n";
  os << "  \"ticks\": " << config.ticks << ",\n";
  os << "  \"market_symbol\": \"" << escape_json(config.market_symbol) << "\",\n";
  os << "  \"bots\": [\n";
  for (size_t i = 0; i < config.bots.size(); ++i) {
    const BotConfig& bot = config.bots[i];
    os << "    {\n";
    os << "      \"user\": " << bot.user << ",\n";
    os << "      \"name\": \"" << escape_json(bot.name) << "\",\n";
    os << "      \"strategy_type\": \"" << escape_json(bot.strategy_type) << "\",\n";
    os << "      \"latency\": {\"order\": " << bot.latency.order_latency
       << ", \"cancel\": " << bot.latency.cancel_latency
       << ", \"market_data\": " << bot.latency.market_data_latency
       << ", \"private_data\": " << bot.latency.private_data_latency << "},\n";
    os << "      \"params\": {";
    bool first = true;
    for (const auto& [key, value] : bot.params) {
      if (!first) os << ", ";
      first = false;
      os << "\"" << escape_json(key) << "\": " << format_double(value);
    }
    os << "}\n";
    os << "    }" << (i + 1 == config.bots.size() ? "\n" : ",\n");
  }
  os << "  ]\n";
  os << "}";
  return os.str();
}

std::string sweep_config_to_json(const std::vector<SweepParam>& params) {
  std::ostringstream os;
  os << "{\n  \"params\": [\n";
  for (size_t i = 0; i < params.size(); ++i) {
    const SweepParam& param = params[i];
    os << "    {\"bot_name\": \"" << escape_json(param.bot_name)
       << "\", \"param_name\": \"" << escape_json(param.param_name)
       << "\", \"values\": [";
    for (size_t j = 0; j < param.values.size(); ++j) {
      if (j > 0) os << ", ";
      os << format_double(param.values[j]);
    }
    os << "]}" << (i + 1 == params.size() ? "\n" : ",\n");
  }
  os << "  ]\n}";
  return os.str();
}

std::string seed_config_to_json(const std::vector<uint64_t>& seeds) {
  std::ostringstream os;
  os << "{\n  \"seeds\": [";
  for (size_t i = 0; i < seeds.size(); ++i) {
    if (i > 0) os << ", ";
    os << seeds[i];
  }
  os << "]\n}";
  return os.str();
}

std::string market_environment_to_json(const MarketEnvironmentConfig& config) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"market_symbol\": \"" << escape_json(config.market_symbol) << "\",\n";
  os << "  \"reference_price\": " << config.reference_price << ",\n";
  os << "  \"ticks\": " << config.ticks << ",\n";
  os << "  \"warmup_ticks\": " << config.warmup_ticks << ",\n";
  os << "  \"initial_book\": [\n";
  for (size_t i = 0; i < config.initial_book.size(); ++i) {
    const InitialBookLevel& level = config.initial_book[i];
    os << "    {\"side\": \"" << (level.side == lob::Side::Bid ? "bid" : "ask")
       << "\", \"price\": " << level.price
       << ", \"qty\": " << level.qty << "}"
       << (i + 1 == config.initial_book.size() ? "\n" : ",\n");
  }
  os << "  ],\n";
  os << "  \"noise_intensity\": " << format_double(config.noise_intensity) << ",\n";
  os << "  \"liquidity_scale\": " << format_double(config.liquidity_scale) << ",\n";
  os << "  \"volatility_regime\": " << format_double(config.volatility_regime) << ",\n";
  os << "  \"spread_regime\": " << format_double(config.spread_regime) << "\n";
  os << "}";
  return os.str();
}

std::string agent_population_to_json(const AgentPopulationConfig& config) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"seed\": " << config.seed << ",\n";
  os << "  \"first_user_id\": " << config.first_user_id << ",\n";
  os << "  \"groups\": [\n";
  for (size_t i = 0; i < config.groups.size(); ++i) {
    const AgentGroupConfig& group = config.groups[i];
    os << "    {\n";
    os << "      \"strategy_type\": \"" << escape_json(group.strategy_type) << "\",\n";
    os << "      \"count\": " << group.count << ",\n";
    os << "      \"name_prefix\": \"" << escape_json(group.name_prefix) << "\",\n";
    os << "      \"latency_range\": {"
       << "\"order_min\": " << group.latency_range.order_min
       << ", \"order_max\": " << group.latency_range.order_max
       << ", \"cancel_min\": " << group.latency_range.cancel_min
       << ", \"cancel_max\": " << group.latency_range.cancel_max
       << ", \"market_data_min\": " << group.latency_range.market_data_min
       << ", \"market_data_max\": " << group.latency_range.market_data_max
       << ", \"private_data_min\": " << group.latency_range.private_data_min
       << ", \"private_data_max\": " << group.latency_range.private_data_max << "},\n";
    os << "      \"param_ranges\": {";
    bool first = true;
    for (const auto& [name, range] : group.param_ranges) {
      if (!first) os << ", ";
      first = false;
      os << "\"" << escape_json(name) << "\": {\"min\": "
         << format_double(range.min) << ", \"max\": " << format_double(range.max) << "}";
    }
    os << "}\n";
    os << "    }" << (i + 1 == config.groups.size() ? "\n" : ",\n");
  }
  os << "  ]\n";
  os << "}";
  return os.str();
}

std::string emergence_config_to_json(const EmergenceConfig& config) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"market_environment\": " << market_environment_to_json(config.market_environment) << ",\n";
  os << "  \"agent_population\": " << agent_population_to_json(config.agent_population) << "\n";
  os << "}";
  return os.str();
}

} // namespace lobx::sim
