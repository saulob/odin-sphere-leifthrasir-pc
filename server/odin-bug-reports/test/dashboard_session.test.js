import assert from 'node:assert/strict';
import { test } from 'node:test';

import { ADMIN_CSS, ADMIN_HTML } from '../src/admin_page.js';
import { handlePublic } from '../src/api.js';
import { fixture, makeEnv, post, submit } from './harness.js';
import { dashboardFor, settle } from './dom.js';

const TOKEN = 'admin-token-0123456789-abcdefgh';
const TOKEN_KEY = 'osl-bug-reports-token';
const OPEN_KEY = 'osl-bug-reports-open';

const openFirst = async (page) => {
  page.nodes.get('list').find((n) => n.tag === 'button')[0].click();
  await settle();
};
const deleteRequests = (page) => page.requests.filter((r) => r.init.method === 'DELETE');
const failing = (status) => (path, init) => (init.method === 'DELETE' ? { status, body: { error: 'failed' } } : null);

test('the list puts Status next to the report id and marks the columns that narrow screens drop', async () => {
  const env = makeEnv();
  await submit(env);
  const page = await dashboardFor(env);
  const heads = page.nodes.get('list').find((n) => n.tag === 'th');
  assert.deepEqual(heads.map((h) => h.className), ['col-id', 'col-status', 'col-submitted', 'col-launcher', 'col-build', 'col-region',
    'col-os', 'col-gpu', 'summary']);
  const cells = page.nodes.get('list').find((n) => n.tag === 'td');
  assert.deepEqual(cells.map((c) => c.className), heads.map((h) => h.className));
  assert.match(ADMIN_CSS, /@media \(max-width: 1000px\) \{\s*#list \.col-os, #list \.col-gpu \{ display: none; \}/);
  assert.match(ADMIN_CSS, /@media \(max-width: 760px\) \{\s*#list \.col-launcher, #list \.col-region \{ display: none; \}/);
});

test('a report with no region shows a dash in the list and in its facts', async () => {
  const env = makeEnv();
  const { region: _region, ...withoutRegion } = fixture();
  await handlePublic(post('/v1/reports', { ...withoutRegion, description: 'A report from an older launcher build' }), {}, env.desk);
  const page = await dashboardFor(env);
  assert.equal(page.rows('list')[1][5], '–');
  await openFirst(page);
  assert.ok(page.text('facts').includes('Region–'));
});

test('Delete report asks first, and Cancel, Escape or the backdrop leave the report alone', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const page = await dashboardFor(env);
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
  await openFirst(page);
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
  page.nodes.get('delete-open').click();
  assert.equal(page.nodes.get('delete-backdrop').hidden, false);
  assert.equal(page.text('delete-title'), `Delete ${filed.body.ticket} permanently?`);
  assert.equal(page.nodes.get('delete-cancel').focused, true);
  page.nodes.get('delete-cancel').click();
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
  page.nodes.get('delete-open').click();
  page.press('Escape');
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
  page.nodes.get('delete-open').click();
  page.nodes.get('delete-backdrop').fire('click', { target: page.nodes.get('delete-dialog') });
  assert.equal(page.nodes.get('delete-backdrop').hidden, false);
  page.nodes.get('delete-backdrop').click();
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
  assert.equal(deleteRequests(page).length, 0);
  assert.ok(env.service.get(filed.body.ticket));
});

test('confirming deletes the report on the service and removes it from the page', async () => {
  const env = makeEnv();
  const gone = await submit(env, {}, { ip: '203.0.113.1' });
  env.advance(1000);
  const kept = await submit(env, { description: 'The report that stays on the list' }, { ip: '203.0.113.2' });
  const page = await dashboardFor(env);
  assert.equal(page.rows('list').length, 3);
  page.nodes.get('list').find((n) => n.tag === 'button')[1].click();
  await settle();
  assert.equal(page.text('title'), gone.body.ticket);
  page.nodes.get('delete-open').click();
  page.nodes.get('delete-confirm').click();
  await settle();
  const sent = deleteRequests(page);
  assert.equal(sent.length, 1);
  assert.equal(sent[0].path, `/admin/api/reports/${gone.body.ticket}`);
  assert.equal(sent[0].init.headers.Authorization, `Bearer ${TOKEN}`);
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
  assert.equal(page.nodes.get('detail').hidden, true);
  assert.deepEqual(page.rows('list').slice(1).map((row) => row[0]), [kept.body.ticket]);
  assert.equal(page.text('count'), `${gone.body.ticket} deleted. 1 reports`);
  assert.equal(page.text('c-new-status'), '1');
  assert.equal(env.service.get(gone.body.ticket), null);
  assert.equal(env.rows('report_tracking').length, 1);
  assert.ok(env.service.get(kept.body.ticket));
});

test('deleting a report that is already gone counts as deleted', async () => {
  const env = makeEnv();
  await submit(env);
  const page = await dashboardFor(env, { answer: failing(404) });
  await openFirst(page);
  page.nodes.get('delete-open').click();
  page.nodes.get('delete-confirm').click();
  await settle();
  assert.equal(page.rows('list').length, 1);
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
});

test('a failed delete keeps the dialog and the report, and says so', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const page = await dashboardFor(env, { answer: failing(502) });
  await openFirst(page);
  page.nodes.get('delete-open').click();
  page.nodes.get('delete-confirm').click();
  await settle();
  assert.equal(page.nodes.get('delete-backdrop').hidden, false);
  assert.equal(page.text('delete-error'), 'Could not delete the report. Try again.');
  assert.equal(page.nodes.get('delete-confirm').disabled, false);
  assert.equal(page.rows('list')[1][0], filed.body.ticket);
});

test('a token refused while deleting returns to the sign-in without deleting', async () => {
  const env = makeEnv();
  await submit(env);
  const page = await dashboardFor(env, { answer: failing(401) });
  await openFirst(page);
  page.nodes.get('delete-open').click();
  page.nodes.get('delete-confirm').click();
  await settle();
  assert.equal(page.nodes.get('login').hidden, false);
  assert.equal(page.nodes.get('delete-backdrop').hidden, true);
  assert.equal(page.text('login-error'), 'The token was not accepted.');
  assert.equal(env.rows('reports').length, 1);
});

test('the session menu holds Sign out, opens and closes, and exists only while signed in', async () => {
  const env = makeEnv();
  await submit(env);
  assert.ok(ADMIN_HTML.indexOf('id="signout"') > ADMIN_HTML.indexOf('id="session-menu"'));
  const out = await dashboardFor(env, { signIn: false });
  assert.equal(out.nodes.get('session').hidden, true);
  const page = await dashboardFor(env);
  assert.equal(page.nodes.get('session').hidden, false);
  assert.equal(page.nodes.get('session-menu').hidden, true);
  page.nodes.get('menu-button').click();
  assert.equal(page.nodes.get('session-menu').hidden, false);
  assert.equal(page.nodes.get('menu-button').attrs['aria-expanded'], 'true');
  assert.equal(page.nodes.get('signout').focused, true);
  assert.match(page.text('session-state'), /held in memory only/);
  page.clickAnywhere(page.nodes.get('session'));
  assert.equal(page.nodes.get('session-menu').hidden, false);
  page.clickAnywhere(page.nodes.get('list'));
  assert.equal(page.nodes.get('session-menu').hidden, true);
  page.nodes.get('menu-button').click();
  page.press('Escape');
  assert.equal(page.nodes.get('session-menu').hidden, true);
  page.nodes.get('menu-button').click();
  page.nodes.get('signout').click();
  assert.equal(page.nodes.get('session').hidden, true);
  assert.equal(page.nodes.get('session-menu').hidden, true);
  assert.equal(page.nodes.get('dash').hidden, true);
  assert.equal(page.nodes.get('login').hidden, false);
});

test('without Keep for this tab nothing is stored in the browser', async () => {
  const page = await dashboardFor(makeEnv());
  assert.equal(page.storage.size, 0);
});

test('Keep for this tab restores the session and the open report after a reload, and Sign out forgets both', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const first = await dashboardFor(env, { remember: true });
  assert.equal(first.storage.get(TOKEN_KEY), TOKEN);
  assert.match(first.text('session-state'), /kept for this tab only/);
  await openFirst(first);
  assert.equal(first.storage.get(OPEN_KEY), filed.body.ticket);

  const reloaded = await dashboardFor(env, { signIn: false, stored: Object.fromEntries(first.storage) });
  assert.equal(reloaded.nodes.get('login').hidden, true);
  assert.equal(reloaded.nodes.get('dash').hidden, false);
  assert.equal(reloaded.nodes.get('remember').checked, true);
  assert.equal(reloaded.nodes.get('detail').hidden, false);
  assert.equal(reloaded.text('title'), filed.body.ticket);

  reloaded.nodes.get('close').click();
  assert.equal(reloaded.storage.has(OPEN_KEY), false);
  await openFirst(reloaded);
  reloaded.nodes.get('menu-button').click();
  reloaded.nodes.get('signout').click();
  assert.equal(reloaded.storage.size, 0);
  assert.equal(reloaded.nodes.get('remember').checked, false);
});

test('a remembered report that was deleted meanwhile is not reopened', async () => {
  const env = makeEnv();
  const filed = await submit(env);
  const stored = { [TOKEN_KEY]: TOKEN, [OPEN_KEY]: filed.body.ticket };
  env.service.remove(filed.body.ticket);
  const page = await dashboardFor(env, { signIn: false, stored });
  assert.equal(page.nodes.get('dash').hidden, false);
  assert.equal(page.nodes.get('detail').hidden, true);
  assert.equal(page.storage.has(OPEN_KEY), false);
  assert.ok(!page.requests.some((r) => r.path.includes(filed.body.ticket)));
});

test('a remembered value that is not a report id is ignored', async () => {
  const env = makeEnv();
  await submit(env);
  const page = await dashboardFor(env, { signIn: false, stored: { [TOKEN_KEY]: TOKEN, [OPEN_KEY]: '../../x' } });
  assert.equal(page.nodes.get('detail').hidden, true);
  assert.deepEqual(page.requests.map((r) => r.path), ['/admin/api/reports']);
});
