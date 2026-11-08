import assert from 'node:assert/strict';
import { test } from 'node:test';

import { handlePublic } from '../src/api.js';
import { READERS, REPORT, isTicket, readClaims, readReport, readStatusChange, ticketCode } from '../src/reports.js';
import { DAY, HOUR, MARKER, fixture, makeEnv, post, report, submit } from './harness.js';

const claim = (filed) => ({ ticket: filed.body.ticket, receipt: filed.body.receipt });
const lookup = (env, path, body, options) => handlePublic(post(path, body, options), {}, env.desk);

test('the canonical request is accepted and names exactly the fields the service reads', () => {
  const { report: read, code } = readReport(fixture());
  assert.equal(code, undefined);
  assert.deepEqual(Object.keys(read).sort(), Object.keys(READERS).sort());
  assert.deepEqual(Object.keys(fixture()).sort(), Object.keys(READERS).sort());
});

test('a valid report is stored and answered with a ticket, a private receipt and the time', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  assert.equal(filed.response.status, 201);
  assert.match(filed.body.ticket, /^OSL-[A-HJ-NP-Z2-9]{6}$/);
  assert.match(filed.body.receipt, /^[0-9a-f]{64}$/);
  assert.equal(filed.body.submitted_at, env.clock.now);
  assert.deepEqual(Object.keys(filed.body).sort(), ['receipt', 'submitted_at', 'ticket']);
  const stored = env.service.get(filed.body.ticket);
  assert.equal(stored.description, report().description);
  assert.equal(stored.gpu, 'NVIDIA GeForce RTX 4070');
  assert.equal(stored.launcher_version, '0.1.0');
  assert.equal(stored.region, 'USA');
  assert.equal(stored.log_text, fixture().log_text);
  assert.equal(stored.status, 'new');
});

test('tickets are unique and carry the OSL prefix', async () => {
  const env = makeEnv();
  const tickets = new Set();
  for (let i = 0; i < 3; i++) {
    tickets.add((await submit(env, {}, { ip: `203.0.113.${i + 1}` })).body.ticket);
    env.advance(1000);
  }
  assert.equal(tickets.size, 3);
  assert.ok([...tickets].every(isTicket));
  assert.equal(isTicket('ABC-ABCDEF'), false);
  assert.equal(isTicket('OSL-ABCDE0'), false);
  assert.equal(ticketCode(new Uint8Array([0, 1, 2, 3, 4, 255])), 'OSL-ABCDE9');
});

test('description, metadata and log are validated and nothing is rewritten', () => {
  const bad = (extra, code, detail) => {
    const result = readReport({ ...fixture(), ...extra });
    assert.equal(result.code, code, JSON.stringify(extra).slice(0, 60));
    if (detail) assert.match(result.detail, detail);
  };
  bad({ description: 'short' }, 'bad_request', /too short/);
  bad({ description: '         ten      ' }, 'bad_request', /too short/);
  bad({ description: 'x'.repeat(501) }, 'too_large', /description/);
  assert.equal(readReport({ ...fixture(), description: 'x'.repeat(500) }).report.description.length, 500);
  assert.equal(readReport({ ...fixture(), description: '€'.repeat(500) }).report.description.length, 500);
  bad({ description: 'line one\r\nline two is long' }, 'bad_request');
  bad({ description: 'with\u0007bell character' }, 'bad_request');
  bad({ launcher_version: ' 0.1.0' }, 'bad_request');
  bad({ launcher_version: 'not a version!' }, 'bad_request');
  bad({ cusa: 'XXXX00000' }, 'bad_request');
  bad({ eboot_sha256: 'abc' }, 'bad_request');
  bad({ log_file: '../../etc/passwd' }, 'bad_request');
  bad({ log_file: 'other_game_012.log' }, 'bad_request');
  bad({ log_clipped: 'maybe' }, 'bad_request');
  bad({ gpu: 'g'.repeat(97) }, 'too_large');
  bad({ operating_system: '' }, 'bad_request');
  bad({ renderer: 7 }, 'bad_request');
  bad({ log_text: 12 }, 'bad_request', /log_text/);
  bad({ log_text: '   \n  ' }, 'bad_request', /empty log/);
  bad({ log_text: 'one\r\ntwo' }, 'bad_request');
  bad({ log_text: 'a\u0000b' }, 'bad_request');
  bad({ unexpected: 'x' }, 'bad_request', /unexpected/);
  assert.equal(readReport({ ...fixture(), log_text: 'tab\tand\nnewline\n' }).report.log_text, 'tab\tand\nnewline\n');
  const missing = fixture();
  delete missing.log_text;
  assert.equal(readReport(missing).code, 'bad_request');
  for (const body of [null, [], 'text', 7]) assert.equal(readReport(body).code, 'bad_request');
});

test('the log is limited to 64 KiB of UTF-8, not 64 K characters', () => {
  const edge = 'a'.repeat(REPORT.logBytes - 1) + '\n';
  assert.equal(readReport({ ...fixture(), log_text: edge }).code, undefined);
  assert.equal(readReport({ ...fixture(), log_text: `${edge}a` }).code, 'too_large');
  assert.equal(readReport({ ...fixture(), log_text: '€'.repeat(Math.floor(REPORT.logBytes / 3) + 1) }).code, 'too_large');
  assert.equal(readReport({ ...fixture(), log_text: '€'.repeat(Math.floor(REPORT.logBytes / 3)) }).code, undefined);
});

test('a report keeps no address, token or identity, and its receipt only as a salted hash', async () => {
  const env = makeEnv();
  const filed = await submit(env, {}, { ip: '198.51.100.23' });
  const stored = env.everything();
  assert.ok(!stored.includes('198.51.100.23'));
  assert.ok(!stored.includes(filed.body.receipt));
  assert.deepEqual(env.store.all("SELECT name FROM pragma_table_info('reports')").map((r) => r.name).sort(),
    ['cusa', 'description', 'driver', 'eboot_sha256', 'game_build', 'game_version', 'gpu', 'launcher_version', 'log_clipped',
      'log_file', 'log_text', 'ms', 'operating_system', 'region', 'render_resolution', 'renderer', 'ticket']);
  const quota = env.rows('report_quota');
  assert.equal(quota.length, 1);
  assert.match(quota[0].address_key, /^[0-9a-f]{32}$/);
  assert.deepEqual(env.store.all("SELECT name FROM pragma_table_info('report_quota')").map((r) => r.name).sort(), ['address_key', 'ms']);
  assert.match(env.rows('report_tracking')[0].receipt_hash, /^[0-9a-f]{64}$/);
  assert.ok(!env.logs.join('\n').includes('198.51.100.23'));
  assert.ok(!env.logs.join('\n').includes(filed.body.receipt));
});

test('the quota windows roll: three an hour per address, others unaffected', async () => {
  const env = makeEnv();
  const ip = '203.0.113.9';
  for (let i = 0; i < 3; i++) assert.equal((await submit(env, {}, { ip })).response.status, 201);
  const hourly = await submit(env, {}, { ip });
  assert.equal(hourly.response.status, 429);
  assert.deepEqual(hourly.body, { error: 'hourly_limit' });
  assert.equal(hourly.response.headers.get('Retry-After'), '60');
  assert.equal((await submit(env, {}, { ip: '203.0.113.10' })).response.status, 201);
  env.advance(HOUR + 1000);
  assert.equal((await submit(env, {}, { ip })).response.status, 201);
});

test('the daily address limit answers daily_limit once the hour is clear', async () => {
  const env = makeEnv();
  const ip = '203.0.113.11';
  for (let i = 0; i < REPORT.perAddressPerDay; i++) {
    assert.equal((await submit(env, {}, { ip })).response.status, 201, `report ${i + 1}`);
    env.advance(HOUR / 3 + 1);
  }
  const refused = await submit(env, {}, { ip });
  assert.equal(refused.response.status, 429);
  assert.equal(refused.body.error, 'daily_limit');
  env.advance(DAY);
  assert.equal((await submit(env, {}, { ip })).response.status, 201);
});

test('the service as a whole takes at most 300 reports a day', () => {
  const env = makeEnv();
  const read = readReport(report()).report;
  const ticket = (i) => ticketCode(new Uint8Array([i & 31, (i >> 5) & 31, (i >> 10) & 31, 1, 2, 3]));
  for (let i = 0; i < REPORT.perDay; i++) {
    const address = `10.0.${i >> 8}.${i & 255}`;
    assert.equal(env.service.desk.refusal(address, env.clock.now), null);
    env.service.desk.file(read, ticket(i), address, env.clock.now);
  }
  assert.equal(env.service.desk.refusal('192.0.2.1', env.clock.now), 'limit');
  env.advance(DAY + 1000);
  assert.equal(env.service.desk.refusal('192.0.2.1', env.clock.now), null);
});

test('abuse in memory refuses a burst before it reaches SQL', async () => {
  const env = makeEnv();
  const answers = [];
  for (let i = 0; i < 8; i++) answers.push((await submit(env, { log_text: 'x\n'.repeat(2) }, { ip: '203.0.113.50' })).body.error ?? 'ok');
  assert.deepEqual(answers.slice(0, 3), ['ok', 'ok', 'ok']);
  assert.ok(answers.slice(3).every((a) => a === 'hourly_limit' || a === 'limit'));
  assert.ok(answers.includes('limit'));
  assert.equal(env.rows('reports').length, 3);
});

test('status lookups need the receipt; a wrong receipt and an unknown ticket look the same', async () => {
  const env = makeEnv();
  const mine = await submit(env, {}, { ip: '203.0.113.1' });
  const other = await submit(env, {}, { ip: '203.0.113.2' });
  const asked = await lookup(env, '/v1/reports/status', { reports: [
    claim(mine), { ticket: other.body.ticket, receipt: mine.body.receipt }, { ticket: 'OSL-ZZZZZZ', receipt: mine.body.receipt }] });
  assert.equal(asked.status, 200);
  const answer = await asked.json();
  assert.equal(answer.reports.length, 1);
  assert.equal(answer.reports[0].ticket, mine.body.ticket);
  assert.equal(answer.reports[0].status, 'new');
  assert.deepEqual(Object.keys(answer.reports[0]).sort(), ['status', 'status_updated_at', 'ticket']);
  const stranger = await (await lookup(env, '/v1/reports/status', { reports: [{ ticket: mine.body.ticket, receipt: 'f'.repeat(64) }] })).json();
  const unknown = await (await lookup(env, '/v1/reports/status', { reports: [{ ticket: 'OSL-ZZZZZZ', receipt: 'f'.repeat(64) }] })).json();
  assert.deepEqual(stranger, unknown);
  assert.deepEqual(stranger, { reports: [] });
});

test('a status change reaches the owner with the fixed version and the developer message', async () => {
  const env = makeEnv();
  const mine = await submit(env);
  env.advance(5000);
  const saved = env.service.setStatus(mine.body.ticket, readStatusChange({ status: 'fixed', fixed_version: '0.1.1', player_message: ' Fixed in the next build. ' }).change);
  assert.equal(saved.status, 'fixed');
  const answer = await (await lookup(env, '/v1/reports/status', { reports: [claim(mine)] })).json();
  assert.deepEqual(answer.reports[0], { ticket: mine.body.ticket, status: 'fixed', status_updated_at: env.clock.now,
    fixed_version: '0.1.1', player_message: 'Fixed in the next build.' });
  const unchanged = env.service.setStatus(mine.body.ticket, readStatusChange({ status: 'fixed', fixed_version: '0.1.1', player_message: 'Fixed in the next build.' }).change);
  assert.equal(unchanged.status_updated_at, env.clock.now);
});

test('only the owner reads a report back, and only while its log is kept', async () => {
  const env = makeEnv();
  const mine = await submit(env);
  const read = await lookup(env, '/v1/reports/read', claim(mine));
  assert.equal(read.status, 200);
  const body = await read.json();
  assert.equal(body.report.description, report().description);
  assert.equal(body.report.submitted_at, env.clock.now);
  assert.equal(body.report.ticket, mine.body.ticket);
  const denied = await lookup(env, '/v1/reports/read', { ticket: mine.body.ticket, receipt: 'e'.repeat(64) });
  assert.equal(denied.status, 404);
  assert.deepEqual(await denied.json(), { error: 'not_found' });
  env.advance(REPORT.keptDays * DAY + 1000);
  assert.equal((await lookup(env, '/v1/reports/read', claim(mine))).status, 404);
  const status = await (await lookup(env, '/v1/reports/status', { reports: [claim(mine)] })).json();
  assert.equal(status.reports.length, 1);
});

test('malformed lookups are refused', async () => {
  const env = makeEnv();
  const status = (body) => lookup(env, '/v1/reports/status', body).then((r) => r.status);
  assert.equal(await status({ reports: 'x' }), 400);
  assert.equal(await status({ reports: [null] }), 400);
  assert.equal(await status({ reports: [{ ticket: 'OSL-ABCDEF' }] }), 400);
  assert.equal(await status({ reports: [], extra: 1 }), 400);
  assert.equal(await status([]), 400);
  assert.equal(await status({ reports: Array(REPORT.statusBatch + 1).fill({ ticket: 'OSL-ABCDEF', receipt: 'a'.repeat(64) }) }), 400);
  assert.equal(await status({ reports: Array(REPORT.statusBatch).fill({ ticket: 'OSL-ABCDEF', receipt: 'a'.repeat(64) }) }), 200);
  assert.equal(readClaims({ reports: [{ ticket: 'OSL-ABCDEF', receipt: 'A'.repeat(64) }] }), null);
  assert.equal((await lookup(env, '/v1/reports/read', { ticket: 'OSL-ABCDEF', receipt: 'a'.repeat(64), extra: 1 })).status, 400);
  assert.equal((await lookup(env, '/v1/reports/read', { ticket: 'nope', receipt: 'a'.repeat(64) })).status, 400);
});

test('lookups have their own budget and never use up a report quota', async () => {
  const env = makeEnv();
  const ip = '203.0.113.77';
  const codes = [];
  for (let i = 0; i < 25; i++) codes.push((await lookup(env, '/v1/reports/status', { reports: [] }, { ip })).status);
  assert.equal(codes.filter((c) => c === 200).length, 20);
  assert.ok(codes.filter((c) => c === 429).length >= 1);
  assert.equal((await submit(env, {}, { ip })).response.status, 201);
});

test('only the newest 500 reports are kept, and old ones leave with their age', async () => {
  const env = makeEnv();
  const read = readReport(report()).report;
  for (let i = 0; i < REPORT.kept + 3; i++) {
    env.service.desk.file(read, ticketCode(new Uint8Array([i & 31, (i >> 5) & 31, (i >> 10) & 31, 7, 8, 9])), '10.0.0.1', env.clock.now + i);
  }
  assert.equal(env.rows('reports').length, REPORT.kept);
  assert.equal(env.service.list().reports.length, REPORT.kept);
  env.advance(REPORT.keptDays * DAY + REPORT.kept + 10);
  assert.equal(env.service.list().reports.length, 0);
  assert.equal(env.rows('report_tracking').length, 0);
});

test('request, quota and tracking rows leave with their limits', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  assert.equal(env.rows('report_quota').length, 1);
  env.advance(DAY + 1000);
  await submit(env, {}, { ip: '203.0.113.99' });
  assert.equal(env.rows('report_quota').length, 1);
  env.advance(REPORT.statusKeptDays * DAY);
  env.service.list();
  assert.equal(env.service.desk.claimed(claim(filed)), null);
});

test('a description with a marker reaches only the stored report, nowhere else', async () => {
  const env = makeEnv();
  await submit(env);
  const where = env.store.all("SELECT name FROM sqlite_master WHERE type = 'table'").map((t) => t.name)
    .filter((name) => JSON.stringify(env.rows(name)).includes(MARKER));
  assert.deepEqual(where, ['reports']);
});
