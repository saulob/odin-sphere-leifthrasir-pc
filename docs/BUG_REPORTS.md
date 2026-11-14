# Bug reports

The launcher's **Bug Report** page sends one privacy-filtered game log, with a description, to a small report service
that only the developer reads. Nothing is sent until the player presses **Send report**. The service is a Cloudflare
Worker of its own, in [server/odin-bug-reports](../server/odin-bug-reports/README.md); it shares nothing with any other
project's service.

The production service is `https://odin-sphere-bug-reports.saulobenigno.workers.dev`. It is `kBugReportServiceUrl` in
`launcher/game_profile.h`, so a normal build sends there with no configuration. `/health` answers
`service: odin-sphere-bug-reports`, `protocol: 1` and `status: ok`.

## What happens

1. The player describes the problem (10 to 500 characters) and picks one of the five newest game logs. The most recent
   gameplay session that has ended is chosen by default.
2. **Preview** shows exactly what would be sent, after the privacy filter. **Send report** builds the same report and
   sends it. Only that button opens a connection to send.
3. The launcher makes one outgoing HTTPS request, off the UI thread, with a short timeout. The service answers with a
   ticket (`OSL-XXXXXX`) and a private receipt. The page shows the ticket; the receipt is kept in
   `user\bug_reports.json` and is sent only to ask about that one report.
4. **My Reports** lists the reports sent from this PC (the newest 30). The status, the fixed version and the developer's
   reply are checked off the UI thread when the launcher starts, when the page opens and on **Refresh**, and only when the
   list is not empty. See "My Reports" below.

The page opens with the privacy note, then the four steps: report the problem, choose one game log, preview what will be
sent, send. Sending shows a status card; a success shows the report ID with **Copy report ID**. If the PC is offline, the
service is busy or a limit was reached, the card says so and the description and the chosen log are kept. Under the send
row, **Save report** writes the same filtered report to `logs\reports\`, and the GitHub issue tracker stays available. The
game is fully playable without any of this.

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
itself, and no log is sent without the player pressing Send report.

## Privacy

`launcher/log_scrubber.cpp` filters the description and the log before they are shown or sent. It removes or masks
Windows and POSIX user names, computer names, profile and home folders, other local folders outside Windows' own, network
share paths, IPv4 and IPv6 addresses, email addresses, account numbers, tokens, passwords and other labelled secrets, and
control characters. It keeps what a report is for: versions, the operating system, the renderer, the graphics card, driver and
audio diagnostics, error messages, and the names (not the folders) of files. **Automatic filtering cannot promise to catch
every private value**, which is why the page shows the preview and says so.

- No telemetry, no usage statistics, no heartbeat and no background traffic are sent.
- There is no installation, player or account identity, and no GitHub login. Each report gets a random ticket and a
  random receipt; the service keeps only a salted hash of the receipt.
- The service sees the connection's address while it answers, to limit abuse. It does not store it with the report: the
  per-address quota keeps a salted hash for one day. The network is not anonymous: the address is visible to the
  connection's path and to Cloudflare.
- The newest 500 reports are kept for 180 days; their statuses for 365 days.

The preview is the exact text of the report that Send report transmits: the same package is built once, shown and sent,
and `tests/bug_report_tests.cpp` checks that every field of the request appears in the preview.

## My Reports

- The history is `user\bug_reports.json`, the newest 30 reports, each with its receipt. It is written to a new file and
  moved into place; a damaged file is set aside as `bug_reports.json.damaged` and the list starts empty. **Remove from this PC**
  deletes only the local entry.
- A status change marks the report **Updated**. A developer message that is new or changed is a **New reply**: the row shows
  a New reply marker, the page shows "A report has a new reply" with **Show**, and a reply comes before a plain update.
- Opening the Bug Report page clears nothing. A report counts as seen only when the player opens that report, which also
  shows the **Message from developer** panel, highlighted while the reply is new.
- When the service answers a status check but leaves out a report that was asked about with its receipt, the report is marked
  **No longer available**. Its last known status and message stay, it stops counting as an update, and it is not asked about
  again automatically; **Refresh** asks again. The service gives the same answer for a report that was deleted, one that
  expired and a wrong receipt.
- The checks run off the UI thread; offline, they fail silently, and **Refresh** says so.

## Receipts

The service answers a submission with a ticket, a private receipt and the time. The receipt is 32 random bytes in hex, shown
to nobody, kept in `user\bug_reports.json`, and stored by the service only as a salted hash. Status and read requests carry
the ticket and its receipt; a wrong receipt and an unknown ticket get the same answer. There is no account behind it: whoever
holds the file can ask about those reports.

## Limits and retention

| Limit | Value |
|---|---|
| request body | 192 KiB, checked before and while reading |
| description | 10 to 500 characters |
| log | 64 KiB of UTF-8 |
| reports per address | 3 an hour, 10 a day (IPv6 counted by /64) |
| reports in all | 300 a day |
| status check | 30 reports per request |
| reports kept | newest 500, at most 180 days |
| statuses kept | 5000, at most 365 days |

Burst limits (per address and global) live in memory and cost no database write; Cloudflare's edge limiter in front of the
Worker (20 requests a minute per address) refuses before the database is woken. Answers use `400`, `403`, `404`, `413`,
`415`, `429` with `Retry-After`, and `502`, never a stack trace.

## Admin dashboard

`/admin` is the developer's page (the production address plus `/admin`); it is a static page; its data comes from `/admin/api/...` behind a Bearer token, the Worker secret
`OSL_BUG_REPORT_ADMIN_TOKEN` (at least 24 characters, compared by digest, wrong tries limited per address). The token is
never in the page or in the logs, and cross-origin calls are refused. The developer lists reports, reads one with its complete
log, copies or downloads the log, sets a status (Open, Investigating, Fixed with a version, Cannot reproduce, Duplicate, Not a
bug, Closed) with a message for the player, and deletes a report after a confirmation. Deleting removes the report, its log,
its status, its message and its receipt hash; the player's launcher then sees the report as no longer available.

## Network behaviour

- Outgoing HTTPS only (port 443), with the system's certificate validation, TLS 1.2 or newer, no redirects. Plain
  `http://` is refused. Windows uses WinHTTP. Linux runs the system's `curl` without a shell, with the address as an
  argument; without `curl` the page reports that the service is unreachable.
- The launcher never listens, binds or opens a port, does not touch Windows Firewall, and does not ask for administrator
  rights.
- Connections happen from Send report, What was sent, Refresh and the automatic status check, which asks only about
  reports already in My Reports. Opening the page sends nothing. `tests/bug_report_tests.cpp` checks this in the source.
- Timeouts: 8 s to connect, 15 s to send, 20 s to receive, 30 s in all.
- The game process has no bug report code.

## Service address

A normal launcher needs no setting. `OSL_BUG_REPORT_URL` is a development override for one run (https only,
`https://host[:port]`); an empty value is ignored and an invalid one disables sending for that run instead of falling back
to production. Deployment is in the [service README](../server/odin-bug-reports/README.md).

## Deploying the Worker

The production Worker is deployed. Redeploying is manual, from the repository root; nothing in the build or the tests deploys:

1. Set `namespace_id` in `server\odin-bug-reports\wrangler.toml` to an integer no other rate-limit binding in the
   Cloudflare account uses. `tools\worker.ps1` refuses to continue while it is the placeholder.
2. `.\tools\worker.ps1 -DryRun` checks the files and the configuration and has Wrangler bundle the Worker; nothing is uploaded.
3. Create the token and store it with `npx wrangler secret put OSL_BUG_REPORT_ADMIN_TOKEN` in `server\odin-bug-reports`.
   The script never does this.
4. `.\tools\worker.ps1` deploys.
5. Open `/health`, then `/admin`, then send one real report from the launcher.
6. The launcher already points at the production address; change `kBugReportServiceUrl` only when the address changes.

## Tests

`tools\test.ps1` builds only the test targets, incrementally, and runs them in groups (Runtime, CPU, Packages, Launcher,
Updater, Features, Graphics), continuing after a failure and exiting non-zero if any failed. The Launcher group has
`osl-bug-report-tests` (no window, no network: a stand-in transport and synthetic logs), the Worker's tests with Node's own
runner on an in-memory database, and a check of `worker.ps1` against a stand-in `npx`. The launcher test leaves the request
its client builds, and the Worker test checks that the service accepts exactly that request; the script deletes any earlier
copy first. The Updater group includes the loopback network tests. `npm test` in `server\odin-bug-reports` runs the Worker
tests alone (the launcher contract test is skipped without the request file).
