import assert from 'node:assert/strict';
import { test } from 'node:test';

import { ADMIN_HTML } from '../src/admin_page.js';
import { makeEnv, submit } from './harness.js';
import { dashboardFor, openDashboard, settle } from './dom.js';

test('the dashboard lists reports with date, versions, system, renderer and status, and counts the statuses', async () => {
  const env = makeEnv({ now: () => 1_700_000_000_000 });
  await submit(env, { description: 'First problem with the intro movie' }, { ip: '203.0.113.1' });
  env.advance(1000);
  const second = await submit(env, { description: 'Second problem in the menu', operating_system: 'Windows 10 (build 19045)', renderer: 'OpenGL' }, { ip: '203.0.113.2' });
  env.service.setStatus(second.body.ticket, { status: 'investigating', fixed_version: '', player_message: '' });
  const page = await dashboardFor(env, { now: env.clock.now });
  const rows = page.rows('list');
  assert.deepEqual(rows[0], ['Report ID', 'Status', 'Submitted', 'Launcher', 'Game build', 'Region', 'Operating system', 'Renderer · GPU',
    'Description']);
  assert.equal(rows.length, 3);
  assert.equal(rows[1][0], second.body.ticket);
  assert.equal(rows[1][1], 'Investigating');
  assert.equal(rows[2][1], 'Open');
  assert.equal(rows[1][3], '0.1.0');
  assert.equal(rows[1][4], 'CUSA05083 1.00 USA 1.00');
  assert.equal(rows[1][5], 'USA');
  assert.equal(rows[1][6], 'Windows 10 (build 19045)');
  assert.equal(rows[1][7], 'OpenGL · NVIDIA GeForce RTX 4070');
  assert.equal(rows[1][8], 'Second problem in the menu');
  assert.equal(page.text('c-new'), '2');
  assert.equal(page.text('c-new-status'), '1');
  assert.equal(page.text('c-investigating'), '1');
  assert.equal(page.text('c-fixed'), '0');
  assert.equal(page.text('count'), '2 reports');
  assert.equal(page.nodes.get('empty').hidden, true);
});

test('an empty service says so', async () => {
  const page = await dashboardFor(makeEnv());
  assert.equal(page.nodes.get('empty').hidden, false);
  assert.match(ADMIN_HTML, /<p id="empty" class="empty" hidden>No bug reports yet\.<\/p>/);
  assert.equal(page.nodes.get('list-wrap').hidden, true);
});

test('a report opens with its facts and its log as text, never as markup', async () => {
  const env = makeEnv();
  const hostile = '<script>alert(1)</script> & "quoted"\nsecond line\n';
  const filed = await submit(env, { log_text: hostile, description: 'Description with <b>markup</b> in it' });
  const page = await dashboardFor(env);
  page.nodes.get('list').find((n) => n.tag === 'button')[0].click();
  await settle();
  assert.equal(page.nodes.get('detail').hidden, false);
  assert.equal(page.text('title'), filed.body.ticket);
  assert.equal(page.text('text'), 'Description with <b>markup</b> in it');
  assert.equal(page.text('log'), hostile);
  const facts = page.text('facts');
  for (const expected of ['0.1.0', 'CUSA05083 1.00 USA', 'Region', 'CUSA05083', 'Windows 11 (build 26200)', 'Vulkan', 'NVIDIA GeForce RTX 4070',
    'NVIDIA 621.1.0.0, Vulkan 1.4.357', '1920x1080 (native)', 'odin_sphere_leifthrasir_026.log']) {
    assert.ok(facts.includes(expected), expected);
  }
  assert.equal(page.nodes.get('status').value, 'new');
  assert.equal(page.nodes.get('fixed-field').hidden, true);
  page.nodes.get('close').click();
  assert.equal(page.nodes.get('detail').hidden, true);
});

test('the log is downloaded with its safe name and copied exactly', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const page = await dashboardFor(env);
  page.nodes.get('list').find((n) => n.tag === 'button')[0].click();
  await settle();
  page.nodes.get('download').click();
  await settle();
  assert.deepEqual(page.downloads, [{ href: 'blob:test', download: `${filed.body.ticket}-odin_sphere_leifthrasir_026.log` }]);
  page.nodes.get('copy').click();
  await settle();
  assert.equal(page.clipboard.length, 1);
  assert.match(page.clipboard[0], /Renderer: selected vulkan/);
  assert.ok(page.requests.every((r) => r.init.headers.Authorization.startsWith('Bearer ') && r.init.credentials === 'omit'));
});

test('saving a status sends only the status, the fixed version and the message, and updates the list', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const page = await dashboardFor(env);
  page.nodes.get('list').find((n) => n.tag === 'button')[0].click();
  await settle();
  page.nodes.get('status').value = 'fixed';
  for (const fn of page.nodes.get('status').listeners.change) fn();
  assert.equal(page.nodes.get('fixed-field').hidden, false);
  page.nodes.get('fixed').value = '0.1.1';
  page.nodes.get('message').value = 'Fixed in 0.1.1.';
  page.nodes.get('status-save').click();
  await settle();
  const post = page.requests.find((r) => r.init.method === 'POST');
  assert.equal(post.path, `/admin/api/reports/${filed.body.ticket}/status`);
  assert.deepEqual(JSON.parse(post.init.body), { status: 'fixed', fixed_version: '0.1.1', player_message: 'Fixed in 0.1.1.' });
  assert.equal(post.init.headers['Content-Type'], 'application/json');
  assert.equal(page.text('status-note'), 'Saved.');
  assert.equal(page.rows('list')[1][1], 'Fixed');
  assert.equal(page.text('c-fixed'), '1');
  assert.deepEqual(env.service.get(filed.body.ticket).fixed_version, '0.1.1');
});

test('a token the service does not accept returns to the sign-in', async () => {
  const env = makeEnv();
  const page = await openDashboard(() => ({ status: 401, body: { error: 'unauthorized' } }));
  assert.equal(page.nodes.get('dash').hidden, true);
  assert.equal(page.nodes.get('login').hidden, false);
  assert.equal(page.text('login-error'), 'The token was not accepted.');
  assert.equal(page.nodes.get('token').value, '');
  assert.equal(env.rows('reports').length, 0);
});
