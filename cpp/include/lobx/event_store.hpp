#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "lobx/types.hpp"

namespace lobx {

struct EventRecord {
  uint64_t seq{0};
  lob::Timestamp ts{0};
  std::string type;
  std::string payload;
};

class EventStore {
public:
  bool open_jsonl(const std::string& path);
  void append(lob::Timestamp ts, std::string type, std::string payload);
  void flush();
  void set_memory_enabled(bool enabled) { store_memory_ = enabled; }

  const std::vector<EventRecord>& records() const { return records_; }
  uint64_t next_seq() const { return next_seq_; }

private:
  static std::string escape_json(const std::string& s);
  uint64_t next_seq_{1};
  bool store_memory_{true};
  std::vector<EventRecord> records_;
  std::ofstream out_;
};

} // namespace lobx
