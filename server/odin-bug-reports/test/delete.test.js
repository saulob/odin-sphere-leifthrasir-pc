import assert from 'node:assert/strict';
import { test } from 'node:test';

import { handleAdmin, resetAdminLimits } from '../src/admin.js';
import { handlePublic } from '../src/api.js';
import { ReportDesk, readReport } from '../src/reports.js';
import { sqliteStore } from '../local/host.js';
import { ENV, TOKEN, fixture, makeEnv, post, submit } from './harness.js';

const remove = (env, ticket, { token = TOKEN, method = 'DELETE', envVars = ENV, ip = '203.0.113.5' } = {}) => {
  resetAdminLimits();
  return handleAdmin(new Request(`https://reports.example/admin/api/reports/${ticket}`, {
    method, headers: { ...(token === null ? {} : { Authorization: `Bearer ${token}` }), 'CF-Connecting-IP': ip },
  }), envVars, env.desk, () => {});
};
const ask = async (env, path, body) => handlePublic(post(path, body), {}, env.desk);
const claim = (filed) => ({ ticket: filed.body.ticket, receipt: filed.body.receipt });

test('deleting needs the private token and a stored secret, and an unauthorised call deletes nothing', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  for (const token of [null, 'wrong-token-wrong-token-wrong-1', `${TOKEN}x`]) {
    const response = await remove(env, filed.body.ticket, { token });
    assert.equal(response.status, 401);
    assert.equal(response.headers.get('WWW-Authenticate'), 'Bearer');
  }
  assert.equal((await remove(env, filed.body.ticket, { envVars: {} })).status, 503);
  assert.equal((await remove(env, filed.body.ticket, { envVars: { OSL_BUG_REPORT_ADMIN_TOKEN: 'short' }, token: 'short' })).status, 503);
  assert.ok(env.service.get(filed.body.ticket));
  assert.equal(env.rows('report_tracking').length, 1);
});

test('a malformed report id is 400 and an unknown one is 404, both after the token', async () => {
  const env = makeEnv();
  await submit(env);
  for (const id of ['not-a-ticket', '..%2F..%2Fsecret', 'osl-ZZZZZZ', 'OSL-ZZZZZ', 'OSL-ZZZZZZZ', 'OSL-ZZZZZ0']) {
    const response = await remove(env, id);
    assert.equal(response.status, 400, id);
    assert.equal((await response.json()).detail, 'invalid report id');
  }
  assert.equal((await remove(env, 'not-a-ticket', { token: null })).status, 401);
  const unknown = await remove(env, 'OSL-ZZZZZZ');
  assert.equal(unknown.status, 404);
  assert.deepEqual(await unknown.json(), { error: 'not_found' });
  assert.equal(env.rows('reports').length, 1);
});

test('deleting answers {"deleted": ticket} and removes the report, its log and its tracking row together', async () => {
  const env = makeEnv();
  const gone = await submit(env, {}, { ip: '203.0.113.1' });
  const kept = await submit(env, {}, { ip: '203.0.113.2' });
  env.service.setStatus(gone.body.ticket, { status: 'fixed', fixed_version: '0.1.1', player_message: 'Fixed.' });
  const response = await remove(env, gone.body.ticket);
  assert.equal(response.status, 200);
  assert.equal(response.headers.get('Content-Type'), 'application/json; charset=utf-8');
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
  assert.equal(await response.text(), JSON.stringify({ deleted: gone.body.ticket }));
  assert.equal(env.service.get(gone.body.ticket), null);
  assert.deepEqual(env.rows('reports').map((r) => r.ticket), [kept.body.ticket]);
  assert.deepEqual(env.rows('report_tracking').map((r) => r.ticket), [kept.body.ticket]);
  assert.ok(!env.everything().includes(gone.body.ticket));
  assert.equal((await remove(env, gone.body.ticket)).status, 404);
  assert.ok(env.service.get(kept.body.ticket));
});

test('a deleted report stops being answered for: the status batch leaves it out and read says not_found', async () => {
  const env = makeEnv();
  const gone = await submit(env, {}, { ip: '203.0.113.1' });
  const kept = await submit(env, {}, { ip: '203.0.113.2' });
  assert.equal((await ask(env, '/v1/reports/read', claim(gone))).status, 200);
  await remove(env, gone.body.ticket);
  const status = await (await ask(env, '/v1/reports/status', { reports: [claim(gone), claim(kept)] })).json();
  assert.deepEqual(status.reports.map((r) => r.ticket), [kept.body.ticket]);
  const read = await ask(env, '/v1/reports/read', claim(gone));
  assert.equal(read.status, 404);
  assert.deepEqual(await read.json(), { error: 'not_found' });
  const same = await ask(env, '/v1/reports/read', { ticket: 'OSL-ZZZZZZ', receipt: 'f'.repeat(64) });
  assert.deepEqual(await same.json(), { error: 'not_found' });
  assert.equal((await ask(env, '/v1/reports/read', claim(kept))).status, 200);
});

test('a report whose log expired but whose status remains can still be deleted', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  env.store.run('DELETE FROM reports WHERE ticket = ?', filed.body.ticket);
  assert.equal(env.service.remove(filed.body.ticket), true);
  assert.equal(env.rows('report_tracking').length, 0);
  assert.equal(env.service.remove(filed.body.ticket), false);
});

test('a delete that fails midway changes nothing', () => {
  const store = sqliteStore();
  const desk = new ReportDesk(store, 'salt');
  desk.file(readReport(fixture()).report, 'OSL-AAAAAA', '203.0.113.1', 1);
  desk.track('OSL-AAAAAA', 'a'.repeat(64), 1);
  const broken = ReportDesk.prototype.remove.bind({
    sql: {
      ...store,
      run: (query, ...args) => {
        if (query.startsWith('DELETE FROM report_tracking')) throw new Error('storage failed');
        return store.run(query, ...args);
      },
    },
    taken: (ticket) => desk.taken(ticket),
  });
  assert.throws(() => broken('OSL-AAAAAA'), /storage failed/);
  assert.ok(desk.get('OSL-AAAAAA'));
  assert.equal(store.all('SELECT * FROM report_tracking').length, 1);
});

test('DELETE is allowed on one report only, and every other admin route names its own methods', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const ticket = filed.body.ticket;
  const method = async (path, verb) => {
    resetAdminLimits();
    return handleAdmin(new Request(`https://reports.example${path}`, { method: verb, headers: { Authorization: `Bearer ${TOKEN}` },
      body: verb === 'POST' ? '{}' : undefined }), ENV, env.desk, () => {});
  };
  const allow = async (path, verb) => {
    const response = await method(path, verb);
    assert.equal(response.status, 405, `${verb} ${path}`);
    return response.headers.get('Allow');
  };
  assert.equal(await allow(`/admin/api/reports/${ticket}`, 'POST'), 'GET, DELETE');
  assert.equal(await allow(`/admin/api/reports/${ticket}`, 'PUT'), 'GET, DELETE');
  assert.equal(await allow(`/admin/api/reports/${ticket}/log`, 'DELETE'), 'GET');
  assert.equal(await allow(`/admin/api/reports/${ticket}/status`, 'DELETE'), 'POST');
  assert.equal(await allow('/admin/api/reports', 'DELETE'), 'GET');
  assert.equal(await allow('/admin', 'DELETE'), 'GET');
  assert.equal(await allow('/admin/app.js', 'DELETE'), 'GET');
  assert.ok(env.service.get(ticket));
});

test('the public API has no way to delete a report', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  for (const path of ['/v1/reports', `/v1/reports/${filed.body.ticket}`, '/v1/reports/read', '/v1/reports/status']) {
    const response = await handlePublic(new Request(`https://reports.example${path}`, {
      method: 'DELETE', headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${TOKEN}` } }), ENV, env.desk);
    assert.ok([404, 405].includes(response.status), path);
  }
  assert.ok(env.service.get(filed.body.ticket));
});
