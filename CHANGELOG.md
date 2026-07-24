# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-11-04

### Added
- `canscan` command line with `monitor`, `record`, `stats`, `replay` and `generate` commands.
- SocketCAN capture and transmit for classical CAN and CAN FD, with identifier filters (`ID`, `ID:MASK`, `ID~MASK`).
- DBC parser (`BO_`, `SG_`, `BA_ "GenMsgCycleTime"`) with Intel/Motorola byte order, signed/unsigned signals, factor/offset and extended IDs.
- Live decoded signal table and bus-load statistics with per-ID periods.
- candump-compatible recording and timed replay, including CAN FD and RTR frames.
- Traffic generator driven by a DBC, plus log replay as hardware-free input for every command.
- `scripts/setup_vcan.sh` to create a CAN FD capable virtual interface.
- Unit tests via CTest, GitHub Actions CI (GCC, Clang, ASan/UBSan, static analysis, coverage), CodeQL, API documentation and release packaging.

[1.0.0]: https://github.com/bekirhankarakurt/can-bus-analyzer/releases/tag/v1.0.0
