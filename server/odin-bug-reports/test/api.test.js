import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';

import { handlePublic, readBounded } from '../src/api.js';
import { REPORT } from '../src/reports.js';
import { fixture, makeEnv, post, report, submit } from './harness.js';

const SRC = join(import.meta.dirname, '..', 'src');
const countingDesk = (env) => {
  const calls = [];
  const desk = Object.fromEntries(['submit', 'statuses', 'read'].map((name) => [name, async (...args) => {
    calls.push(name);
    return env.desk[name](...args);
  }]));
  return { desk, calls };
};
const call = (request, env = {}, desk = makeEnv().desk) => handlePublic(request, env, desk);

test('/health answers with the service name and nothing about its contents', async () => {
  const response = await call(new Request('https://reports.example/health'));
  assert.equal(response.status, 200);
  assert.deepEqual(Object.keys(await response.json()).sort(), ['protocol', 'service', 'status', 'worker_build']);
  assert.equal((await call(new Request('https://reports.example/'))).status, 200);
  assert.equal((await call(new Request('https://reports.example/health', { method: 'POST' }))).status, 405);
});

test('responses carry no CORS headers and the usual protections', async () => {
  const env = makeEnv();
  const { response } = await submit(env);
  assert.equal(response.headers.get('Access-Control-Allow-Origin'), null);
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
  assert.equal(response.headers.get('X-Content-Type-Options'), 'nosniff');
  assert.equal(response.headers.get('Referrer-Policy'), 'no-referrer');
  assert.match(response.headers.get('Content-Type'), /^application\/json/);
  const options = await call(new Request('https://reports.example/v1/reports', { method: 'OPTIONS' }), {}, env.desk);
  assert.equal(options.status, 405);
  assert.equal(options.headers.get('Access-Control-Allow-Origin'), null);
});

test('a request from a web page, another type or another path never reaches the desk', async () => {
  const env = makeEnv();
  const { desk, calls } = countingDesk(env);
  const fromPage = await call(post('/v1/reports', report(), { headers: { Origin: 'https://evil.example' } }), {}, desk);
  assert.equal(fromPage.status, 403);
  assert.deepEqual(await fromPage.json(), { error: 'origin_not_allowed' });
  const plain = await call(post('/v1/reports', report(), { headers: { 'Content-Type': 'text/plain' } }), {}, desk);
  assert.equal(plain.status, 415);
  const form = await call(post('/v1/reports', report(), { headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }), {}, desk);
  assert.equal(form.status, 415);
  assert.equal((await call(new Request('https://reports.example/v1/reports'), {}, desk)).status, 405);
  assert.equal((await call(post('/v1/other', report()), {}, desk)).status, 404);
  assert.equal((await call(post('/admin/api/reports', report()), {}, desk)).status, 404);
  assert.deepEqual(calls, []);
  assert.equal(env.rows('reports').length, 0);
});

test('malformed payloads are refused before the desk is asked', async () => {
  const env = makeEnv();
  const { desk, calls } = countingDesk(env);
  const refuse = async (options) => (await call(post('/v1/reports', null, options), {}, desk)).status;
  assert.equal(await refuse({ raw: '{not json' }), 400);
  assert.equal(await refuse({ raw: '' }), 400);
  assert.equal(await refuse({ raw: '[]' }), 400);
  assert.equal(await refuse({ raw: 'null' }), 400);
  assert.equal(await refuse({ raw: JSON.stringify({ ...fixture(), extra: 1 }) }), 400);
  assert.equal(await refuse({ raw: JSON.stringify({ ...fixture(), description: 'x' }) }), 400);
  assert.equal(await refuse({ raw: JSON.stringify({ ...fixture(), log_text: 'x'.repeat(REPORT.logBytes + 1) }) }), 413);
  assert.equal(await refuse({ raw: new Uint8Array([0x7b, 0xff, 0xfe, 0x7d]) }), 400);
  assert.deepEqual(calls, []);
});

test('a body over the limit is refused by its declared size or while it is read', async () => {
  const env = makeEnv();
  const { desk, calls } = countingDesk(env);
  const huge = 'x'.repeat(REPORT.bodyBytes + 1);
  const declared = await call(post('/v1/reports', null, { raw: huge }), {}, desk);
  assert.equal(declared.status, 413);
  const streamed = new Request('https://reports.example/v1/reports', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: new ReadableStream({ start(controller) { controller.enqueue(new TextEncoder().encode(huge)); controller.close(); } }),
    duplex: 'half',
  });
  assert.equal((await call(streamed, {}, desk)).status, 413);
  const bounded = await readBounded(new Request('https://reports.example/', {
    method: 'POST',
    body: new ReadableStream({ start(controller) { controller.enqueue(new Uint8Array(100)); controller.enqueue(new Uint8Array(100)); controller.close(); } }),
    duplex: 'half',
  }), 150);
  assert.deepEqual(bounded, { code: 'too_large' });
  const lying = post('/v1/reports', null, { raw: '{}', headers: { 'Content-Length': '99999999999999' } });
  assert.equal((await call(lying, {}, desk)).status, 413);
  assert.deepEqual(calls, []);
  assert.equal(env.rows('reports').length, 0);
});

test('the edge limiter refuses before anything else and its failure lets the request through', async () => {
  const env = makeEnv();
  const { desk, calls } = countingDesk(env);
  const keys = [];
  const denying = { limit: async ({ key }) => { keys.push(key); return { success: false }; } };
  const refused = await call(post('/v1/reports', report(), { ip: '2001:db8:1:2:3:4:5:6' }), { REPORT_LIMITER: denying }, desk);
  assert.equal(refused.status, 429);
  assert.equal(refused.headers.get('Retry-After'), '60');
  assert.deepEqual(calls, []);
  assert.deepEqual(keys, ['report:2001:db8:1:2']);
  const broken = { limit: async () => { throw new Error('limiter down'); } };
  assert.equal((await call(post('/v1/reports', report()), { REPORT_LIMITER: broken }, desk)).status, 201);
});

test('an unavailable desk answers 502 without a stack or detail', async () => {
  const logs = [];
  const failing = { submit: async () => { throw new Error('storage exploded at line 12'); } };
  const response = await handlePublic(post('/v1/reports', report()), {}, failing, (text) => logs.push(text));
  assert.equal(response.status, 502);
  assert.equal(await response.text(), '{"error":"unavailable"}');
  assert.ok(logs.length === 1 && !logs[0].includes(report().description));
});

test('the service source holds no telemetry, heartbeat, WebSocket or player identity', () => {
  const files = ['api.js', 'admin.js', 'service.js', 'reports.js', 'worker.js', 'limits.js'];
  const source = files.map((name) => readFileSync(join(SRC, name), 'utf8')).join('\n');
  assert.ok(!/WebSocket|setInterval|\balarm\b|metrics|heartbeat|presence|player_id|account|analytics/i.test(source.replace(/no account/gi, '')));
  assert.ok(!/console\.log\([^)]*(token|receipt|Authorization|CF-Connecting-IP)/i.test(source));
  const worker = readFileSync(join(SRC, 'worker.js'), 'utf8');
  assert.ok(worker.indexOf('isAdminPath(url.pathname)') < worker.indexOf('return handlePublic'));
});
