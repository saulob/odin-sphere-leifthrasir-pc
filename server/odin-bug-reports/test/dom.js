// Runs the dashboard script against a minimal DOM and a canned API, so the tests read what it renders.
import vm from 'node:vm';

import { handleAdmin, resetAdminLimits } from '../src/admin.js';
import { ADMIN_HTML, ADMIN_JS } from '../src/admin_page.js';
import { ENV } from './harness.js';

class FakeNode {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.className = '';
    this.attrs = {};
    this.listeners = {};
    this.hidden = false;
    this.disabled = false;
    this.value = '';
    this.checked = false;
    this.own = '';
    this.focused = false;
  }

  get textContent() {
    return this.children.length ? this.children.map((c) => c.textContent).join('') : this.own;
  }

  set textContent(value) {
    this.children = [];
    this.own = String(value);
  }

  get firstChild() {
    return this.children[0] ?? null;
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }

  removeChild(child) {
    this.children = this.children.filter((c) => c !== child);
  }

  setAttribute(name, value) {
    this.attrs[name] = value;
  }

  addEventListener(type, fn) {
    (this.listeners[type] ??= []).push(fn);
  }

  fire(type, event = {}) {
    for (const fn of this.listeners[type] ?? []) fn({ preventDefault() {}, target: this, ...event });
  }

  click() {
    this.fire('click');
  }

  contains(node) {
    return node === this || this.children.some((c) => c.contains?.(node));
  }

  focus() {
    this.focused = true;
  }

  find(predicate, found = []) {
    if (predicate(this)) found.push(this);
    for (const child of this.children) child.find(predicate, found);
    return found;
  }

  rows() {
    return this.find((n) => n.tag === 'tr').map((tr) => tr.children.map((c) => c.textContent));
  }
}

export const settle = async () => {
  for (let i = 0; i < 30; i++) await new Promise((resolve) => setImmediate(resolve));
};

// respond(path, init) answers like fetch, as a value or a promise: { status, body, headers }.
export async function openDashboard(respond, { signIn = true, now = Date.now(), stored = {}, remember = false } = {}) {
  const nodes = new Map();
  for (const tag of ADMIN_HTML.matchAll(/<[a-z0-9]+\s[^>]*>/g)) {
    const id = /\sid="([^"]+)"/.exec(tag[0]);
    if (id) Object.assign(nodes.set(id[1], new FakeNode('div')).get(id[1]), { hidden: /\shidden(\s|>)/.test(tag[0]) });
  }
  const storage = new Map(Object.entries(stored));
  const documentListeners = {};
  const requests = [];
  const downloads = [];
  const clipboard = [];
  const sandbox = {
    document: {
      activeElement: null,
      addEventListener: (type, fn) => { (documentListeners[type] ??= []).push(fn); },
      getElementById: (id) => nodes.get(id) ?? null,
      createElement: (tag) => {
        const node = new FakeNode(tag);
        if (tag === 'a') node.click = () => downloads.push({ href: node.href, download: node.download });
        return node;
      },
    },
    navigator: { clipboard: { writeText: async (text) => { clipboard.push(text); } } },
    sessionStorage: {
      getItem: (key) => storage.get(key) ?? null,
      setItem: (key, value) => { storage.set(key, String(value)); },
      removeItem: (key) => { storage.delete(key); },
    },
    URL: { createObjectURL: () => 'blob:test', revokeObjectURL() {} },
    setTimeout: () => 1,
    fetch: async (path, init = {}) => {
      requests.push({ path, init });
      const { status = 200, body = {}, headers = {} } = await respond(path, init);
      const text = typeof body === 'string' ? body : JSON.stringify(body);
      return {
        status, ok: status >= 200 && status < 300, json: async () => JSON.parse(text), text: async () => text,
        blob: async () => text, headers: { get: (name) => headers[name.toLowerCase()] ?? null },
      };
    },
    Intl, Math, Date: class extends Date { static now() { return now; } }, Number, JSON, Promise, Error, Object, Array,
  };
  vm.runInNewContext(ADMIN_JS, sandbox);
  if (signIn) {
    nodes.get('token').value = 'admin-token-0123456789-abcdefgh';
    nodes.get('remember').checked = remember;
    for (const fn of nodes.get('login').listeners.submit) fn({ preventDefault() {} });
  }
  await settle();
  return {
    document: sandbox.document, nodes, requests, downloads, clipboard, storage,
    press: (key) => { for (const fn of documentListeners.keydown ?? []) fn({ key, preventDefault() {} }); },
    clickAnywhere: (target) => { for (const fn of documentListeners.click ?? []) fn({ target }); },
    text: (id) => nodes.get(id).textContent,
    rows: (id) => nodes.get(id).rows(),
    all: () => [...nodes.values()].map((n) => n.textContent).join('\n'),
  };
}

// The dashboard script talks to the real admin handler, so what it renders is what the service answers.
export async function dashboardFor(env, { now, answer, ...options } = {}) {
  resetAdminLimits();
  return openDashboard(async (path, init = {}) => {
    const override = await answer?.(path, init);
    if (override) return override;
    const response = await handleAdmin(new Request(`https://reports.example${path}`, init), ENV, env.desk, () => {});
    return { status: response.status, body: await response.text(), headers: Object.fromEntries(response.headers) };
  }, { now, ...options });
}
