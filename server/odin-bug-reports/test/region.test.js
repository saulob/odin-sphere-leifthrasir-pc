import assert from 'node:assert/strict';
import { test } from 'node:test';

import { handleAdmin, resetAdminLimits } from '../src/admin.js';
import { handlePublic } from '../src/api.js';
import { READERS, REPORT, ReportDesk, readReport } from '../src/reports.js';
import { sqliteStore } from '../local/host.js';
import { ENV, TOKEN, fixture, makeEnv, post, submit } from './harness.js';

const without = (field) => {
  const body = fixture();
  delete body[field];
  return body;
};

test('region is one of the fields the service reads, with the canonical request carrying it', () => {
  assert.ok(Object.hasOwn(READERS, 'region'));
  assert.equal(fixture().region, 'USA');
  assert.equal(readReport(fixture()).report.region, 'USA');
});

test('region is optional: an empty text and an absent field both store an empty region', () => {
  assert.equal(readReport({ ...fixture(), region: '' }).report.region, '');
  assert.equal(readReport(without('region')).report.region, '');
});

test('region follows the metadata rules: length limit, no control characters, no padding, text only', () => {
  const bad = (region, code) => assert.equal(readReport({ ...fixture(), region }).code, code, JSON.stringify(region));
  assert.equal(readReport({ ...fixture(), region: 'r'.repeat(REPORT.metadataChars) }).code, undefined);
  assert.equal(readReport({ ...fixture(), region: '€'.repeat(REPORT.metadataChars) }).code, undefined);
  bad('r'.repeat(REPORT.metadataChars + 1), 'too_large');
  bad('€'.repeat(REPORT.metadataChars + 1), 'too_large');
  for (const control of ['\n', '\t', '\r', '\u0000', '\u0007', '\u007f', '\u0085']) bad(`E${control}U`, 'bad_request');
  bad(' EU', 'bad_request');
  bad('EU ', 'bad_request');
  for (const value of [7, null, true, ['EU'], { a: 1 }]) bad(value, 'bad_request');
  assert.equal(readReport({ ...fixture(), region: 'bad\u0007' }).detail, 'region');
});

test('a filed region is stored, read back by its owner, and shown in the admin list and report', async () => {
  const env = makeEnv();
  const filed = await submit(env, { region: 'HK' });
  assert.equal(filed.response.status, 201);
  assert.equal(env.service.get(filed.body.ticket).region, 'HK');
  const read = await (await handlePublic(post('/v1/reports/read', { ticket: filed.body.ticket, receipt: filed.body.receipt }), {}, env.desk)).json();
  assert.equal(read.report.region, 'HK');
  resetAdminLimits();
  const admin = (path) => handleAdmin(new Request(`https://reports.example${path}`, { headers: { Authorization: `Bearer ${TOKEN}` } }), ENV, env.desk);
  assert.equal((await (await admin('/admin/api/reports')).json()).reports[0].region, 'HK');
  assert.equal((await (await admin(`/admin/api/reports/${filed.body.ticket}`)).json()).region, 'HK');
});

test('a request with a bad region is refused by the API and nothing is stored', async () => {
  const env = makeEnv();
  for (const region of ['x'.repeat(65), 'a\u0007b', 'a\nb']) {
    const { response } = await submit(env, { region }, { ip: '198.51.100.1' });
    assert.ok([400, 413].includes(response.status), region);
  }
  assert.equal(env.rows('reports').length, 0);
  assert.equal((await submit(env, { region: '' }, { ip: '198.51.100.2' })).response.status, 201);
});

test('a reports table made before region existed gains the column and keeps its rows', () => {
  const store = sqliteStore();
  store.run(`CREATE TABLE reports (ticket TEXT PRIMARY KEY, ms INTEGER NOT NULL,
    launcher_version TEXT NOT NULL, game_build TEXT NOT NULL, game_version TEXT NOT NULL, cusa TEXT NOT NULL,
    eboot_sha256 TEXT NOT NULL, operating_system TEXT NOT NULL, renderer TEXT NOT NULL, gpu TEXT NOT NULL,
    driver TEXT NOT NULL, render_resolution TEXT NOT NULL, description TEXT NOT NULL, log_file TEXT NOT NULL,
    log_clipped TEXT NOT NULL, log_text TEXT NOT NULL)`);
  store.run(`INSERT INTO reports VALUES ('OSL-AAAAAA', 5, '0.1.0', 'old build', '1.00', '', '', 'Windows', '', '', '', '', 'old report text',
    'odin_sphere_leifthrasir_001.log', 'no', 'old log')`);
  const desk = new ReportDesk(store, 'salt');
  const columns = store.all('PRAGMA table_info(reports)').map((column) => column.name);
  assert.equal(columns.filter((name) => name === 'region').length, 1);
  assert.equal(desk.get('OSL-AAAAAA').region, '');
  assert.equal(desk.list(6).reports[0].region, '');
  desk.file(readReport({ ...fixture(), region: 'EU' }).report, 'OSL-BBBBBB', '203.0.113.1', 10);
  assert.equal(desk.get('OSL-BBBBBB').region, 'EU');
  assert.doesNotThrow(() => new ReportDesk(store, 'salt'));
  assert.equal(store.all('PRAGMA table_info(reports)').length, columns.length);
});
