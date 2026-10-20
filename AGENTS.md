# Repository instructions

## Scope

- The maintained implementation is the platform-specific C++ code under `src/linux/` and `src/windows/`.
- The Python version is no longer maintained. Legacy archives are stored outside this project under the workspace's `reference/legacy-history/` directory. Do not inspect or modify their contents; do not include them in source or release packages.
- Linux and Windows are separate implementations with separate `config.yaml` files; do not assume changes in one are shared by the other.
- `src/linux/campus-link-repair.service` is a systemd template containing `<USERNAME>` and `<PROGRAM_DIR>` placeholders. The complete deployment procedure is in `linux_systemd-setup.md`.
- `windows-headless-setup.md` is the step-by-step Task Scheduler guide for running the Windows build unattended (hidden window, start at logon). Update it if the Windows CLI or config keys change.

## Status

- Linux: feature-complete and verified on a real campus network deployment (v2).
- Windows: aligned with Linux on connection/security semantics, config validation, response classification, address selection, and `--self-test`. Still to verify on a real desktop: long-run shutdown, partial thread-creation failure, tray behavior under Windows Terminal.
- Intentional platform asymmetries: `CA_BUNDLE` is Linux-only (WinHTTP has no public option to replace the system trust store; install internal CAs via certmgr.msc); `DEBUG_RESPONSE` and `PROXY_RECOVERY` are Windows-only. Windows proxy recovery lives in `src/windows/ProxyRecovery.h`, included by the main source; the single-source build command still applies.
- Historical audit and change log (v1 to v2) are read-only files in the external legacy archive. Record new work in `dev-log/`, not in those archives.

## Build and verification

- No build system, formatter, linter, type checker, or CI workflow is present. Verify C++ changes by compiling the affected source directly. A standalone Windows proxy recovery integration test is available under `tests/windows/`.
- Linux (run from `src/linux/`, with the libcurl development package installed): `g++ campus-link-repair.cpp -o campus-link-repair -lcurl -O2`
- Windows MinGW (run from `src/windows/`): `g++ campus-link-repair.cpp -o campus-link-repair.exe -lwinhttp -liphlpapi -lws2_32 -lshell32 -luser32 -static`
- Quick offline check (no config, no external network; exit 0 = pass): `./campus-link-repair --self-test` (Linux, 19 cases) or `campus-link-repair.exe --self-test` (Windows, 47 cases: 21 response/address checks plus 26 proxy endpoint and recovery policy checks). Windows additionally rejects decimal/exponent result prefixes before treating authentication as a recovery prerequisite.
- Windows integration test (run from repo root): `g++ tests/windows/proxy-recovery-test.cpp -o <TEMP>/proxy-recovery-test.exe -lwinhttp -liphlpapi -lws2_32 -lshell32 -luser32 -static -O2`, then execute it. It uses real loopback TCP probes and mocked WinINet APIs; it never writes the developer's live proxy settings. Expected warnings from deliberately simulated notification/authentication failures are printed during the test.
- Windows `--diagnose-network` is read-only: it loads no account config, queries current-user LAN proxy settings and probes loopback TCP ports; it never changes proxy settings or contacts the portal.
- `PROXY_RECOVERY` accepts `auto` (default), `detect`, or `off`. Automatic changes require repeated loopback connection refusals spanning at least 30 seconds and successful direct portal authentication. Remote/mixed proxies, live listeners, timeouts, and unknown formats remain untouched; PAC recovery is restricted to loopback HTTP(S) URLs. Only enable flags change; endpoints, bypass rules, auto-discovery, DNS, routes, and TUN interfaces are preserved. Do not run setting-mutating recovery against the developer's live profile as a test; use a mocked API or a disposable Windows account.
- The C++ programs are long-running network clients; a real run requires valid credentials, the adjacent config file, and access to the configured campus portal. Do not expose `config.yaml` or full logs because they contain credentials and may contain IPs/portal responses.

## Runtime details

- Linux C++ reads `config.yaml` from the process working directory, so `config.yaml` must be beside the executable and systemd `WorkingDirectory` must point there.
- Windows C++ resolves `config.yaml` relative to the executable directory.
- Both binaries return exit code `78` for missing/invalid account configuration; the Linux service uses `RestartPreventExitStatus=78` to avoid restart loops.
- Windows launched by double-click waits for Enter before exiting when the exit code is non-zero, so errors remain visible; interactive-console detection must keep pipeline/hidden-console/service launches unblocked.
- `SERVER_IP` pins the connect address but requests still send the portal `Host` header and keep the domain for SNI/certificate validation; `LOGIN_IP` overrides the client IPv4 submitted to the portal. Treat both as deployment-specific settings, not code defaults.

## Editing conventions

- Keep platform-specific behavior in its existing platform source; avoid introducing a cross-platform abstraction unless both implementations actually need it.
- Preserve the existing line-ending style (LF) and concise C++ style. Do not add dependencies or generated/build artifacts to the repository.
- Project identity is CampusLinkRepair (校园网连接修复助手), GitHub repository `cunyk/CampusLinkRepair-cqu`, executable/service basename `campus-link-repair`. Keep portal hostnames and the upstream MIT copyright notice intact when changing product branding.
