// The report service behind the Durable Object: the store, the abuse limits and the random values. No Cloudflare import,
// so Node runs it in the tests. Every method answers { code: 'ok', ... } or { code } with what went wrong.
import { PROTECTION, RateLimiter } from './limits.js';
import { ReportDesk, ticketCode } from './reports.js';
import { toHex } from './sha256.js';

export const SERVICE = 'odin-sphere-bug-reports';
export const PROTOCOL = 1;
export const WORKER_BUILD = '2026-11-08-odin-bug-reports';

// host: { sql: { all, get, run, transaction? }, now(), randomBytes(n), log(text) }
export class ReportService {
  constructor(host) {
    this.host = host;
    this.desk = new ReportDesk(host.sql, this.salt());
    this.guards = {
      submitAll: new RateLimiter(PROTECTION.submitAll),
      submitAddress: new RateLimiter(PROTECTION.submitAddress),
      lookupAll: new RateLimiter(PROTECTION.lookupAll),
      statusAddress: new RateLimiter(PROTECTION.statusAddress),
      readAddress: new RateLimiter(PROTECTION.readAddress),
    };
  }

  // The service's own random salt for the keys of the quota and the receipts; it never leaves the database.
  salt() {
    const { sql } = this.host;
    sql.run('CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT NOT NULL)');
    const stored = sql.get("SELECT v FROM meta WHERE k = 'salt'");
    if (stored) return stored.v;
    const salt = toHex(this.host.randomBytes(32));
    sql.run("INSERT INTO meta (k, v) VALUES ('salt', ?)", salt);
    return salt;
  }

  limited(guards) {
    const now = this.host.now();
    if (!guards.every(([guard, key]) => this.guards[guard].allows(key, now))) return true;
    for (const [guard, key] of guards) this.guards[guard].take(key, now);
    return false;
  }

  newTicket() {
    let ticket;
    do ticket = ticketCode(this.host.randomBytes(6));
    while (this.desk.taken(ticket));
    return ticket;
  }

  submit(report, address) {
    if (this.limited([['submitAll', 'all'], ['submitAddress', address]])) return { code: 'limit' };
    const now = this.host.now();
    const atomically = this.host.sql.transaction ?? ((work) => work());
    const filed = atomically(() => {
      const refusal = this.desk.refusal(address, now);
      if (refusal) return { code: refusal };
      const ticket = this.newTicket();
      const receipt = toHex(this.host.randomBytes(32));
      this.desk.file(report, ticket, address, now);
      this.desk.track(ticket, receipt, now);
      return { code: 'ok', ticket, receipt, submitted_at: now };
    });
    if (filed.code !== 'ok') this.host.log(`report refused: ${filed.code}`);
    return filed;
  }

  statuses(claims, address) {
    if (this.limited([['lookupAll', 'all'], ['statusAddress', address]])) return { code: 'limit' };
    this.desk.purge(this.host.now());
    return { code: 'ok', reports: this.desk.statuses(claims) };
  }

  read(claim, address) {
    if (this.limited([['lookupAll', 'all'], ['readAddress', address]])) return { code: 'limit' };
    this.desk.purge(this.host.now());
    const report = this.desk.report(claim);
    return report ? { code: 'ok', report } : { code: 'not_found' };
  }

  list() {
    return this.desk.list(this.host.now());
  }

  get(ticket) {
    this.desk.purge(this.host.now());
    const found = this.desk.get(ticket);
    return found ? { ...found, ...this.desk.status(ticket) } : null;
  }

  setStatus(ticket, change) {
    return this.desk.setStatus(ticket, change, this.host.now());
  }

  remove(ticket) {
    return this.desk.remove(ticket);
  }
}
