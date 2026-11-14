import assert from 'node:assert/strict';
import { test } from 'node:test';

import { handleAdmin, resetAdminLimits, tokenMatches } from '../src/admin.js';
import { handlePublic } from '../src/api.js';
import { PROTECTION, RateLimiter, ipGroup } from '../src/limits.js';
import { REPORT } from '../src/reports.js';
import { ENV, TOKEN, makeEnv, post, submit } from './harness.js';

const spec = { burst: 3, perSecond: 1 };
const claim = { reports: [{ ticket: 'OSL-ABCDEF', receipt: 'a'.repeat(64) }] };
const status = (env, ip) => handlePublic(post('/v1/reports/status', claim, { ip }), {}, env.desk);
const read = (env, ip) => handlePublic(post('/v1/reports/read', claim.reports[0], { ip }), {}, env.desk);

test('a limiter spends its burst, refuses, and refills with time', () => {
  const limiter = new RateLimiter(spec);
  const now = 1_000_000;
  assert.ok(limiter.allows('a', now));
  for (let i = 0; i < 3; i++) assert.ok(limiter.take('a', now));
  assert.ok(!limiter.allows('a', now) && !limiter.take('a', now));
  assert.ok(!limiter.allows('a', now + 900));
  assert.ok(limiter.take('a', now + 1000));
  assert.ok(limiter.take('b', now));
  assert.ok(limiter.tokens('a', now + 60_000) <= spec.burst);
});

test('asking whether a key is allowed spends nothing', () => {
  const limiter = new RateLimiter(spec);
  for (let i = 0; i < 20; i++) assert.ok(limiter.allows('a', 5));
  for (let i = 0; i < 3; i++) assert.ok(limiter.take('a', 5));
  assert.ok(!limiter.take('a', 5));
});

test('a limiter tracks a bounded number of keys however many addresses ask', () => {
  const limiter = new RateLimiter(spec, 64);
  for (let i = 0; i < 10_000; i++) {
    limiter.take(`10.${i >> 16}.${(i >> 8) & 255}.${i & 255}`, 1_000_000 + i);
    assert.ok(limiter.buckets.size <= 64);
  }
  assert.equal(limiter.buckets.size, 64);
});

test('pruning drops idle keys first and keeps the ones still holding a limit', () => {
  const limiter = new RateLimiter({ burst: 2, perSecond: 1 }, 4);
  const start = 1_000_000;
  for (const key of ['idle1', 'idle2']) limiter.take(key, start);
  limiter.take('busy1', start + 10_000);
  limiter.take('busy1', start + 10_000);
  limiter.take('busy2', start + 10_000);
  assert.equal(limiter.buckets.size, 4);
  limiter.take('new', start + 10_000);
  assert.ok(!limiter.buckets.has('idle1') && !limiter.buckets.has('idle2'));
  assert.ok(limiter.buckets.has('busy1') && limiter.buckets.has('busy2') && limiter.buckets.has('new'));
  assert.ok(!limiter.allows('busy1', start + 10_000));
});

test('when every key is busy the oldest ones are evicted', () => {
  const limiter = new RateLimiter({ burst: 5, perSecond: 0.001 }, 3);
  for (const key of ['a', 'b', 'c']) limiter.take(key, 1_000_000);
  limiter.take('d', 1_000_000);
  assert.deepEqual([...limiter.buckets.keys()], ['b', 'c', 'd']);
});

test('the service keeps at most its tracked-key bound while addresses rotate', async () => {
  const env = makeEnv();
  for (let i = 0; i < PROTECTION.trackedKeys + 200; i++) {
    env.advance(1000);
    const response = await status(env, `198.${51 + (i >> 16)}.${(i >> 8) & 255}.${i & 255}`);
    assert.equal(response.status, 200);
  }
  for (const guard of Object.values(env.service.guards)) assert.ok(guard.buckets.size <= PROTECTION.trackedKeys);
  assert.ok(env.service.guards.statusAddress.buckets.size > 0);
});

test('IPv6 clients are grouped by their /64, whatever the spelling', () => {
  const group = ipGroup('2001:db8:1:2::1');
  assert.equal(group, '2001:db8:1:2');
  for (const spelling of ['2001:0db8:0001:0002:ffff:ffff:ffff:ffff', '2001:DB8:1:2:3:4:5:6', '2001:db8:1:2::', '2001:db8:1:2:0:0:0:9']) {
    assert.equal(ipGroup(spelling), group, spelling);
  }
  assert.notEqual(ipGroup('2001:db8:1:3::1'), group);
  assert.notEqual(ipGroup('2001:db8:2:2::1'), group);
  assert.equal(ipGroup('2001:db8::5'), '2001:db8:0:0');
  assert.equal(ipGroup('::1'), '0:0:0:0');
});

test('an IPv4-mapped IPv6 address is the IPv4 address', () => {
  assert.equal(ipGroup('::ffff:203.0.113.7'), '203.0.113.7');
  assert.equal(ipGroup('::FFFF:203.0.113.7'), '203.0.113.7');
  assert.equal(ipGroup('203.0.113.7'), '203.0.113.7');
  assert.equal(ipGroup(undefined), 'undefined');
});

test('reports from one /64 share an address quota and another /64 does not', async () => {
  const env = makeEnv();
  const inside = ['2001:db8:aa:bb::1', '2001:db8:aa:bb:ffff::2', '2001:0db8:00aa:00bb:1:2:3:4'];
  for (const ip of inside) assert.equal((await submit(env, {}, { ip })).response.status, 201);
  const refused = await submit(env, {}, { ip: '2001:db8:aa:bb::99' });
  assert.equal(refused.response.status, 429);
  assert.equal(refused.body.error, 'hourly_limit');
  assert.equal((await submit(env, {}, { ip: '2001:db8:aa:cc::1' })).response.status, 201);
  assert.ok(!env.everything().includes('2001:db8:aa:bb'));
});

test('an IPv4-mapped address shares the quota of the IPv4 address it carries', async () => {
  const env = makeEnv();
  for (let i = 0; i < REPORT.perAddressPerHour; i++) {
    assert.equal((await submit(env, {}, { ip: i % 2 ? '203.0.113.77' : '::ffff:203.0.113.77' })).response.status, 201);
  }
  assert.equal((await submit(env, {}, { ip: '::ffff:203.0.113.77' })).response.status, 429);
  assert.equal((await submit(env, {}, { ip: '203.0.113.77' })).response.status, 429);
});

test('the global submission burst refuses once spent, whoever asks', async () => {
  const env = makeEnv();
  const answers = [];
  for (let i = 0; i < PROTECTION.submitAll.burst + 1; i++) answers.push((await submit(env, {}, { ip: `192.0.2.${i + 1}` })).response.status);
  assert.deepEqual(answers.slice(0, PROTECTION.submitAll.burst), Array(PROTECTION.submitAll.burst).fill(201));
  assert.equal(answers.at(-1), 429);
  assert.equal(env.rows('reports').length, PROTECTION.submitAll.burst);
});

test('lookups have a global burst and a budget per address, each with Retry-After', async () => {
  const env = makeEnv();
  for (let i = 0; i < PROTECTION.statusAddress.burst; i++) assert.equal((await status(env, '203.0.113.1')).status, 200);
  const perAddress = await status(env, '203.0.113.1');
  assert.equal(perAddress.status, 429);
  assert.equal(perAddress.headers.get('Retry-After'), '60');
  assert.deepEqual(await perAddress.json(), { error: 'limit' });

  for (let i = 0; i < PROTECTION.readAddress.burst; i++) assert.equal((await read(env, '203.0.113.2')).status, 404);
  assert.equal((await read(env, '203.0.113.2')).status, 429);
  assert.equal((await status(env, '203.0.113.2')).status, 200);

  const all = makeEnv();
  const used = [];
  for (let i = 0; i < PROTECTION.lookupAll.burst + 1; i++) used.push((await status(all, `198.51.${i >> 8}.${i & 255}`)).status);
  assert.equal(used.filter((code) => code === 200).length, PROTECTION.lookupAll.burst);
  assert.equal(used.at(-1), 429);
});

test('a refused request costs no write', async () => {
  const env = makeEnv();
  for (let i = 0; i < 6; i++) await submit(env, {}, { ip: '203.0.113.60' });
  const before = env.everything();
  for (let i = 0; i < 5; i++) assert.equal((await submit(env, {}, { ip: '203.0.113.60' })).response.status, 429);
  assert.equal(env.everything(), before);
});

test('the sign-in limit counts one /64 as one address', async () => {
  const env = makeEnv();
  resetAdminLimits();
  const now = 1_700_000_000_000;
  const attempt = (ip, token) => handleAdmin(new Request('https://reports.example/admin/api/reports', {
    headers: { Authorization: `Bearer ${token}`, 'CF-Connecting-IP': ip },
  }), ENV, env.desk, () => {}, now);
  for (let i = 0; i < PROTECTION.adminFailures.burst; i++) assert.equal((await attempt(`2001:db8:5:5::${i + 1}`, 'x'.repeat(30))).status, 401);
  assert.equal((await attempt('2001:db8:5:5:9::1', TOKEN)).status, 429);
  assert.equal((await attempt('2001:db8:5:6::1', TOKEN)).status, 200);
  resetAdminLimits();
});

test('the token comparison accepts only the exact Bearer token', () => {
  assert.ok(tokenMatches(`Bearer ${TOKEN}`, TOKEN));
  for (const authorization of [null, undefined, '', TOKEN, `bearer ${TOKEN}`, `Bearer ${TOKEN} `, `Bearer ${TOKEN}x`, `Bearer ${TOKEN.slice(0, -1)}`,
    `Bearer ${TOKEN.slice(0, -1)}X`, 'Bearer ', 'Basic abc']) {
    assert.ok(!tokenMatches(authorization, TOKEN), String(authorization));
  }
  assert.ok(!tokenMatches(`Bearer ${'a'.repeat(23)}`, 'a'.repeat(23)));
  assert.ok(!tokenMatches('Bearer short', undefined));
});
