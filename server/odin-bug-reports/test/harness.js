// A service on a disposable in-memory database, a clock the test moves, and requests built the way the launcher builds them.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

import { handlePublic } from '../src/api.js';
import { localService } from '../local/host.js';

export const TOKEN = 'admin-token-0123456789-abcdefgh';
export const ENV = { OSL_DASHBOARD_TOKEN: TOKEN };
export const MARKER = 'UNIQUE-REPORT-MARKER-4711';
export const HOUR = 3600 * 1000;
export const DAY = 24 * HOUR;

export const fixture = () => JSON.parse(readFileSync(join(import.meta.dirname, 'fixtures', 'request.json'), 'utf8'));

export const report = (extra = {}) => ({ ...fixture(), description: `The game froze on the title screen ${MARKER}`, ...extra });

export function makeEnv(options = {}) {
  const clock = { now: 1_700_000_000_000 };
  const logs = [];
  const { service, store } = localService({ now: () => clock.now, log: (text) => logs.push(text), ...options });
  const desk = {
    submit: async (...args) => service.submit(...args),
    statuses: async (...args) => service.statuses(...args),
    read: async (...args) => service.read(...args),
    list: async () => service.list(),
    get: async (ticket) => service.get(ticket),
    setStatus: async (ticket, change) => service.setStatus(ticket, change),
    remove: async (ticket) => service.remove(ticket),
  };
  return {
    service, store, desk, clock, logs,
    advance: (ms) => { clock.now += ms; },
    rows: (table) => store.all(`SELECT * FROM ${table}`),
    everything: () => JSON.stringify(Object.fromEntries(store.all("SELECT name FROM sqlite_master WHERE type = 'table'")
      .map((t) => [t.name, store.all(`SELECT * FROM ${t.name}`)]))),
  };
}

export const post = (path, body, { ip = '203.0.113.7', headers = {}, raw } = {}) => new Request(`https://reports.example${path}`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': ip, ...headers },
  body: raw ?? JSON.stringify(body),
});

export async function submit(env, extra = {}, options = {}, apiEnv = {}) {
  const response = await handlePublic(post('/v1/reports', report(extra), options), apiEnv, env.desk);
  return { response, body: await response.json() };
}
