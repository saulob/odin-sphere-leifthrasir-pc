import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';

import { handleAdmin, resetAdminLimits, tokenMatches } from '../src/admin.js';
import { ADMIN_CSS, ADMIN_HTML, ADMIN_JS } from '../src/admin_page.js';
import { handlePublic } from '../src/api.js';
import { ENV, TOKEN, fixture, makeEnv, submit } from './harness.js';

const SRC = join(import.meta.dirname, '..', 'src');
const bearer = (token) => ({ Authorization: `Bearer ${token}` });
const admin = (env, path, { method = 'GET', body, token = TOKEN, headers = {}, envVars = ENV, ip = '203.0.113.5', now } = {}) =>
  handleAdmin(new Request(`https://reports.example${path}`, {
    method,
    headers: { ...(token === null ? {} : bearer(token)), 'CF-Connecting-IP': ip, ...headers },
    body: method === 'POST' && body !== undefined ? (typeof body === 'string' ? body : JSON.stringify(body)) : undefined,
  }), envVars, env.desk, () => {}, now);

test('GET /admin serves the page without a token, under a strict policy', async () => {
  const env = makeEnv();
  for (const path of ['/admin', '/admin/']) {
    const response = await admin(env, path, { token: null });
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('Content-Type'), 'text/html; charset=utf-8');
    assert.equal(response.headers.get('Cache-Control'), 'no-store');
    const html = await response.text();
    assert.match(html, /<title>Odin Sphere Leifthrasir Bug Reports<\/title>/);
    assert.ok(!html.includes(TOKEN));
  }
  for (const [path, type] of [['/admin/app.js', 'text/javascript'], ['/admin/app.css', 'text/css']]) {
    const response = await admin(env, path, { token: null });
    assert.equal(response.status, 200);
    assert.ok(response.headers.get('Content-Type').startsWith(type));
    assert.match(response.headers.get('Content-Security-Policy'), /default-src 'none'.*script-src 'self'/);
    assert.equal(response.headers.get('X-Frame-Options'), 'DENY');
    assert.equal(response.headers.get('Referrer-Policy'), 'no-referrer');
  }
});

test('the page holds no inline script, external resource, secret or unsafe markup call', () => {
  assert.ok(!/<script>|<style>| style=| on[a-z]+=/i.test(ADMIN_HTML));
  assert.ok(!/\beval\(|new Function|innerHTML|document\.write|document\.cookie|[?&]token=|localStorage/.test(ADMIN_JS));
  for (const text of [ADMIN_HTML, ADMIN_CSS, ADMIN_JS]) assert.ok(!/(src|href)=["']?https?:|@import|url\(/i.test(text));
  assert.doesNotThrow(() => new Function(ADMIN_JS));
  const all = ADMIN_HTML + ADMIN_CSS + ADMIN_JS;
  assert.ok(!all.includes(TOKEN) && !/OSL_BUG_REPORT_ADMIN_TOKEN/.test(all));
});

test('every admin API needs the private token, whatever the method or the report', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const paths = ['/admin/api/reports', `/admin/api/reports/${filed.body.ticket}`, `/admin/api/reports/${filed.body.ticket}/log`];
  for (const headers of [{ token: null }, { token: 'wrong-token-wrong-token-wrong-1' }, { token: `${TOKEN}x` }, { token: null, headers: { Authorization: TOKEN } }]) {
    resetAdminLimits();
    for (const path of paths) assert.equal((await admin(env, path, headers)).status, 401, path);
    const status = await admin(env, `/admin/api/reports/${filed.body.ticket}/status`, { ...headers, method: 'POST', body: { status: 'closed' } });
    assert.equal(status.status, 401);
    assert.equal(status.headers.get('WWW-Authenticate'), 'Bearer');
  }
  assert.equal(env.service.get(filed.body.ticket).status, 'new');
  assert.equal(tokenMatches(`Bearer ${TOKEN}`, TOKEN), true);
  assert.equal(tokenMatches(`Bearer ${TOKEN}`, 'short'), false);
});

test('an unset or short secret answers 503 and never lets anyone in', async () => {
  const env = makeEnv();
  assert.equal((await admin(env, '/admin/api/reports', { envVars: {} })).status, 503);
  assert.equal((await admin(env, '/admin/api/reports', { token: 'short', envVars: { OSL_BUG_REPORT_ADMIN_TOKEN: 'short' } })).status, 503);
  assert.equal((await admin(env, '/admin/api/reports', { token: '', envVars: { OSL_BUG_REPORT_ADMIN_TOKEN: '' } })).status, 503);
  assert.equal((await admin(env, '/admin/api/reports', { envVars: { OTHER_DASHBOARD_TOKEN: TOKEN } })).status, 503);
});

test('wrong tokens are limited per address, and the limit holds against the right token too', async () => {
  const env = makeEnv();
  resetAdminLimits();
  const now = 1_700_000_000_000;
  for (let i = 0; i < 10; i++) assert.equal((await admin(env, '/admin/api/reports', { token: 'x'.repeat(30), ip: '198.51.100.1', now })).status, 401);
  const blocked = await admin(env, '/admin/api/reports', { token: TOKEN, ip: '198.51.100.1', now });
  assert.equal(blocked.status, 429);
  assert.equal(blocked.headers.get('Retry-After'), '60');
  assert.equal((await admin(env, '/admin/api/reports', { token: TOKEN, ip: '198.51.100.2', now })).status, 200);
  assert.equal((await admin(env, '/admin/api/reports', { token: TOKEN, ip: '198.51.100.1', now: now + 60_000 })).status, 200);
  resetAdminLimits();
});

test('the list and a report show what the player sent, and the log downloads as stored', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const list = await (await admin(env, '/admin/api/reports')).json();
  assert.equal(list.reports.length, 1);
  const row = list.reports[0];
  assert.equal(row.ticket, filed.body.ticket);
  assert.equal(row.status, 'new');
  assert.equal(row.launcher_version, '0.1.0');
  assert.equal(row.game_version, '1.00');
  assert.equal(row.region, 'USA');
  assert.equal(row.operating_system, 'Windows 11 (build 26200)');
  assert.equal(row.renderer, 'Vulkan');
  assert.equal(row.gpu, 'NVIDIA GeForce RTX 4070');
  assert.ok(!('log_text' in row) && !('description' in row));
  const one = await (await admin(env, `/admin/api/reports/${filed.body.ticket}`)).json();
  assert.equal(one.log_text, fixture().log_text);
  assert.ok(!('receipt_hash' in one));
  const log = await admin(env, `/admin/api/reports/${filed.body.ticket}/log`);
  assert.equal(log.headers.get('Content-Type'), 'text/plain; charset=utf-8');
  assert.equal(log.headers.get('Content-Disposition'), `attachment; filename="${filed.body.ticket}-odin_sphere_leifthrasir_026.log"`);
  assert.equal(await log.text(), fixture().log_text);
});

test('report ids are checked before anything is read, and unknown ones are 404', async () => {
  const env = makeEnv();
  assert.equal((await admin(env, '/admin/api/reports/..%2F..%2Fsecret')).status, 400);
  assert.equal((await admin(env, '/admin/api/reports/not-a-ticket/log')).status, 400);
  assert.equal((await admin(env, '/admin/api/reports/OSL-ZZZZZZ')).status, 404);
  assert.equal((await admin(env, '/admin/api/reports/OSL-ZZZZZZ/log')).status, 404);
  assert.equal((await admin(env, '/admin/api/reports/OSL-ZZZZZZ/status', { method: 'POST', body: { status: 'closed' } })).status, 404);
  assert.equal((await admin(env, '/admin/api/reports/OSL-ZZZZZZ/other')).status, 404);
  assert.equal((await admin(env, '/admin/nope')).status, 404);
  assert.equal((await admin(env, '/admin/api/reports', { method: 'POST', body: {} })).status, 405);
  assert.equal((await admin(env, '/admin/api/reports/OSL-ZZZZZZ/status')).status, 405);
  assert.equal((await admin(env, '/admin', { method: 'POST' })).status, 405);
});

test('a status is saved, validated and visible to the owner', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const save = (body) => admin(env, `/admin/api/reports/${filed.body.ticket}/status`, { method: 'POST', body });
  for (const status of ['new', 'investigating', 'fixed', 'cannot_reproduce', 'duplicate', 'not_a_bug', 'closed']) {
    env.advance(1000);
    const response = await save({ status, fixed_version: status === 'fixed' ? '0.1.1' : '', player_message: status === 'fixed' ? 'Thanks.' : '' });
    assert.equal(response.status, 200, status);
    assert.equal((await response.json()).status, status);
  }
  for (const [body, detail] of [
    [{ status: 'solved' }, 'status'], [{ status: 'fixed', fixed_version: 'not a version' }, 'fixed_version'],
    [{ status: 'closed', player_message: 'x'.repeat(501) }, 'player_message'], [{ status: 'closed', player_message: 'a\rb' }, 'player_message'],
    [{ status: 'closed', extra: 1 }, 'extra'], [[], 'body'], [{ status: 7 }, 'status'],
  ]) {
    const response = await save(body);
    assert.equal(response.status, 400);
    assert.equal((await response.json()).detail, detail);
  }
  assert.equal((await save('{broken')).status, 400);
  assert.equal((await save(JSON.stringify({ status: 'closed', player_message: 'x'.repeat(3000) }))).status, 413);
  await save({ status: 'fixed', fixed_version: '0.1.1', player_message: ' Fixed. ' });
  const seen = await (await handlePublic(new Request('https://reports.example/v1/reports/status', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ reports: [{ ticket: filed.body.ticket, receipt: filed.body.receipt }] }) }), {}, env.desk)).json();
  assert.equal(seen.reports[0].status, 'fixed');
  assert.equal(seen.reports[0].fixed_version, '0.1.1');
  assert.equal(seen.reports[0].player_message, 'Fixed.');
  await save({ status: 'investigating', fixed_version: '9.9.9' });
  assert.equal((await (await admin(env, `/admin/api/reports/${filed.body.ticket}`)).json()).fixed_version, '');
});

test('a request that names another origin is refused, the same origin passes', async () => {
  const env = makeEnv();
  assert.equal((await admin(env, '/admin/api/reports', { headers: { Origin: 'https://evil.example' } })).status, 403);
  assert.equal((await admin(env, '/admin/api/reports', { headers: { Origin: 'https://reports.example' } })).status, 200);
});

test('no administrative data is served by the public API', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  for (const path of ['/admin/api/reports', `/v1/reports/${filed.body.ticket}`, '/v1/reports/list', '/reports']) {
    for (const method of ['GET', 'POST']) {
      const response = await handlePublic(new Request(`https://reports.example${path}`, {
        method, headers: { 'Content-Type': 'application/json', ...bearer(TOKEN) }, body: method === 'POST' ? '{}' : undefined }), ENV, env.desk);
      assert.ok([404, 405].includes(response.status), `${method} ${path}`);
    }
  }
});

test('an unreachable desk answers 502 without a stack or the token', async () => {
  const logs = [];
  const failing = { list: async () => { throw new Error('storage exploded'); } };
  resetAdminLimits();
  const response = await handleAdmin(new Request('https://reports.example/admin/api/reports', { headers: bearer(TOKEN) }), ENV, failing, (t) => logs.push(t));
  assert.equal(response.status, 502);
  const text = await response.text();
  assert.equal(text, '{"error":"unavailable"}');
  assert.ok(!text.includes('exploded') && !logs.join('\n').includes(TOKEN));
});

test('the worker routes /admin before the public API and wires the one secret name', () => {
  const worker = readFileSync(join(SRC, 'worker.js'), 'utf8');
  const admin_source = readFileSync(join(SRC, 'admin.js'), 'utf8');
  assert.ok(admin_source.includes('env.OSL_BUG_REPORT_ADMIN_TOKEN'));
  assert.ok(!/OSL_BUG_REPORT_ADMIN_TOKEN\s*=\s*['"`]/.test(worker + admin_source));
  const names = new Set([...(worker + admin_source).matchAll(/\benv\.([A-Z_]+)/g)].map((m) => m[1]));
  assert.deepEqual([...names].sort(), ['OSL_BUG_REPORT_ADMIN_TOKEN', 'REPORTS']);
});
