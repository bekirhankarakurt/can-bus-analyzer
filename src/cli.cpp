#include "canbus/cli.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>

#include "canbus/candump.hpp"
#include "canbus/dbc.hpp"
#include "canbus/filter.hpp"
#include "canbus/live.hpp"
#include "canbus/socketcan.hpp"
#include "canbus/source.hpp"
#include "canbus/stats.hpp"

namespace canbus {
namespace {

constexpr const char* kUsage = R"(canscan - SocketCAN analyzer and simulator

Usage: canscan <command> [options]

Commands:
  monitor            Print frames, decoded with --dbc; --live shows a refreshing table
  record             Write frames to a candump-compatible log (-o FILE)
  stats              Print a bus-load and per-ID report when the capture ends
  replay LOG         Re-send a candump log with its original timing (to -i, else stdout)
  generate DBC       Synthesize traffic from a DBC file (to -i, else stdout)

Input (monitor, record, stats: choose exactly one):
  -i, --iface IFACE      Capture from a SocketCAN interface (e.g. vcan0)
      --replay LOG       Read frames from a candump log instead of hardware
      --generate DBC     Read frames synthesized from a DBC instead of hardware

Options:
  -i, --iface IFACE      Output interface for replay/generate
      --dbc FILE         DBC file used to decode signals
  -f, --filter SPEC      Identifier filter: ID, ID:MASK, or ID~MASK to reject (repeatable, hex)
  -o, --output FILE      Log file written by record
  -n, --count N          Stop after N accepted frames
  -t, --duration SEC     Stop after SEC seconds of bus time
      --speed X          Playback speed for --replay/--generate/replay/generate (default 1, 0 = no delay)
      --loop             Repeat the log endlessly (replay, --replay)
      --live             Refreshing table instead of a scrolling stream (monitor)
      --bitrate BPS      Nominal bit rate for the load estimate (default 500000)
  -h, --help             Show this help

Press Ctrl-C to stop a running command.
)";

class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class Command { Help, Monitor, Record, Stats, Replay, Generate };
enum class Input { None, Interface, Replay, Generate };

struct Options {
  Command command = Command::Help;
  std::string iface;
  std::string replay_file;
  std::string generate_file;
  std::string dbc_file;
  std::string output_file;
  std::string positional;
  std::vector<std::string> filters;
  double speed = 1.0;
  double duration = 0.0;
  std::uint64_t count = 0;
  std::uint32_t bitrate = 500000;
  bool loop = false;
  bool live = false;
};

double parse_number(const std::string& option, const std::string& text) {
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (text.empty() || *end != '\0' || !(value >= 0.0)) {
    throw UsageError("option " + option + " needs a non-negative number, got '" + text + "'");
  }
  return value;
}

std::uint64_t parse_integer(const std::string& option, const std::string& text) {
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
    throw UsageError("option " + option + " needs a non-negative integer, got '" + text + "'");
  }
  return std::strtoull(text.c_str(), nullptr, 10);
}

Options parse_args(const std::vector<std::string>& args) {
  Options o;
  if (args.empty()) {
    throw UsageError("missing command");
  }
  const std::string& name = args[0];
  if (name == "monitor") {
    o.command = Command::Monitor;
  } else if (name == "record") {
    o.command = Command::Record;
  } else if (name == "stats") {
    o.command = Command::Stats;
  } else if (name == "replay") {
    o.command = Command::Replay;
  } else if (name == "generate") {
    o.command = Command::Generate;
  } else if (name == "-h" || name == "--help" || name == "help") {
    return o;
  } else {
    throw UsageError("unknown command '" + name + "'");
  }

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];
    const auto value = [&]() -> const std::string& {
      if (i + 1 >= args.size()) {
        throw UsageError("option " + arg + " needs a value");
      }
      return args[++i];
    };
    if (arg == "-h" || arg == "--help") {
      o.command = Command::Help;
      return o;
    } else if (arg == "-i" || arg == "--iface") {
      o.iface = value();
    } else if (arg == "--replay") {
      o.replay_file = value();
    } else if (arg == "--generate") {
      o.generate_file = value();
    } else if (arg == "--dbc") {
      o.dbc_file = value();
    } else if (arg == "-f" || arg == "--filter") {
      o.filters.push_back(value());
    } else if (arg == "-o" || arg == "--output") {
      o.output_file = value();
    } else if (arg == "-n" || arg == "--count") {
      o.count = parse_integer(arg, value());
    } else if (arg == "-t" || arg == "--duration") {
      o.duration = parse_number(arg, value());
    } else if (arg == "--speed") {
      o.speed = parse_number(arg, value());
    } else if (arg == "--bitrate") {
      const std::uint64_t bitrate = parse_integer(arg, value());
      if (bitrate == 0 || bitrate > 0xFFFFFFFFULL) {
        throw UsageError("option --bitrate must be between 1 and 4294967295");
      }
      o.bitrate = static_cast<std::uint32_t>(bitrate);
    } else if (arg == "--loop") {
      o.loop = true;
    } else if (arg == "--live") {
      o.live = true;
    } else if (!arg.empty() && arg.front() != '-' && o.positional.empty()) {
      o.positional = arg;
    } else {
      throw UsageError("unexpected argument '" + arg + "'");
    }
  }
  return o;
}

Input input_kind(const Options& o) {
  switch (o.command) {
    case Command::Replay:
      return Input::Replay;
    case Command::Generate:
      return Input::Generate;
    default:
      break;
  }
  const int chosen = int{!o.iface.empty()} + int{!o.replay_file.empty()} + int{!o.generate_file.empty()};
  if (chosen != 1) {
    throw UsageError("choose exactly one input: -i IFACE, --replay LOG or --generate DBC");
  }
  return !o.iface.empty() ? Input::Interface
                          : (!o.replay_file.empty() ? Input::Replay : Input::Generate);
}

void validate(const Options& o) {
  const bool takes_file = o.command == Command::Replay || o.command == Command::Generate;
  if (takes_file && o.positional.empty()) {
    throw UsageError(o.command == Command::Replay ? "replay needs a log file"
                                                  : "generate needs a DBC file");
  }
  if (!takes_file && !o.positional.empty()) {
    throw UsageError("unexpected argument '" + o.positional + "'");
  }
  if (o.command == Command::Record && o.output_file.empty()) {
    throw UsageError("record needs an output file (-o FILE)");
  }
  if (o.live && o.command != Command::Monitor) {
    throw UsageError("--live is only available for monitor");
  }
  const Input input = input_kind(o);
  if (input == Input::Generate && o.speed == 0.0 && o.count == 0 && o.duration == 0.0) {
    throw UsageError("generating without delay needs --count or --duration");
  }
}

Database load_dbc(const std::string& path, std::ostream& err) {
  Database db = Database::load(path);
  if (db.skipped_multiplexed() != 0) {
    err << "warning: " << db.skipped_multiplexed() << " multiplexed signal(s) in '" << path
        << "' are not supported and were skipped\n";
  }
  return db;
}

struct Session {
  std::unique_ptr<FrameSource> source;
  std::optional<Database> decoder;
};

Session open_session(const Options& o, const std::atomic<bool>& stop, std::ostream& err) {
  Session session;
  if (!o.dbc_file.empty()) {
    session.decoder = load_dbc(o.dbc_file, err);
  }
  const Input kind = input_kind(o);
  if (kind == Input::Interface) {
    session.source = std::make_unique<SocketCan>(o.iface);
    return session;
  }
  if (kind == Input::Replay) {
    const std::string& path = o.command == Command::Replay ? o.positional : o.replay_file;
    session.source = std::make_unique<LogSource>(read_log_file(path), o.loop);
  } else {
    const std::string& path = o.command == Command::Generate ? o.positional : o.generate_file;
    const Database generator_db = load_dbc(path, err);
    if (!session.decoder) {
      session.decoder = generator_db;
    }
    session.source = std::make_unique<GeneratorSource>(generator_db, unix_time_now());
  }
  if (o.speed > 0.0) {
    session.source = std::make_unique<PacedSource>(std::move(session.source), o.speed, stop);
  }
  return session;
}

FilterSet build_filters(const Options& o) {
  FilterSet filters;
  for (const std::string& spec : o.filters) {
    try {
      filters.add(Filter::parse(spec));
    } catch (const std::invalid_argument& e) {
      throw UsageError(e.what());
    }
  }
  return filters;
}

// Reads frames until the source ends, a limit is reached or `stop` is set.
template <typename OnFrame>
void pump(FrameSource& source, const FilterSet& filters, const Options& o,
          const std::atomic<bool>& stop, OnFrame&& on_frame) {
  const auto wall_start = std::chrono::steady_clock::now();
  std::optional<double> first_timestamp;
  std::uint64_t accepted = 0;
  while (!stop.load()) {
    CanFrame frame;
    const ReadStatus status = source.next(frame);
    if (status == ReadStatus::End) {
      return;
    }
    if (status == ReadStatus::Timeout) {
      const std::chrono::duration<double> waited = std::chrono::steady_clock::now() - wall_start;
      if (o.duration > 0.0 && waited.count() >= o.duration) {
        return;
      }
      continue;
    }
    if (!first_timestamp) {
      first_timestamp = frame.timestamp;
    }
    if (o.duration > 0.0 && frame.timestamp - *first_timestamp >= o.duration) {
      return;
    }
    if (!filters.accepts(frame)) {
      continue;
    }
    on_frame(frame);
    if (o.count != 0 && ++accepted >= o.count) {
      return;
    }
  }
}

std::string describe(const CanFrame& frame, const std::string& iface, const Database* db) {
  std::string line = format_frame(frame, iface);
  const Message* message = db == nullptr ? nullptr : db->find(frame.id, frame.extended);
  if (message == nullptr) {
    return line;
  }
  line += "  " + message->name;
  for (const DecodedSignal& decoded : decode_message(*message, frame)) {
    line += "  " + format_signal(decoded);
  }
  return line;
}

int run_monitor(const Options& o, Session& session, const FilterSet& filters,
                const std::atomic<bool>& stop, std::ostream& out) {
  const Database* db = session.decoder ? &*session.decoder : nullptr;
  const std::string iface = session.source->name();
  StatsCollector stats(o.bitrate);
  LiveView view(db);
  constexpr auto kRefresh = std::chrono::milliseconds(200);
  constexpr const char* kClearScreen = "\x1b[H\x1b[2J";
  auto last_draw = std::chrono::steady_clock::now();
  pump(*session.source, filters, o, stop, [&](const CanFrame& frame) {
    if (!o.live) {
      out << describe(frame, iface, db) << std::endl;
      return;
    }
    stats.add(frame);
    view.update(frame);
    const auto now = std::chrono::steady_clock::now();
    if (now - last_draw >= kRefresh) {
      out << kClearScreen << view.render(stats.snapshot()) << std::flush;
      last_draw = now;
    }
  });
  if (o.live) {
    out << kClearScreen << view.render(stats.snapshot()) << std::flush;
  }
  return 0;
}

int run_record(const Options& o, Session& session, const FilterSet& filters,
               const std::atomic<bool>& stop, std::ostream& err) {
  std::ofstream file(o.output_file);
  if (!file) {
    throw std::runtime_error("cannot write '" + o.output_file + "'");
  }
  const std::string iface = session.source->name();
  std::uint64_t written = 0;
  pump(*session.source, filters, o, stop, [&](const CanFrame& frame) {
    file << format_frame(frame, iface) << '\n';
    ++written;
  });
  file.flush();
  if (!file) {
    throw std::runtime_error("write to '" + o.output_file + "' failed");
  }
  err << "recorded " << written << " frames to " << o.output_file << '\n';
  return 0;
}

int run_stats(const Options& o, Session& session, const FilterSet& filters,
              const std::atomic<bool>& stop, std::ostream& out) {
  StatsCollector stats(o.bitrate);
  pump(*session.source, filters, o, stop, [&](const CanFrame& frame) { stats.add(frame); });
  out << format_report(stats.snapshot());
  return 0;
}

int run_emit(const Options& o, Session& session, const FilterSet& filters,
             const std::atomic<bool>& stop, std::ostream& out) {
  std::unique_ptr<FrameSink> sink;
  if (o.iface.empty()) {
    sink = std::make_unique<StreamSink>(out, session.source->name());
  } else {
    sink = std::make_unique<SocketCan>(o.iface);
  }
  pump(*session.source, filters, o, stop, [&](const CanFrame& frame) { sink->send(frame); });
  return 0;
}

}  // namespace

int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
            const std::atomic<bool>& stop) {
  try {
    const Options options = parse_args(args);
    if (options.command == Command::Help) {
      out << kUsage;
      return 0;
    }
    validate(options);
    const FilterSet filters = build_filters(options);
    Session session = open_session(options, stop, err);
    switch (options.command) {
      case Command::Monitor:
        return run_monitor(options, session, filters, stop, out);
      case Command::Record:
        return run_record(options, session, filters, stop, err);
      case Command::Stats:
        return run_stats(options, session, filters, stop, out);
      case Command::Replay:
      case Command::Generate:
        return run_emit(options, session, filters, stop, out);
      case Command::Help:
        break;
    }
    return 0;
  } catch (const UsageError& e) {
    err << "error: " << e.what() << "\nTry 'canscan --help'.\n";
    return 2;
  } catch (const std::exception& e) {
    err << "error: " << e.what() << '\n';
    return 1;
  }
}

}  // namespace canbus
