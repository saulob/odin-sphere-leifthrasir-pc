# Odin Sphere Leifthrasir PC port: bug report service

A Cloudflare Worker with one SQLite-backed Durable Object. Players' launchers file reports here; only the developer reads
them, in the private dashboard at `/admin`. It is a service of its own: its own Worker, Durable Object, database and secret,
no player identities.

**Nothing here has been deployed.** No Worker exists yet, no secret has been set, and the address below is where the
Worker will probably be once you deploy it, not a live service. The steps under "Deploy" are yours to run.

## What it is

| Path | Method | Who | What |
|---|---|---|---|
| `/health` | GET | anyone | name, protocol and build; no counts |
| `/v1/reports` | POST | launcher | file a report; answers `201 {ticket, receipt, submitted_at}` |
| `/v1/reports/status` | POST | launcher | up to 30 `{ticket, receipt}` claims; answers the status of those that match |
| `/v1/reports/read` | POST | launcher | `{ticket, receipt}`; answers the stored report to its owner |
| `/admin` | GET | you | the dashboard page (static; holds no data) |
| `/admin/api/reports[/<ticket>[/log\|/status]]` | GET, POST, DELETE | you | list, read, download the log, set the status, delete a report; Bearer token |

Requests are JSON over HTTPS (`Content-Type: application/json`). A request that carries an `Origin` header (a web page)
is refused, there are no CORS headers, bodies are cut at 192 KiB while read, and fields are validated before the
Durable Object is touched. Answers: `400 bad_request`, `403 origin_not_allowed`, `404 not_found`, `413 too_large`,
`415 unsupported_media_type`, `429 hourly_limit | daily_limit | limit` (with `Retry-After`), `502 unavailable`.

A ticket is `OSL-` and six characters; a receipt is 64 lowercase hex characters. The fields of a report are those of
`READERS` in `src/reports.js`; `region` is optional and may be empty or left out.

Limits: 3 reports per hour and 10 per day per connection address (IPv6 by /64), 300 a day in all, in-memory bursts
(`src/limits.js`) and an optional edge limit (`REPORT_LIMITER`, 20 requests a minute per address).

Statuses: Open (`new`), Investigating, Fixed (with a version), Cannot reproduce, Duplicate, Not a bug, Closed, and an
optional message to the player of up to 500 characters.

The dashboard lists reports newest first, opens one with its facts and log, saves a status, downloads or copies the log, and
deletes a report after a confirmation. "Keep for this tab" keeps the token and the open report in the tab's session storage
until Sign out (in the menu at the top right) or until the tab closes.

Deleting a report removes its row, its log and its status in one transaction. Its owner's launcher then gets nothing for it:
the status batch leaves it out and a read answers `404 not_found`.

## Privacy

A report holds only the fields the player saw and approved in the launcher before sending: the description, the launcher and
game versions, the game build and region, the title id (CUSA) and executable hash, the operating system, the renderer, GPU,
driver and render resolution, and one privacy-filtered log. No account, name, address, token or other identity is stored or
logged. A report's receipt is kept only as a salted hash. The abuse quota keeps salted hashes of the connection address with a
time and deletes them after a day. There is no telemetry: nothing is sent unless the player presses Send report.

## Data retention

- The newest 500 reports are kept, for at most 180 days.
- A report's status (and the hash that lets its owner ask for it) is kept up to 5000 entries and 365 days.
- Quota rows are deleted after one day.
- You can delete a report at any time from the dashboard.

## Tests (headless, no network, nothing deployed)

```powershell
cd server\odin-bug-reports
npm test                               # node --test "test/*.test.js"; Node 22 or newer; an in-memory database
```

The tests need no npm packages. `test/contract.test.js` is skipped unless `OSL_CPP_REQUEST` names a request file the launcher
built; `tools\test.ps1` runs the launcher's test first and sets it, so the request the real launcher builds is the one the
Worker has to accept. `test/fixtures/request.json` is the canonical request the other tests start from. `test/config.test.js`
checks `wrangler.toml` (own name, own namespace, no other project's values) and the one secret the code reads.

## Deploy (manual; nothing in this repository deploys)

The Worker is named `odin-sphere-bug-reports`; its address is the launcher's `kBugReportServiceUrl`
(`launcher/game_profile.h`). Redeploying is manual, as below.

1. Install Wrangler if you do not have it (`npm install -g wrangler`, or use `npx wrangler`), and sign in with
   `npx wrangler login`. Check the account with `npx wrangler whoami`.
2. `wrangler.toml` holds the `REPORT_LIMITER` rate-limit `namespace_id`. It must be an integer that no other rate-limit
   binding in your Cloudflare account uses; keep the block, it is the edge abuse limit. `tools\worker.ps1` stops before
   Wrangler while it is the placeholder `SET_UNIQUE_ODIN_RATE_LIMIT_NAMESPACE_ID`.
3. `.\tools\worker.ps1 -DryRun` from the repository root checks the files and the configuration and has Wrangler bundle the
   Worker. Nothing is uploaded.
4. Make a random token of 24 or more characters and keep it in your password manager:

   ```powershell
   $bytes = New-Object byte[] 32; [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
   ($bytes | ForEach-Object { $_.ToString('x2') }) -join ''
   ```

5. In this folder, `npx wrangler secret put OSL_BUG_REPORT_ADMIN_TOKEN` and paste the token. If it says the Worker does not
   exist yet, run step 6 first and then this one; until the secret exists `/admin` answers `503 not_configured`.
6. `.\tools\worker.ps1` deploys (`wrangler deploy`). The script never sets a secret and holds no credential.
7. Check `/health` at the address `wrangler deploy` prints, open `/admin`, sign in with the token, and send one
   real report from the launcher to confirm the whole path.

SQLite-backed Durable Objects are offered on Cloudflare's free and paid Workers plans; check Cloudflare's current limits for
yours. `wrangler.toml` creates the Durable Object class `BugReportDesk` (migration `v1`, SQLite) and the `REPORT_LIMITER`
binding. Do not edit an applied migration.

### Pointing the launcher at it

The launcher's compile-time default (`kBugReportServiceUrl`) is the production Worker,
`https://odin-sphere-bug-reports.saulobenigno.workers.dev`. The environment variable `OSL_BUG_REPORT_URL` overrides it for one
run, to try a different deployment.

## Operate

- `npx wrangler tail odin-sphere-bug-reports` shows the Worker's log. It records refusals and errors, never report text,
  receipts, tokens or addresses.
- To rotate the admin token, run `npx wrangler secret put OSL_BUG_REPORT_ADMIN_TOKEN` again; nothing else needs deploying.
- Wrong tokens are limited per address (10, then one every 30 seconds, per isolate).
- To delete every report, deploy a migration that lists `BugReportDesk` under `deleted_classes` (and a new class name under
  `new_sqlite_classes`). This is irreversible.
- To remove the service, `npx wrangler delete`. Launchers then show "Cannot reach the report service" and keep working.
- The `reports` table gains its `region` column on its own when an older table is opened.

## Layout

| File | What |
|---|---|
| `src/worker.js` | the Worker, and the Durable Object class that holds the service |
| `src/api.js` | the public HTTP routes: origin, media type, size, edge limit, JSON, validation |
| `src/service.js` | the service: random values, limits, the transaction that files a report |
| `src/reports.js` | the request fields, tickets, receipts, statuses, the SQL tables, retention |
| `src/limits.js` | the in-memory rate limiter (bounded, pruned) and address grouping |
| `src/admin.js`, `src/admin_page.js` | the dashboard's API and its static page |
| `src/sha256.js` | a synchronous SHA-256 |
| `local/host.js` | the Node host (node:sqlite) the tests use |
| `test/` | the tests; `test/fixtures/request.json` is the canonical request |
