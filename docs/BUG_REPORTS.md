# Bug reports

The launcher's **Bug Report** page sends one privacy-filtered game log, with a description, to a small report service
that only the developer reads. Nothing is sent until the player presses **Send report** and confirms. The service is a
Cloudflare Worker of its own, in [server/odin-bug-reports](../server/odin-bug-reports/README.md); it shares nothing with
any other project's service.

**Status: the service is not deployed yet.** `kBugReportServiceUrl` in `launcher/game_profile.h` is empty, so a build
made from this source has no service: the Bug Report page says so, keeps the text, and offers **Save report** and the
GitHub issue tracker instead. Reporting on GitHub stays the way to report problems until the service is live.

## What happens

1. The player describes the problem (10 to 500 characters) and picks one of the five newest game logs. The most recent
   gameplay session that has ended is chosen by default.
2. **Preview** shows exactly what would be sent. **Send report** builds the same report, shows it in the preview and asks
   to confirm. Only the confirmation opens a connection.
3. The launcher makes one outgoing HTTPS request, off the UI thread, with a short timeout. The service answers with a
   ticket (`OSL-XXXXXX`) and a private receipt. The page shows the ticket; the receipt is kept in
   `user\bug_reports.json` and is sent only to ask about that one report.
4. **My Reports** lists the reports sent from this PC (the newest 30). The status, the fixed version and the developer's
   reply are checked off the UI thread when the launcher starts, when the page opens and on **Refresh**, and only when the
   list is not empty and a service is configured. A change marks the report **Updated** and puts a dot on the sidebar
   entry until that report is opened; a new developer message is a **New reply**. A report the service no longer has
   shows **No longer available** with its last known status. **What was sent** reads the stored report back.

If the PC is offline or blocked by a firewall, the page says so and keeps the text. **Save report** writes the same
filtered report to `logs\reports\` for attaching to a GitHub issue. The game is fully playable without any of this.

## What is sent

Exactly the fields of the preview, and nothing else:

| Field | Source |
|---|---|
| description | typed by the player, filtered, at most 500 characters |
| launcher_version | the port version |
| game_build, game_version, region, cusa, eboot_sha256 | the installed game (label, version, region, title id, hash of its executable) |
| operating_system | e.g. `Windows 11 (build 26200)` |
| renderer, gpu, driver, render_resolution | read from the start of the selected log, filtered |
| log_file | the name of the chosen log, `odin_sphere_leifthrasir_NNN.log` |
| log_clipped | whether only the end of the log was kept |
| log_text | the end of the chosen log after the privacy filter, at most 64 KiB |

Not sent: saves, game files, screenshots, settings files, crash dumps, other logs. No report is sent for a crash by
itself, and no log is sent without the player's confirmation.

## Privacy

`launcher/log_scrubber.cpp` filters the description and the log before they are shown or sent. It removes or masks
Windows and POSIX user names, computer names, profile and home folders, other local folders outside Windows' own, network
share paths, IPv4 and IPv6 addresses, account numbers, tokens, passwords and other labelled secrets, and control
characters. It keeps what a report is for: versions, the operating system, the renderer, the graphics card, driver and
audio diagnostics, error messages, and the names (not the folders) of files. **Automatic filtering cannot promise to catch
every private value**, which is why the page shows the preview and says so.

- No telemetry, no usage statistics, no heartbeat and no background traffic are sent.
- There is no installation, player or account identity, and no GitHub login. Each report gets a random ticket and a
  random receipt; the service keeps only a salted hash of the receipt.
- The service sees the connection's address while it answers, to limit abuse. It does not store it with the report: the
  per-address quota keeps a salted hash for one day. The network is not anonymous: the address is visible to the
  connection's path and to Cloudflare.
- The newest 500 reports are kept for 180 days; their statuses for 365 days.

## Network behaviour

- Outgoing HTTPS only (port 443), with the system's certificate validation, TLS 1.2 or newer, no redirects. Plain
  `http://` is refused. Windows uses WinHTTP. Linux runs the system's `curl` without a shell, with the address as an
  argument; without `curl` the page reports that the service is unreachable.
- The launcher never listens, binds or opens a port, does not touch Windows Firewall, and does not ask for administrator
  rights.
- Connections happen from Send report (after confirming), What was sent, Refresh and the automatic status check, which
  asks only about reports already in My Reports. `tests/bug_report_tests.cpp` checks this in the source.
- Timeouts: 8 s to connect, 15 s to send, 20 s to receive, 30 s in all.
- The game process has no bug report code.

## Service address

`OSL_BUG_REPORT_URL` overrides `kBugReportServiceUrl` for one run (https only, `https://host[:port]`). When the Worker is
deployed, put its address into `kBugReportServiceUrl`. Deployment is in the
[service README](../server/odin-bug-reports/README.md).

## Tests

`tools\test.ps1` builds and runs `osl-bug-report-tests` (no window, no network: a stand-in transport and synthetic logs)
and then the Worker's tests with Node's own runner on an in-memory database. The launcher test leaves the request its
client builds, and the Worker test checks that the service accepts it.
