// Problem reports filed from the launcher's Bug Report page. A filed report holds only the fields below, all of them
// shown to the player first. Who filed it stays apart: the quota keeps salted hashes of the connection's address for a
// day, and ownership is a random receipt of which only a hash is kept.
import { digestsMatch, sha256Hex } from './sha256.js';

export const REPORT = {
  bodyBytes: 192 * 1024,
  descriptionChars: 500,
  descriptionMinChars: 10,
  logBytes: 64 * 1024,
  metadataChars: 64,
  detailChars: 96,
  previewChars: 140,
  perAddressPerHour: 3,
  perAddressPerDay: 10,
  perDay: 300,
  kept: 500,
  keptDays: 180,
  messageChars: 500,
  statusBatch: 30,
  statusKept: 5000,
  statusKeptDays: 365,
};

export const REPORT_STATUSES = ['new', 'investigating', 'fixed', 'cannot_reproduce', 'duplicate', 'not_a_bug', 'closed'];

const HOUR_MS = 3600 * 1000;
const DAY_MS = 24 * HOUR_MS;
const LOG_FILE = /^odin_sphere_leifthrasir_[0-9]{3,10}\.log$/;
const VERSION = /^[0-9A-Za-z][0-9A-Za-z.+-]{0,31}$/;
const TICKET = /^OSL-[A-HJ-NP-Z2-9]{6}$/;
const RECEIPT = /^[0-9a-f]{64}$/;
const STATUS_FIELDS = new Set(['status', 'fixed_version', 'player_message']);
const TICKET_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

// OSL-XXXXXX from six random bytes; the alphabet has 32 letters, so each byte maps without bias.
export const ticketCode = (bytes) => `OSL-${Array.from(bytes.subarray(0, 6), (b) => TICKET_ALPHABET[b % 32]).join('')}`;

export const isTicket = (text) => typeof text === 'string' && TICKET.test(text);

// Built from the two validated names only, so nothing the client sent beyond them reaches a header.
export function logDownloadName(report) {
  if (!isTicket(report.ticket)) return 'odin-sphere-leifthrasir-report.log';
  return LOG_FILE.test(report.log_file) ? `${report.ticket}-${report.log_file}` : `${report.ticket}.log`;
}

const utf8Bytes = (text) => new TextEncoder().encode(text).length;

// What the launcher's filter leaves in text: line feeds and tabs, no other control characters, no carriage returns.
// eslint-disable-next-line no-control-regex
const HAS_FORBIDDEN = /[\u0000-\u0008\u000b-\u001f\u007f-\u009f]/;
// eslint-disable-next-line no-control-regex
const HAS_CONTROL = /[\u0000-\u001f\u007f-\u009f]/;

class Refusal extends Error {
  constructor(code, detail) {
    super(detail);
    this.code = code;
  }
}

function text(msg, field, maxChars, { optional = false, pattern = null } = {}) {
  const value = msg[field];
  if (typeof value !== 'string' || HAS_CONTROL.test(value) || value !== value.trim()) throw new Refusal('bad_request', field);
  if (!value && !optional) throw new Refusal('bad_request', field);
  if (Array.from(value).length > maxChars) throw new Refusal('too_large', field);
  if (value && pattern && !pattern.test(value)) throw new Refusal('bad_request', field);
  return value;
}

function description(msg) {
  const value = msg.description;
  if (typeof value !== 'string' || HAS_FORBIDDEN.test(value) || value.includes('\r')) throw new Refusal('bad_request', 'description');
  if (Array.from(value.trim()).length < REPORT.descriptionMinChars) throw new Refusal('bad_request', 'description too short');
  if (Array.from(value).length > REPORT.descriptionChars) throw new Refusal('too_large', 'description');
  return value;
}

function logText(msg) {
  const value = msg.log_text;
  if (typeof value !== 'string') throw new Refusal('bad_request', 'log_text');
  if (utf8Bytes(value) > REPORT.logBytes) throw new Refusal('too_large', 'log_text');
  if (HAS_FORBIDDEN.test(value) || value.includes('\r')) throw new Refusal('bad_request', 'log_text');
  if (!value.trim()) throw new Refusal('bad_request', 'empty log');
  return value;
}

function yesNo(msg, field) {
  if (msg[field] !== 'yes' && msg[field] !== 'no') throw new Refusal('bad_request', field);
  return msg[field];
}

const meta = (field, options) => (msg) => text(msg, field, REPORT.metadataChars, options);
const detail = (field) => (msg) => text(msg, field, REPORT.detailChars, { optional: true });

export const READERS = {
  description,
  launcher_version: meta('launcher_version', { pattern: VERSION }),
  game_build: meta('game_build'),
  region: (msg) => (msg.region === undefined ? '' : text(msg, 'region', REPORT.metadataChars, { optional: true })),
  game_version: meta('game_version', { optional: true, pattern: /^[0-9][0-9.]{0,15}$/ }),
  cusa: meta('cusa', { optional: true, pattern: /^CUSA[0-9]{5}$/ }),
  eboot_sha256: meta('eboot_sha256', { optional: true, pattern: /^[0-9a-f]{64}$/ }),
  operating_system: meta('operating_system'),
  renderer: meta('renderer', { optional: true }),
  gpu: detail('gpu'),
  driver: detail('driver'),
  render_resolution: meta('render_resolution', { optional: true }),
  log_file: meta('log_file', { pattern: LOG_FILE }),
  log_clipped: (msg) => yesNo(msg, 'log_clipped'),
  log_text: logText,
};

// The stored fields of a request, or { code, detail } for the answer. Nothing here trusts a size the client states, and
// a field the page could not have shown is refused rather than dropped.
export function readReport(body) {
  if (body === null || typeof body !== 'object' || Array.isArray(body)) return { code: 'bad_request', detail: 'body' };
  for (const key of Object.keys(body)) {
    if (!Object.hasOwn(READERS, key)) return { code: 'bad_request', detail: key };
  }
  try {
    const report = {};
    for (const [field, read] of Object.entries(READERS)) report[field] = read(body);
    return { report };
  } catch (error) {
    if (error instanceof Refusal) return { code: error.code, detail: error.message };
    throw error;
  }
}

// One report's receipt from a lookup, or null. Nothing here says whether the report exists.
export function readClaim(entry) {
  if (entry === null || typeof entry !== 'object' || Array.isArray(entry)) return null;
  const { ticket, receipt } = entry;
  return isTicket(ticket) && typeof receipt === 'string' && RECEIPT.test(receipt) ? { ticket, receipt } : null;
}

export function readReadClaim(body) {
  if (body === null || typeof body !== 'object' || Object.keys(body).some((key) => key !== 'ticket' && key !== 'receipt')) return null;
  return readClaim(body);
}

// The claims of a status request, or null when it is malformed or too long.
export function readClaims(body) {
  if (body === null || typeof body !== 'object' || Object.keys(body).some((key) => key !== 'reports')) return null;
  const { reports } = body;
  if (!Array.isArray(reports) || reports.length > REPORT.statusBatch) return null;
  const claims = reports.map(readClaim);
  return claims.includes(null) ? null : claims;
}

// An admin's change: { change } or { detail }. A fixed version exists only on a Fixed report, so a stale one never shows.
export function readStatusChange(body) {
  if (body === null || typeof body !== 'object' || Array.isArray(body)) return { detail: 'body' };
  for (const key of Object.keys(body)) if (!STATUS_FIELDS.has(key)) return { detail: key };
  const { status, fixed_version: version = '', player_message: message = '' } = body;
  if (!REPORT_STATUSES.includes(status)) return { detail: 'status' };
  if (typeof version !== 'string' || (status === 'fixed' && version && !VERSION.test(version))) return { detail: 'fixed_version' };
  if (typeof message !== 'string' || HAS_FORBIDDEN.test(message) || message.includes('\r')) return { detail: 'player_message' };
  if (Array.from(message).length > REPORT.messageChars) return { detail: 'player_message' };
  return { change: { status, fixed_version: status === 'fixed' ? version : '', player_message: message.trim() } };
}

const LIST_COLUMNS = `reports.ticket AS ticket, ms, launcher_version, game_build, region, game_version, operating_system, renderer, gpu,
  driver, render_resolution, log_file, log_clipped, substr(description, 1, ${REPORT.previewChars}) AS preview,
  COALESCE(t.status, 'new') AS status, COALESCE(t.status_ms, ms) AS status_updated_at`;

const statusOf = (row) => ({
  status: row.status,
  status_updated_at: row.status_ms,
  fixed_version: row.fixed_version,
  player_message: row.player_message,
});

// The report store. `sql` is { all, get, run } over a SQLite database.
export class ReportDesk {
  constructor(sql, salt) {
    this.sql = sql;
    this.salt = salt;
    sql.run(`CREATE TABLE IF NOT EXISTS reports (ticket TEXT PRIMARY KEY, ms INTEGER NOT NULL,
      launcher_version TEXT NOT NULL, game_build TEXT NOT NULL, region TEXT NOT NULL DEFAULT '', game_version TEXT NOT NULL,
      cusa TEXT NOT NULL, eboot_sha256 TEXT NOT NULL, operating_system TEXT NOT NULL, renderer TEXT NOT NULL,
      gpu TEXT NOT NULL, driver TEXT NOT NULL, render_resolution TEXT NOT NULL, description TEXT NOT NULL,
      log_file TEXT NOT NULL, log_clipped TEXT NOT NULL, log_text TEXT NOT NULL)`);
    if (!sql.all('PRAGMA table_info(reports)').some((column) => column.name === 'region')) {
      sql.run("ALTER TABLE reports ADD COLUMN region TEXT NOT NULL DEFAULT ''");
    }
    sql.run('CREATE TABLE IF NOT EXISTS report_quota (address_key TEXT NOT NULL, ms INTEGER NOT NULL)');
    sql.run(`CREATE TABLE IF NOT EXISTS report_tracking (ticket TEXT PRIMARY KEY, receipt_hash TEXT NOT NULL,
      submitted_ms INTEGER NOT NULL, status TEXT NOT NULL, status_ms INTEGER NOT NULL, fixed_version TEXT NOT NULL,
      player_message TEXT NOT NULL)`);
  }

  key(kind, value) {
    return sha256Hex(`${this.salt}|${kind}|${value}`).slice(0, 32);
  }

  // The answer code that refuses a report now, or null. The windows roll: a report leaves them one hour and one day after
  // it was accepted. The shorter window comes first, because that is the one to wait for.
  refusal(address, now) {
    this.sql.run('DELETE FROM report_quota WHERE ms <= ?', now - DAY_MS);
    const key = this.key('address', address);
    const used = this.sql.get(`SELECT COUNT(*) AS everyone, COALESCE(SUM(address_key = ?), 0) AS address_day,
      COALESCE(SUM(address_key = ? AND ms > ?), 0) AS address_hour FROM report_quota`, key, key, now - HOUR_MS);
    if (used.address_hour >= REPORT.perAddressPerHour) return 'hourly_limit';
    if (used.address_day >= REPORT.perAddressPerDay) return 'daily_limit';
    if (used.everyone >= REPORT.perDay) return 'limit';
    return null;
  }

  file(report, ticket, address, now) {
    this.purge(now);
    this.sql.run(`INSERT INTO reports (ticket, ms, launcher_version, game_build, region, game_version, cusa, eboot_sha256,
      operating_system, renderer, gpu, driver, render_resolution, description, log_file, log_clipped, log_text)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`, ticket, now, report.launcher_version, report.game_build,
    report.region, report.game_version, report.cusa, report.eboot_sha256, report.operating_system, report.renderer, report.gpu,
    report.driver, report.render_resolution, report.description, report.log_file, report.log_clipped, report.log_text);
    this.sql.run('INSERT INTO report_quota (address_key, ms) VALUES (?, ?)', this.key('address', address), now);
    this.sql.run(`DELETE FROM reports WHERE rowid IN
      (SELECT rowid FROM reports ORDER BY ms DESC, rowid DESC LIMIT -1 OFFSET ?)`, REPORT.kept);
  }

  // Reports and their statuses leave after their own number of days, whatever the count limits say.
  purge(now) {
    this.sql.run('DELETE FROM reports WHERE ms <= ?', now - REPORT.keptDays * DAY_MS);
    this.sql.run('DELETE FROM report_tracking WHERE submitted_ms <= ?', now - REPORT.statusKeptDays * DAY_MS);
  }

  receiptHash(receipt) {
    return sha256Hex(`${this.salt}|receipt|${receipt}`);
  }

  // A status outlives the log it came with, so its table has a longer limit of its own.
  track(ticket, receipt, now) {
    this.sql.run(`INSERT INTO report_tracking (ticket, receipt_hash, submitted_ms, status, status_ms, fixed_version, player_message)
      VALUES (?, ?, ?, 'new', ?, '', '')`, ticket, this.receiptHash(receipt), now, now);
    this.sql.run(`DELETE FROM report_tracking WHERE rowid IN
      (SELECT rowid FROM report_tracking ORDER BY submitted_ms DESC, rowid DESC LIMIT -1 OFFSET ?)`, REPORT.statusKept);
  }

  // Every claim costs the same hash, so a wrong receipt and an unknown ticket cannot be told apart.
  claimed({ ticket, receipt }) {
    const hash = this.receiptHash(receipt);
    const row = this.sql.get('SELECT * FROM report_tracking WHERE ticket = ?', ticket);
    return row && digestsMatch(row.receipt_hash, hash) ? row : null;
  }

  // What the launcher may know of the claimed reports; a claim that fails is left out.
  statuses(claims) {
    return claims.map((claim) => this.claimed(claim)).filter(Boolean).map((row) => ({
      ticket: row.ticket,
      status: row.status,
      status_updated_at: row.status_ms,
      ...(row.fixed_version ? { fixed_version: row.fixed_version } : {}),
      ...(row.player_message ? { player_message: row.player_message } : {}),
    }));
  }

  // The stored report for its owner, or null once its log is gone.
  report(claim) {
    const row = this.claimed(claim);
    const stored = row && this.get(row.ticket);
    if (!stored) return null;
    const { ms, ...report } = stored;
    return { ...report, submitted_at: ms };
  }

  status(ticket) {
    const row = this.sql.get('SELECT * FROM report_tracking WHERE ticket = ?', ticket);
    if (row) return statusOf(row);
    const stored = this.sql.get('SELECT ms FROM reports WHERE ticket = ?', ticket);
    return stored ? { status: 'new', status_updated_at: stored.ms, fixed_version: '', player_message: '' } : null;
  }

  // The status after the change, or null for an unknown report. A save that changes nothing keeps its time.
  setStatus(ticket, change, now) {
    const current = this.status(ticket);
    if (!current) return null;
    if (Object.keys(change).every((field) => current[field] === change[field])) return current;
    const submitted = this.sql.get('SELECT ms FROM reports WHERE ticket = ?', ticket)?.ms ?? current.status_updated_at;
    this.sql.run(`INSERT INTO report_tracking (ticket, receipt_hash, submitted_ms, status, status_ms, fixed_version, player_message)
      VALUES (?, '', ?, ?, ?, ?, ?) ON CONFLICT(ticket) DO UPDATE SET status = excluded.status, status_ms = excluded.status_ms,
      fixed_version = excluded.fixed_version, player_message = excluded.player_message`, ticket, submitted, change.status, now,
    change.fixed_version, change.player_message);
    return this.status(ticket);
  }

  // The whole report: its fields and log, and the status and receipt hash that let its owner look it up. False when
  // nothing was stored under the ticket.
  remove(ticket) {
    const atomically = this.sql.transaction ?? ((work) => work());
    return atomically(() => {
      const existed = this.taken(ticket);
      this.sql.run('DELETE FROM reports WHERE ticket = ?', ticket);
      this.sql.run('DELETE FROM report_tracking WHERE ticket = ?', ticket);
      return existed;
    });
  }

  taken(ticket) {
    return Boolean(this.sql.get(`SELECT 1 AS x FROM reports WHERE ticket = ?
      UNION ALL SELECT 1 AS x FROM report_tracking WHERE ticket = ?`, ticket, ticket));
  }

  list(now) {
    this.purge(now);
    return {
      reports: this.sql.all(`SELECT ${LIST_COLUMNS} FROM reports LEFT JOIN report_tracking t ON t.ticket = reports.ticket
        ORDER BY ms DESC, reports.rowid DESC`),
      kept: REPORT.kept,
    };
  }

  get(ticket) {
    return this.sql.get('SELECT * FROM reports WHERE ticket = ?', ticket) ?? null;
  }
}
