// The service's host for Node: node:sqlite for the tables, a clock the tests can move.
import { randomBytes } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';

import { ReportService } from '../src/service.js';

export function sqliteStore(file = ':memory:') {
  const db = new DatabaseSync(file);
  db.exec('PRAGMA journal_mode = WAL');
  const cache = new Map();
  const prepare = (query) => {
    let statement = cache.get(query);
    if (!statement) {
      statement = db.prepare(query);
      cache.set(query, statement);
    }
    return statement;
  };
  return {
    all: (query, ...args) => prepare(query).all(...args).map((row) => ({ ...row })),
    get: (query, ...args) => {
      const row = prepare(query).get(...args);
      return row ? { ...row } : undefined;
    },
    run: (query, ...args) => {
      prepare(query).run(...args);
    },
    transaction: (work) => {
      db.exec('BEGIN');
      try {
        const result = work();
        db.exec('COMMIT');
        return result;
      } catch (error) {
        db.exec('ROLLBACK');
        throw error;
      }
    },
    close: () => db.close(),
  };
}

export function localService({ db = ':memory:', now = () => Date.now(), log = () => {} } = {}) {
  const store = sqliteStore(db);
  const service = new ReportService({ sql: store, now, randomBytes: (count) => new Uint8Array(randomBytes(count)), log });
  return { service, store };
}
