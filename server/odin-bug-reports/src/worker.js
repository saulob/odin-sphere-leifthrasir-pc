// Cloudflare Worker with one SQLite-backed Durable Object holding the reports (README.md). The Worker checks every
// request, then asks the object by name; the object is reached from nowhere else and nothing here opens a socket.
import { DurableObject } from 'cloudflare:workers';

import { handleAdmin, isAdminPath } from './admin.js';
import { handlePublic, reply } from './api.js';
import { ReportService } from './service.js';

export class BugReportDesk extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    const sql = ctx.storage.sql;
    ctx.blockConcurrencyWhile(async () => {
      this.service = new ReportService({
        sql: {
          all: (query, ...args) => sql.exec(query, ...args).toArray(),
          get: (query, ...args) => sql.exec(query, ...args).toArray()[0],
          run: (query, ...args) => {
            sql.exec(query, ...args);
          },
          transaction: (work) => ctx.storage.transactionSync(work),
        },
        now: () => Date.now(),
        randomBytes: (count) => crypto.getRandomValues(new Uint8Array(count)),
        log: (text) => console.log(text),
      });
    });
  }

  submit(report, address) {
    return this.service.submit(report, address);
  }

  statuses(claims, address) {
    return this.service.statuses(claims, address);
  }

  read(claim, address) {
    return this.service.read(claim, address);
  }

  list() {
    return this.service.list();
  }

  get(ticket) {
    return this.service.get(ticket);
  }

  setStatus(ticket, change) {
    return this.service.setStatus(ticket, change);
  }

  remove(ticket) {
    return this.service.remove(ticket);
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const desk = env.REPORTS.get(env.REPORTS.idFromName('global'));
    const log = (text) => console.log(text);
    if (isAdminPath(url.pathname)) return handleAdmin(request, env, desk, log);
    return handlePublic(request, env, desk, log).catch((error) => {
      log(`reports: ${error?.message ?? error}`);
      return reply({ error: 'unavailable' }, 502);
    });
  },
};
