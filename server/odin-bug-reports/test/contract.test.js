import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { handlePublic } from '../src/api.js';
import { READERS, readReport } from '../src/reports.js';
import { makeEnv, post } from './harness.js';

// OSL_CPP_REQUEST names a request file the launcher's test built from a synthetic log.
const requestFile = process.env.OSL_CPP_REQUEST;
const skip = requestFile ? false : 'set OSL_CPP_REQUEST to a request file the launcher built';

test('the request the launcher builds is accepted and stored as previewed', { skip }, async () => {
  const text = readFileSync(requestFile, 'utf8');
  const sent = JSON.parse(text);
  assert.deepEqual(Object.keys(sent).sort(), Object.keys(READERS).sort());
  const { report, code, detail } = readReport(sent);
  assert.equal(code, undefined, `${code} ${detail}`);

  const env = makeEnv();
  const response = await handlePublic(post('/v1/reports', null, { raw: text }), {}, env.desk);
  assert.equal(response.status, 201);
  const { ticket } = await response.json();
  const stored = env.service.get(ticket);
  for (const [field, value] of Object.entries(report)) assert.equal(stored[field], value, field);
  assert.ok(!text.includes('ana.silva') && !text.includes('hunter2') && !text.includes('203.0.113.42'));
});
