// The Worker's private dashboard under /admin. Its secret, OSL_DASHBOARD_TOKEN, is a Cloudflare secret sent as a
// Bearer header; the page itself is static and holds nothing. No Cloudflare import, so Node runs it in the tests.
import { ADMIN_CSS, ADMIN_HTML, ADMIN_JS } from './admin_page.js';
import { RateLimiter, PROTECTION, ipGroup } from './limits.js';
import { isTicket, logDownloadName, readStatusChange } from './reports.js';
import { sha256Hex } from './sha256.js';

const MIN_TOKEN_CHARS = 24;
const MAX_STATUS_BODY_BYTES = 2048;
const REPORTS_PATH = '/admin/api/reports';
const METHODS = { report: ['GET', 'DELETE'], report_status: ['POST'] };

const SECURITY_HEADERS = {
  'Cache-Control': 'no-store',
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer',
  'X-Frame-Options': 'DENY',
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Resource-Policy': 'same-origin',
  'Content-Security-Policy': "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; " +
    "base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
};

const ASSETS = {
  '/admin': [ADMIN_HTML, 'text/html; charset=utf-8'],
  '/admin/': [ADMIN_HTML, 'text/html; charset=utf-8'],
  '/admin/app.css': [ADMIN_CSS, 'text/css; charset=utf-8'],
  '/admin/app.js': [ADMIN_JS, 'text/javascript; charset=utf-8'],
};

export const isAdminPath = (pathname) => pathname === '/admin' || pathname.startsWith('/admin/');

function reply(body, status, type, extra = {}) {
  return new Response(body, { status, headers: { ...SECURITY_HEADERS, 'Content-Type': type, ...extra } });
}

const json = (value, status = 200, extra = {}) => reply(JSON.stringify(value), status, 'application/json; charset=utf-8', extra);

// Compares digests so the time taken does not depend on where the texts differ.
export function tokenMatches(authorization, token) {
  if (typeof token !== 'string' || token.length < MIN_TOKEN_CHARS) return false;
  const given = typeof authorization === 'string' && authorization.startsWith('Bearer ') ? authorization.slice(7) : '';
  const a = sha256Hex(given);
  const b = sha256Hex(token);
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

// The dashboard API behind a path, or null. A report id is checked after the token, before anything reads it.
function adminApi(pathname) {
  if (pathname === REPORTS_PATH) return { name: 'reports' };
  if (!pathname.startsWith(`${REPORTS_PATH}/`)) return null;
  const [ticket, rest, ...more] = pathname.slice(REPORTS_PATH.length + 1).split('/');
  if (more.length > 0) return null;
  if (rest === undefined) return { name: 'report', ticket };
  if (rest === 'log') return { name: 'report_log', ticket };
  if (rest === 'status') return { name: 'report_status', ticket };
  return null;
}

async function saveStatus(request, ticket, source) {
  const length = Number(request.headers.get('Content-Length') ?? 0);
  if (length > MAX_STATUS_BODY_BYTES) return json({ error: 'too_large' }, 413);
  const body = await request.text();
  if (new TextEncoder().encode(body).length > MAX_STATUS_BODY_BYTES) return json({ error: 'too_large' }, 413);
  let parsed = null;
  try {
    parsed = JSON.parse(body);
  } catch {
    return json({ error: 'bad_request', detail: 'invalid JSON' }, 400);
  }
  const { change, detail } = readStatusChange(parsed);
  if (!change) return json({ error: 'bad_request', detail }, 400);
  const saved = await source.setStatus(ticket, change);
  return saved ? json(saved) : json({ error: 'not_found' }, 404);
}

// A page of another site must not be able to drive the API with the admin's browser: the Bearer header already stops
// that, and a request that names another origin is refused as well.
const crossOrigin = (request, url) => {
  const origin = request.headers.get('Origin');
  return origin !== null && origin !== url.origin;
};

const failedSignIns = new RateLimiter(PROTECTION.adminFailures);

export function resetAdminLimits() {
  failedSignIns.buckets.clear();
}

// source: { list(), get(ticket), setStatus(ticket, change), remove(ticket) } as promises, used only after the token matched.
export async function handleAdmin(request, env, source, log = () => {}, now = Date.now()) {
  const url = new URL(request.url);
  const api = adminApi(url.pathname);
  const methods = METHODS[api?.name] ?? ['GET'];
  if (!methods.includes(request.method)) return json({ error: 'method_not_allowed' }, 405, { Allow: methods.join(', ') });
  const asset = ASSETS[url.pathname];
  if (asset) return reply(asset[0], 200, asset[1]);
  if (!api) return json({ error: 'not_found' }, 404);
  if (crossOrigin(request, url)) return json({ error: 'origin_not_allowed' }, 403);
  if (typeof env.OSL_DASHBOARD_TOKEN !== 'string' || env.OSL_DASHBOARD_TOKEN.length < MIN_TOKEN_CHARS) {
    return json({ error: 'not_configured' }, 503);
  }
  const address = ipGroup(request.headers.get('CF-Connecting-IP') ?? 'local');
  if (!failedSignIns.allows(address, now)) return json({ error: 'limit' }, 429, { 'Retry-After': '60' });
  if (!tokenMatches(request.headers.get('Authorization'), env.OSL_DASHBOARD_TOKEN)) {
    failedSignIns.take(address, now);
    return json({ error: 'unauthorized' }, 401, { 'WWW-Authenticate': 'Bearer' });
  }
  if (api.ticket !== undefined && !isTicket(api.ticket)) return json({ error: 'bad_request', detail: 'invalid report id' }, 400);
  try {
    if (api.name === 'reports') return json(await source.list());
    if (api.name === 'report_status') return await saveStatus(request, api.ticket, source);
    if (request.method === 'DELETE') {
      return await source.remove(api.ticket) ? json({ deleted: api.ticket }) : json({ error: 'not_found' }, 404);
    }
    const report = await source.get(api.ticket);
    if (!report) return json({ error: 'not_found' }, 404);
    if (api.name === 'report') return json(report);
    return reply(report.log_text, 200, 'text/plain; charset=utf-8',
      { 'Content-Disposition': `attachment; filename="${logDownloadName(report)}"` });
  } catch (error) {
    log(`admin: reports unavailable: ${error?.message ?? error}`);
    return json({ error: 'unavailable' }, 502);
  }
}
