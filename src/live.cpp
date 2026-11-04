#include "canbus/live.hpp"

#include <cstdio>
#include <iomanip>
#include <sstream>

namespace canbus {

std::string format_data(const CanFrame& frame) {
  std::string out;
  if (frame.rtr) {
    return out;
  }
  char byte[4];
  for (std::size_t i = 0; i < frame.len; ++i) {
    std::snprintf(byte, sizeof byte, "%02X", frame.data[i]);
    if (i != 0) {
      out.push_back(' ');
    }
    out += byte;
  }
  return out;
}

void LiveView::update(const CanFrame& frame) {
  Entry& entry = entries_[frame.id | (frame.extended ? (1ULL << 32) : 0ULL)];
  entry.last = frame;
  ++entry.count;
}

std::string LiveView::render(const StatsSnapshot& stats) const {
  std::ostringstream os;
  os << std::fixed << std::setprecision(1) << "canscan  " << stats.frames << " frames  "
     << stats.frames_per_s << " frames/s  load " << std::setprecision(2) << stats.load_percent
     << " %\n\n";
  os << std::left << std::setw(10) << "ID" << std::setw(18) << "Name" << std::right
     << std::setw(8) << "Count" << "  Data\n";
  for (const auto& [key, entry] : entries_) {
    const CanFrame& frame = entry.last;
    const Message* message = db_ == nullptr ? nullptr : db_->find(frame.id, frame.extended);
    char id[16];
    std::snprintf(id, sizeof id, frame.extended ? "%08X" : "%03X", frame.id);
    os << std::left << std::setw(10) << id << std::setw(18)
       << (message != nullptr ? message->name : std::string("-")) << std::right << std::setw(8)
       << entry.count << "  " << format_data(frame) << '\n';
    if (message != nullptr) {
      for (const DecodedSignal& decoded : decode_message(*message, frame)) {
        os << "            " << format_signal(decoded) << '\n';
      }
    }
  }
  return os.str();
}

}  // namespace canbus
