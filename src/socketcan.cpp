#include "canbus/socketcan.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace canbus {
namespace {

constexpr int kPollTimeoutMs = 100;

[[noreturn]] void throw_errno(const std::string& what) {
  throw std::runtime_error(what + ": " + std::strerror(errno));
}

}  // namespace

FileDescriptor::~FileDescriptor() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

SocketCan::SocketCan(const std::string& iface) : iface_(iface) {
  if (iface.empty() || iface.size() >= IFNAMSIZ) {
    throw std::runtime_error("invalid CAN interface name '" + iface + "'");
  }
  fd_ = FileDescriptor(::socket(PF_CAN, SOCK_RAW, CAN_RAW));
  if (fd_.get() < 0) {
    throw_errno("cannot open CAN socket");
  }
  ifreq request{};
  std::memcpy(request.ifr_name, iface.c_str(), iface.size());
  if (::ioctl(fd_.get(), SIOCGIFINDEX, &request) < 0) {
    throw_errno("cannot find interface '" + iface + "'");
  }
  const int enable = 1;
  if (::setsockopt(fd_.get(), SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof enable) < 0) {
    throw_errno("cannot enable CAN FD on '" + iface + "'");
  }
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = request.ifr_ifindex;
  if (::bind(fd_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) < 0) {
    throw_errno("cannot bind to '" + iface + "'");
  }
}

ReadStatus SocketCan::next(CanFrame& out) {
  pollfd poll_fd{};
  poll_fd.fd = fd_.get();
  poll_fd.events = POLLIN;
  const int ready = ::poll(&poll_fd, 1, kPollTimeoutMs);
  if (ready < 0) {
    if (errno == EINTR) {
      return ReadStatus::Timeout;
    }
    throw_errno("poll failed on '" + iface_ + "'");
  }
  if (ready == 0) {
    return ReadStatus::Timeout;
  }

  canfd_frame raw{};
  const ssize_t got = ::read(fd_.get(), &raw, sizeof raw);
  if (got < 0) {
    if (errno == EINTR || errno == EAGAIN) {
      return ReadStatus::Timeout;
    }
    throw_errno("read failed on '" + iface_ + "'");
  }
  const auto size = static_cast<std::size_t>(got);
  if ((size != CAN_MTU && size != CANFD_MTU) || (raw.can_id & CAN_ERR_FLAG) != 0) {
    return ReadStatus::Timeout;
  }

  out = CanFrame{};
  out.timestamp = unix_time_now();
  out.extended = (raw.can_id & CAN_EFF_FLAG) != 0;
  out.rtr = (raw.can_id & CAN_RTR_FLAG) != 0;
  out.id = raw.can_id & (out.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
  out.fd = size == CANFD_MTU;
  if (out.fd) {
    out.brs = (raw.flags & CANFD_BRS) != 0;
    out.esi = (raw.flags & CANFD_ESI) != 0;
  }
  out.len = static_cast<std::uint8_t>(std::min<std::size_t>(raw.len, out.fd ? kMaxDataLen : 8));
  if (!out.rtr) {
    std::copy_n(raw.data, out.len, out.data.begin());
  }
  return ReadStatus::Frame;
}

void SocketCan::send(const CanFrame& frame) {
  canfd_frame raw{};
  raw.can_id = frame.id | (frame.extended ? CAN_EFF_FLAG : 0u) | (frame.rtr ? CAN_RTR_FLAG : 0u);
  raw.len = frame.len;
  if (frame.fd) {
    raw.flags = static_cast<__u8>((frame.brs ? CANFD_BRS : 0) | (frame.esi ? CANFD_ESI : 0));
  }
  if (!frame.rtr) {
    std::copy_n(frame.data.begin(), frame.len, raw.data);
  }
  const std::size_t size = frame.fd ? CANFD_MTU : CAN_MTU;
  const ssize_t sent = ::write(fd_.get(), &raw, size);
  if (sent < 0 || static_cast<std::size_t>(sent) != size) {
    throw_errno("send failed on '" + iface_ + "'");
  }
}

}  // namespace canbus
