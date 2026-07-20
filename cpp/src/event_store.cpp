#include "lobx/event_store.hpp"

namespace lobx {

bool EventStore::open_jsonl(const std::string& path) { out_.open(path, std::ios::out | std::ios::app); return static_cast<bool>(out_); }

void EventStore::append(lob::Timestamp ts, std::string type, std::string payload) {
  EventRecord ev{next_seq_++, ts, std::move(type), std::move(payload)};
  if (out_) {
    out_ << "{\"seq\":" << ev.seq << ",\"ts\":" << ev.ts << ",\"type\":\"" << escape_json(ev.type)
         << "\",\"payload\":\"" << escape_json(ev.payload) << "\"}\n";
  }
  if (store_memory_) records_.push_back(std::move(ev));
}

void EventStore::flush() { if (out_) out_.flush(); }

std::string EventStore::escape_json(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += ch; break;
    }
  }
  return out;
}

} // namespace lobx
