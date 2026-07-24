# Security Policy

## Supported versions

| Version | Supported |
| --- | --- |
| 1.x | Yes |
| < 1.0 | No |

## Reporting a vulnerability

Please do not open a public issue for security problems.

Report them privately through GitHub Security Advisories:
[Report a vulnerability](https://github.com/bekirhankarakurt/can-bus-analyzer/security/advisories/new).

Include the affected version, a description of the impact and, if possible, a minimal
log or DBC file that reproduces the problem. Expect an initial response within a week.
Parsers for candump logs and DBC files are the main attack surface; crashes or
out-of-bounds reads triggered by malformed input are treated as security issues.
