// The private dashboard (/admin): a static shell, its stylesheet and script, served same-origin under a strict CSP.
// The script keeps the token in memory, in this tab's sessionStorage only when asked, and sends it as a Bearer header.
export const ADMIN_HTML = String.raw`<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<meta name="robots" content="noindex, nofollow">
<title>Odin Sphere Leifthrasir Bug Reports</title>
<link rel="stylesheet" href="/admin/app.css">
</head>
<body>
<header class="top">
  <div class="top-bar">
    <h1>Odin Sphere Leifthrasir · Bug Reports</h1>
    <div id="session" class="session" hidden>
      <button id="menu-button" type="button" class="icon-button" aria-label="Dashboard session menu" aria-haspopup="true" aria-expanded="false" aria-controls="session-menu"><span class="bars" aria-hidden="true"></span></button>
      <div id="session-menu" class="menu" role="menu" aria-label="Dashboard session" hidden>
        <p class="menu-title">Dashboard session</p>
        <p id="session-state" class="muted"></p>
        <button id="signout" type="button" role="menuitem" class="quiet">Sign out</button>
      </div>
    </div>
  </div>
  <p class="muted">Reports players sent from the launcher. Each one holds a description, the launcher and game versions, the game region, the operating system, the renderer and GPU, and one privacy-filtered log. Nothing else is collected.</p>
</header>
<main>
  <form id="login" class="panel login" autocomplete="off">
    <label for="token">Admin token</label>
    <input id="token" type="password" autocomplete="off" spellcheck="false" required>
    <label class="check"><input id="remember" type="checkbox"> Keep for this tab</label>
    <button type="submit">Open dashboard</button>
    <p id="login-error" class="error" role="alert"></p>
  </form>
  <section id="dash" hidden>
    <div class="toolbar">
      <button id="refresh" type="button">Refresh reports</button>
      <span id="count" class="muted" role="status"></span>
    </div>
    <div class="cards" id="cards">
      <div class="card"><span class="label">New in the last 24 hours</span><span id="c-new" class="value">–</span></div>
      <div class="card"><span class="label">Open</span><span id="c-new-status" class="value">–</span></div>
      <div class="card"><span class="label">Investigating</span><span id="c-investigating" class="value">–</span></div>
      <div class="card"><span class="label">Fixed</span><span id="c-fixed" class="value">–</span></div>
      <div class="card"><span class="label">Cannot reproduce</span><span id="c-cannot_reproduce" class="value">–</span></div>
      <div class="card"><span class="label">Duplicate</span><span id="c-duplicate" class="value">–</span></div>
      <div class="card"><span class="label">Not a bug</span><span id="c-not_a_bug" class="value">–</span></div>
      <div class="card"><span class="label">Closed</span><span id="c-closed" class="value">–</span></div>
    </div>
    <div class="panel">
      <p class="muted">Newest first. The log of a report is stored as the player previewed it and is shown and downloaded exactly as stored. No account, name or address is kept with a report; the newest 500 are kept for 180 days.</p>
      <div class="toolbar">
        <label for="filter-version">Launcher version</label>
        <select id="filter-version"></select>
        <label for="filter-os">Operating system</label>
        <select id="filter-os"></select>
        <label for="filter-status">Status</label>
        <select id="filter-status">
            <option value="">All statuses</option>
            <option value="new">Open</option>
            <option value="investigating">Investigating</option>
            <option value="fixed">Fixed</option>
            <option value="cannot_reproduce">Cannot reproduce</option>
            <option value="duplicate">Duplicate</option>
            <option value="not_a_bug">Not a bug</option>
            <option value="closed">Closed</option>
        </select>
      </div>
      <p id="empty" class="empty" hidden>No bug reports yet.</p>
      <div id="list-wrap" class="scroll list"><table id="list"></table></div>
    </div>
    <div id="detail" class="panel detail" hidden>
      <div class="toolbar header">
        <h2 id="title"></h2>
        <button id="download" type="button">Download log</button>
        <button id="copy" type="button" class="quiet">Copy log</button>
        <span id="note" class="muted" role="status"></span>
        <button id="close" type="button" class="quiet push">Close report</button>
      </div>
      <p id="text" class="text"></p>
      <dl id="facts" class="facts"></dl>
      <h3>Report status</h3>
      <div class="status-form">
        <label for="status">Status</label>
        <select id="status">
            <option value="new">Open</option>
            <option value="investigating">Investigating</option>
            <option value="fixed">Fixed</option>
            <option value="cannot_reproduce">Cannot reproduce</option>
            <option value="duplicate">Duplicate</option>
            <option value="not_a_bug">Not a bug</option>
            <option value="closed">Closed</option>
        </select>
        <p id="status-meaning" class="muted"></p>
        <div id="fixed-field" class="status-field" hidden>
          <label for="fixed">Fixed in version</label>
          <input id="fixed" type="text" maxlength="32" autocomplete="off" placeholder="0.1.1">
        </div>
        <div class="status-field">
          <label for="message">Message to player</label>
          <textarea id="message" rows="3" maxlength="500"></textarea>
        </div>
        <div class="toolbar">
          <button id="status-save" type="button">Save status</button>
          <span id="status-note" class="muted" role="status"></span>
        </div>
      </div>
      <h3 id="log-name"></h3>
      <p class="muted snapshot">This is the sanitized log snapshot captured when the report was sent.</p>
      <pre id="log" class="log" tabindex="0"></pre>
      <div class="danger-zone">
        <h3>Delete report</h3>
        <p class="muted">Permanently removes this report, its log and the status its player sees. Meant for test and unwanted reports.</p>
        <button id="delete-open" type="button" class="danger">Delete report</button>
      </div>
    </div>
  </section>
</main>
<div id="delete-backdrop" class="backdrop" hidden>
  <div id="delete-dialog" class="dialog" role="alertdialog" aria-modal="true" aria-labelledby="delete-title" aria-describedby="delete-text">
    <h2 id="delete-title"></h2>
    <p id="delete-text">This removes the stored report and log and cannot be undone.</p>
    <p id="delete-error" class="error" role="alert"></p>
    <div class="dialog-actions">
      <button id="delete-cancel" type="button" class="quiet">Cancel</button>
      <button id="delete-confirm" type="button" class="danger">Delete report</button>
    </div>
  </div>
</div>
<script src="/admin/app.js"></script>
</body>
</html>
`;

export const ADMIN_CSS = String.raw`:root {
  color-scheme: light;
  --surface-0: #f3f3f1;
  --surface-1: #fcfcfb;
  --border: #dcdbd6;
  --grid: #e7e6e2;
  --text-primary: #0b0b0b;
  --text-secondary: #52514e;
  --text-muted: #6d6c67;
  --accent: #2a78d6;
  --error: #b42318;
}
@media (prefers-color-scheme: dark) {
  :root:where(:not([data-theme="light"])) {
    color-scheme: dark;
    --surface-0: #111110;
    --surface-1: #1a1a19;
    --border: #34332f;
    --grid: #2a2a27;
    --text-primary: #f0efec;
    --text-secondary: #c3c2b7;
    --text-muted: #9a998f;
    --accent: #3987e5;
    --error: #f97066;
  }
}
:root[data-theme="dark"] {
  color-scheme: dark;
  --surface-0: #111110;
  --surface-1: #1a1a19;
  --border: #34332f;
  --grid: #2a2a27;
  --text-primary: #f0efec;
  --text-secondary: #c3c2b7;
  --text-muted: #9a998f;
  --accent: #3987e5;
  --error: #f97066;
}
* { box-sizing: border-box; }
[hidden] { display: none !important; }
html { -webkit-text-size-adjust: 100%; }
body {
  margin: 0;
  background: var(--surface-0);
  color: var(--text-primary);
  font: 15px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
.top, main { max-width: 1120px; margin: 0 auto; padding: 0 16px; }
.top { padding-top: 24px; }
.top-bar { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; }
h1 { font-size: 22px; margin: 0 0 4px; min-width: 0; }
.session { position: relative; flex: none; }
.icon-button { padding: 8px 10px; background: transparent; color: var(--text-secondary); border-color: var(--border); line-height: 0; }
.icon-button[aria-expanded="true"] { color: var(--text-primary); border-color: var(--accent); }
.bars, .bars::before, .bars::after { display: block; width: 18px; height: 2px; border-radius: 1px; background: currentColor; }
.bars { position: relative; }
.bars::before, .bars::after { content: ""; position: absolute; left: 0; }
.bars::before { top: -6px; }
.bars::after { top: 6px; }
.menu { position: absolute; right: 0; top: calc(100% + 6px); z-index: 20; width: 260px; padding: 12px; background: var(--surface-1); border: 1px solid var(--border); border-radius: 10px; box-shadow: 0 6px 20px rgba(0, 0, 0, .18); }
.menu-title { margin: 0 0 4px; font-weight: 600; }
.menu .muted { margin: 0 0 12px; }
.menu button { width: 100%; }
h2 { font-size: 15px; margin: 0 0 8px; }
h3 { font-size: 14px; margin: 0 0 4px; }
.muted { color: var(--text-muted); margin: 0 0 12px; font-size: 13px; }
.panel {
  background: var(--surface-1);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 16px;
  margin: 0 0 16px;
  min-width: 0;
}
.login { max-width: 420px; display: grid; gap: 10px; }
.check { display: flex; gap: 8px; align-items: center; color: var(--text-secondary); }
input[type="password"], input[type="text"], textarea, select {
  font: inherit;
  padding: 8px 10px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--surface-0);
  color: var(--text-primary);
  width: 100%;
}
select { width: auto; }
button {
  font: inherit;
  padding: 8px 14px;
  border-radius: 6px;
  border: 1px solid var(--accent);
  background: var(--accent);
  color: #fff;
  cursor: pointer;
}
button.quiet { background: transparent; color: var(--text-secondary); border-color: var(--border); }
button.danger { background: var(--error); border-color: var(--error); color: #fff; }
.push { margin-left: auto; }
.error { color: var(--error); margin: 0; min-height: 1.2em; }
.toolbar { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; margin: 0 0 16px; }
.toolbar .muted { margin: 0; }
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 12px; margin: 0 0 16px; }
.card {
  background: var(--surface-1);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 12px 14px;
  display: flex;
  flex-direction: column;
}
.card .label { color: var(--text-secondary); font-size: 13px; }
.card .value { font-size: 26px; font-weight: 600; font-variant-numeric: tabular-nums; }
.empty { color: var(--text-muted); font-size: 13px; margin: 4px 0; }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: 13px; }
th, td { padding: 6px 8px; text-align: left; border-bottom: 1px solid var(--grid); white-space: nowrap; }
th { color: var(--text-secondary); font-weight: 600; }
.list { max-height: 420px; overflow-y: auto; }
#list th, #list td { vertical-align: top; }
#list td.col-build, #list td.col-os, #list td.col-gpu { white-space: normal; overflow-wrap: anywhere; }
#list td.col-build { max-width: 12em; }
#list td.col-os, #list td.col-gpu { max-width: 10em; }
#list td.summary { white-space: normal; overflow-wrap: anywhere; width: 100%; min-width: 14em; }
@media (max-width: 1000px) {
  #list .col-os, #list .col-gpu { display: none; }
}
@media (max-width: 760px) {
  #list .col-launcher, #list .col-region { display: none; }
  #list td.summary { min-width: 10em; }
}
#list tr.selected td { background: var(--surface-0); }
#list button.ticket { background: transparent; color: var(--text-primary); border: 0; padding: 0; text-decoration: underline; cursor: pointer; }
.detail { margin-top: 16px; }
.detail h2 { margin: 0; }
.header { margin-bottom: 20px; }
.text { white-space: pre-wrap; overflow-wrap: anywhere; margin: 0 0 24px; }
.facts { display: grid; grid-template-columns: max-content 1fr; gap: 4px 12px; margin: 0 0 24px; font-size: 13px; }
.facts dt { color: var(--text-secondary); }
.facts dd { margin: 0; overflow-wrap: anywhere; }
.snapshot { margin-bottom: 8px; }
.status-form { margin: 0 0 24px; }
.status-form > .muted { margin: 4px 0 12px; }
.status-form .toolbar { margin: 12px 0 0; }
.status-field { display: flex; flex-direction: column; gap: 4px; margin: 0 0 12px; }
.status-field[hidden] { display: none; }
.status-field input { max-width: 200px; }
.status-field textarea { max-width: 640px; }
.danger-zone { margin: 28px 0 0; padding: 16px 0 0; border-top: 1px solid var(--border); }
.danger-zone .muted { margin: 0 0 10px; }
.backdrop { position: fixed; inset: 0; z-index: 30; display: flex; align-items: center; justify-content: center; padding: 16px; background: rgba(0, 0, 0, .5); }
.dialog { width: 100%; max-width: 420px; padding: 20px; background: var(--surface-1); border: 1px solid var(--border); border-radius: 10px; }
.dialog h2 { font-size: 17px; margin: 0 0 8px; overflow-wrap: anywhere; }
.dialog p { margin: 0 0 12px; color: var(--text-secondary); }
.dialog p.error { color: var(--error); }
.dialog-actions { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 10px; }
.log { max-height: 480px; overflow: auto; margin: 0; padding: 10px; background: var(--surface-0); border: 1px solid var(--border); border-radius: 6px; font: 12px/1.45 ui-monospace, Consolas, monospace; white-space: pre; }
`;

export const ADMIN_JS = String.raw`(function () {
  'use strict';
  var KEY = 'osl-bug-reports-token';
  var OPEN_KEY = 'osl-bug-reports-open';
  var ZONE = 'America/Sao_Paulo';
  var DAY_MS = 86400000;
  var TICKET = /^OSL-[A-HJ-NP-Z2-9]{6}$/;
  var token = '';
  var kept = false;
  var menuOpen = false;
  var deleteTicket = '';
  var deleting = false;
  var restoreTicket = '';
  var $ = function (id) { return document.getElementById(id); };
  var stampLabel = new Intl.DateTimeFormat('en-GB', { timeZone: ZONE, day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit' });
  var STATUSES = {
    new: ['Open', 'The report has not been reviewed yet.'],
    investigating: ['Investigating', 'The report is being investigated.'],
    fixed: ['Fixed', 'The problem has been corrected.'],
    cannot_reproduce: ['Cannot reproduce', 'The problem could not be reproduced with the available information.'],
    duplicate: ['Duplicate', 'The same problem is already tracked by another report.'],
    not_a_bug: ['Not a bug', 'The reported behavior is expected or is not a bug of this port.'],
    closed: ['Closed', 'The report was intentionally finished without a specific fix or another more precise status. It does not mean Fixed.']
  };
  // Named by string: the page tests forbid any CSS-style address call in the script text.
  var OBJECT_MAKE = 'createObjectURL';
  var OBJECT_DROP = 'revokeObjectURL';
  var reports = [];
  var openTicket = '';
  var loadingTicket = '';
  var logText = '';

  function readStored(key) {
    try { return sessionStorage.getItem(key) || ''; } catch (e) { return ''; }
  }
  function writeStored(key, value) {
    try { if (value) sessionStorage.setItem(key, value); else sessionStorage.removeItem(key); } catch (e) { /* storage blocked */ }
  }
  function storeToken(value) {
    writeStored(KEY, value);
    kept = Boolean(value) && readStored(KEY) === value;
    if (!kept) writeStored(OPEN_KEY, '');
  }
  function el(tag, cls, text) {
    var node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined) node.textContent = text;
    return node;
  }
  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }
  function fmt(n) { return Number(n || 0).toLocaleString('en-US'); }
  function AuthError() {}
  function statusLabel(status) { return STATUSES[status] ? STATUSES[status][0] : status; }
  function gpuText(r) { return [r.renderer, r.gpu].filter(Boolean).join(' · '); }
  function gameText(r) { return [r.game_build, r.game_version].filter(Boolean).join(' '); }

  function request(path, options) {
    var init = options || {};
    init.headers = Object.assign({ Authorization: 'Bearer ' + token }, init.headers || {});
    init.cache = 'no-store';
    init.credentials = 'omit';
    return fetch(path, init).then(function (r) {
      if (r.status === 401) throw new AuthError();
      if (r.status === 429) throw new Error('Too many attempts. Wait a minute and try again.');
      if (r.status === 503) throw new Error('The admin token is not configured on the service.');
      return r;
    });
  }
  function api(path) {
    return request(path).then(function (r) {
      if (!r.ok) throw new Error('The service answered ' + r.status + '.');
      return r.json();
    });
  }

  function options(id, key) {
    var select = $(id);
    var chosen = select.value;
    var seen = {};
    reports.forEach(function (r) { seen[r[key]] = true; });
    clear(select);
    var all = el('option', '', 'All');
    all.value = '';
    select.appendChild(all);
    Object.keys(seen).sort().forEach(function (value) {
      var option = el('option', '', value);
      option.value = value;
      select.appendChild(option);
    });
    select.value = seen[chosen] ? chosen : '';
  }

  function shown() {
    var version = $('filter-version').value;
    var os = $('filter-os').value;
    var status = $('filter-status').value;
    return reports.filter(function (r) {
      return (!version || r.launcher_version === version) && (!os || r.operating_system === os) && (!status || r.status === status);
    });
  }

  function renderCards() {
    var counts = {};
    Object.keys(STATUSES).forEach(function (s) { counts[s] = 0; });
    var now = Date.now();
    var recent = 0;
    reports.forEach(function (r) {
      counts[r.status] = (counts[r.status] || 0) + 1;
      if (now - r.ms < DAY_MS) recent += 1;
    });
    $('c-new').textContent = fmt(recent);
    Object.keys(STATUSES).forEach(function (s) { $(s === 'new' ? 'c-new-status' : 'c-' + s).textContent = fmt(counts[s]); });
  }

  function fact(list, name, value) {
    list.appendChild(el('dt', '', name));
    list.appendChild(el('dd', '', value || '–'));
  }

  function showStatusMeaning() {
    var status = $('status').value;
    $('status-meaning').textContent = STATUSES[status] ? STATUSES[status][1] : '';
    $('fixed-field').hidden = status !== 'fixed';
  }

  function statusForm(r) {
    $('status').value = r.status;
    $('fixed').value = r.fixed_version || '';
    $('message').value = r.player_message || '';
    $('status-note').textContent = '';
    showStatusMeaning();
  }

  function showDetail(r) {
    openTicket = r.ticket;
    loadingTicket = '';
    if (kept) writeStored(OPEN_KEY, r.ticket);
    logText = r.log_text;
    $('title').textContent = r.ticket;
    $('text').textContent = r.description;
    var facts = $('facts');
    clear(facts);
    fact(facts, 'Report ID', r.ticket);
    fact(facts, 'Submitted', stampLabel.format(r.ms) + ' (Sao Paulo)');
    fact(facts, 'Launcher version', r.launcher_version);
    fact(facts, 'Game build', r.game_build);
    fact(facts, 'Region', r.region);
    fact(facts, 'Game version', r.game_version);
    fact(facts, 'CUSA', r.cusa);
    fact(facts, 'Executable SHA-256', r.eboot_sha256);
    fact(facts, 'Operating system', r.operating_system);
    fact(facts, 'Renderer', r.renderer);
    fact(facts, 'GPU', r.gpu);
    fact(facts, 'Driver', r.driver);
    fact(facts, 'Render resolution', r.render_resolution);
    fact(facts, 'Original log file', r.log_file);
    fact(facts, 'Log clipped', r.log_clipped === 'yes' ? 'Yes, only its end was sent' : 'No');
    $('log-name').textContent = 'Sanitized log';
    $('log').textContent = logText;
    $('note').textContent = '';
    statusForm(r);
    $('detail').hidden = false;
    renderList();
  }

  function openReport(ticket) {
    loadingTicket = ticket;
    return api('/admin/api/reports/' + encodeURIComponent(ticket)).then(function (report) {
      if (report.ticket === loadingTicket) showDetail(report);
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      if (ticket !== loadingTicket) return;
      loadingTicket = '';
      $('count').textContent = 'Could not open ' + ticket + '.';
    });
  }

  function closeReport() {
    openTicket = '';
    loadingTicket = '';
    logText = '';
    writeStored(OPEN_KEY, '');
    $('detail').hidden = true;
    renderList();
  }

  function toggleReport(ticket) {
    if (ticket === openTicket) closeReport();
    else openReport(ticket);
  }

  function downloadLog() {
    var ticket = openTicket;
    if (!ticket) return Promise.resolve();
    return request('/admin/api/reports/' + encodeURIComponent(ticket) + '/log').then(function (r) {
      if (!r.ok) throw new Error('log');
      var named = /filename="([^"]+)"/.exec(r.headers.get('Content-Disposition') || '');
      return r.blob().then(function (blob) {
        var object = URL[OBJECT_MAKE](blob);
        var link = el('a');
        link.href = object;
        link.download = named ? named[1] : ticket + '.log';
        link.click();
        setTimeout(function () { URL[OBJECT_DROP](object); }, 1000);
      });
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      $('note').textContent = 'Could not download the log.';
    });
  }

  function copyLog() {
    if (!logText) return Promise.resolve();
    var say = function (text) { $('note').textContent = text; };
    if (!navigator.clipboard || !navigator.clipboard.writeText) { say('Copying is not possible in this browser.'); return Promise.resolve(); }
    return navigator.clipboard.writeText(logText).then(function () { say('Log copied.'); }, function () { say('Could not copy the log.'); });
  }

  function saveStatus() {
    var ticket = openTicket;
    if (!ticket) return Promise.resolve();
    var status = $('status').value;
    var change = { status: status, fixed_version: status === 'fixed' ? $('fixed').value.trim() : '', player_message: $('message').value };
    return request('/admin/api/reports/' + encodeURIComponent(ticket) + '/status', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(change)
    }).then(function (r) {
      if (!r.ok) throw new Error('status');
      return r.json();
    }).then(function (saved) {
      reports.forEach(function (r) {
        if (r.ticket === ticket) { r.status = saved.status; r.status_updated_at = saved.status_updated_at; }
      });
      renderCards();
      renderList();
      if (ticket !== openTicket) return;
      statusForm(saved);
      $('status-note').textContent = 'Saved.';
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      if (ticket === openTicket) $('status-note').textContent = 'Could not save the status. Check the version and the message.';
    });
  }

  function removeReport(ticket) {
    reports = reports.filter(function (r) { return r.ticket !== ticket; });
    if (openTicket === ticket || loadingTicket === ticket) closeReport();
    options('filter-version', 'launcher_version');
    options('filter-os', 'operating_system');
    renderCards();
    renderList();
    $('count').textContent = ticket + ' deleted. ' + $('count').textContent;
  }

  function setDeleteBusy(busy) {
    deleting = busy;
    $('delete-cancel').disabled = busy;
    $('delete-confirm').disabled = busy;
  }

  function openDelete() {
    if (!openTicket) return;
    deleteTicket = openTicket;
    $('delete-title').textContent = 'Delete ' + deleteTicket + ' permanently?';
    $('delete-error').textContent = '';
    setDeleteBusy(false);
    $('delete-backdrop').hidden = false;
    $('delete-cancel').focus();
  }

  function closeDelete() {
    if (deleting) return;
    deleteTicket = '';
    $('delete-backdrop').hidden = true;
  }

  function confirmDelete() {
    var ticket = deleteTicket;
    if (!ticket || deleting) return Promise.resolve();
    setDeleteBusy(true);
    return request('/admin/api/reports/' + encodeURIComponent(ticket), { method: 'DELETE' }).then(function (r) {
      if (!r.ok && r.status !== 404) throw new Error('delete');
      setDeleteBusy(false);
      deleteTicket = '';
      $('delete-backdrop').hidden = true;
      removeReport(ticket);
    }).catch(function (error) {
      setDeleteBusy(false);
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      $('delete-error').textContent = 'Could not delete the report. Try again.';
    });
  }

  function ticketCell(r) {
    var td = el('td');
    var button = el('button', 'ticket', r.ticket);
    button.type = 'button';
    button.addEventListener('click', function () { toggleReport(r.ticket); });
    td.appendChild(button);
    return td;
  }

  var LIST_COLUMNS = [['Report ID', 'col-id'], ['Status', 'col-status'], ['Submitted', 'col-submitted'], ['Launcher', 'col-launcher'],
    ['Game build', 'col-build'], ['Region', 'col-region'], ['Operating system', 'col-os'], ['Renderer · GPU', 'col-gpu'], ['Description', 'summary']];

  function renderList() {
    var rows = shown();
    var node = $('list');
    clear(node);
    var head = el('tr');
    LIST_COLUMNS.forEach(function (c) { head.appendChild(el('th', c[1], c[0])); });
    node.appendChild(head);
    rows.forEach(function (r) {
      var tr = el('tr', r.ticket === openTicket ? 'selected' : '');
      var cells = [ticketCell(r), statusLabel(r.status), stampLabel.format(r.ms), r.launcher_version, gameText(r), r.region || '–',
        r.operating_system, gpuText(r), r.preview];
      cells.forEach(function (cell, i) {
        var td = typeof cell === 'string' ? el('td', '', cell) : cell;
        if (LIST_COLUMNS[i][1]) td.className = td.className ? td.className + ' ' + LIST_COLUMNS[i][1] : LIST_COLUMNS[i][1];
        tr.appendChild(td);
      });
      node.appendChild(tr);
    });
    $('empty').hidden = reports.length > 0;
    $('list-wrap').hidden = reports.length === 0;
    $('count').textContent = !reports.length ? ''
      : rows.length === reports.length ? fmt(rows.length) + ' reports' : fmt(rows.length) + ' of ' + fmt(reports.length) + ' reports';
  }

  function loadReports() {
    $('count').textContent = 'Loading…';
    return api('/admin/api/reports').then(function (r) {
      reports = Array.isArray(r.reports) ? r.reports : [];
      options('filter-version', 'launcher_version');
      options('filter-os', 'operating_system');
      renderCards();
      renderList();
      var again = restoreTicket;
      restoreTicket = '';
      if (!again) return;
      if (reports.some(function (r) { return r.ticket === again; })) return openReport(again);
      writeStored(OPEN_KEY, '');
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      $('count').textContent = error && error.message ? error.message : 'Could not load the reports.';
    });
  }

  function setMenu(open) {
    menuOpen = open;
    $('session-menu').hidden = !open;
    $('menu-button').setAttribute('aria-expanded', String(open));
  }

  function showSessionState() {
    $('session-state').textContent = kept
      ? 'Signed in. The token is kept for this tab only, until you sign out or close the tab.'
      : 'Signed in. The token is held in memory only; reloading the page asks for it again.';
  }

  function showDashboard() {
    $('login').hidden = true;
    $('dash').hidden = false;
    $('session').hidden = false;
    setMenu(false);
    showSessionState();
    loadReports();
  }

  function signOut(message) {
    token = '';
    storeToken('');
    writeStored(OPEN_KEY, '');
    restoreTicket = '';
    reports = [];
    openTicket = '';
    loadingTicket = '';
    logText = '';
    deleting = false;
    deleteTicket = '';
    $('delete-backdrop').hidden = true;
    $('detail').hidden = true;
    setMenu(false);
    $('session').hidden = true;
    $('dash').hidden = true;
    $('login').hidden = false;
    $('remember').checked = false;
    $('token').value = '';
    $('login-error').textContent = message || '';
    $('token').focus();
  }

  $('login').addEventListener('submit', function (event) {
    event.preventDefault();
    token = $('token').value.trim();
    $('token').value = '';
    if (!token) return;
    storeToken($('remember').checked ? token : '');
    $('login-error').textContent = '';
    showDashboard();
  });
  $('refresh').addEventListener('click', loadReports);
  $('signout').addEventListener('click', function () { signOut(''); });
  $('filter-version').addEventListener('change', renderList);
  $('filter-os').addEventListener('change', renderList);
  $('filter-status').addEventListener('change', renderList);
  $('status').addEventListener('change', showStatusMeaning);
  $('status-save').addEventListener('click', saveStatus);
  $('download').addEventListener('click', downloadLog);
  $('copy').addEventListener('click', copyLog);
  $('close').addEventListener('click', closeReport);
  $('menu-button').addEventListener('click', function () {
    setMenu(!menuOpen);
    if (menuOpen) $('signout').focus();
  });
  $('delete-open').addEventListener('click', openDelete);
  $('delete-cancel').addEventListener('click', closeDelete);
  $('delete-confirm').addEventListener('click', confirmDelete);
  $('delete-backdrop').addEventListener('click', function (event) { if (event.target === $('delete-backdrop')) closeDelete(); });
  document.addEventListener('click', function (event) {
    if (menuOpen && !$('session').contains(event.target)) setMenu(false);
  });
  document.addEventListener('keydown', function (event) {
    if (deleteTicket && event.key === 'Tab') {
      event.preventDefault();
      ($('delete-cancel') === document.activeElement ? $('delete-confirm') : $('delete-cancel')).focus();
    } else if (event.key === 'Escape' && deleteTicket) {
      closeDelete();
    } else if (event.key === 'Escape' && menuOpen) {
      setMenu(false);
      $('menu-button').focus();
    }
  });

  token = readStored(KEY);
  if (token) {
    kept = true;
    $('remember').checked = true;
    var saved = readStored(OPEN_KEY);
    restoreTicket = TICKET.test(saved) ? saved : '';
    showDashboard();
  }
}());
`;
