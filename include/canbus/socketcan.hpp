#pragma once

#include <string>

#include "canbus/source.hpp"

namespace canbus {

// Owns a file descriptor and closes it on destruction.
class FileDescriptor {
 public:
  FileDescriptor() noexcept = default;
  explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
  ~FileDescriptor();
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  FileDescriptor(FileDescriptor&& other) noexcept;
  FileDescriptor& operator=(FileDescriptor&& other) noexcept;

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_ = -1;
};

// A raw SocketCAN socket bound to one interface, with CAN FD frames enabled.
// For CAN FD traffic the interface MTU must be 72 (see scripts/setup_vcan.sh).
class SocketCan final : public FrameSource, public FrameSink {
 public:
  // Throws std::runtime_error if the interface cannot be opened.
  explicit SocketCan(const std::string& iface);

  // Waits up to 100 ms for a frame; error frames are skipped.
  ReadStatus next(CanFrame& out) override;
  void send(const CanFrame& frame) override;
  [[nodiscard]] std::string name() const override { return iface_; }

 private:
  FileDescriptor fd_;
  std::string iface_;
};

}  // namespace canbus
