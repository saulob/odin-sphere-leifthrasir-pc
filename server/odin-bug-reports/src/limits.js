// In-memory abuse limits. Nothing here is written to SQL: a limit that cost a write per hit would defeat its purpose, and
// an address held only in memory is gone when the isolate or the object is.
export const PROTECTION = {
  submitAll: { burst: 40, perSecond: 1 / 3 },
  submitAddress: { burst: 6, perSecond: 1 / 60 },
  lookupAll: { burst: 200, perSecond: 2 },
  statusAddress: { burst: 20, perSecond: 1 / 15 },
  readAddress: { burst: 10, perSecond: 1 / 30 },
  adminFailures: { burst: 10, perSecond: 1 / 30 },
  trackedKeys: 4096,
};

export class RateLimiter {
  constructor(spec, maxKeys = PROTECTION.trackedKeys) {
    this.spec = spec;
    this.maxKeys = maxKeys;
    this.buckets = new Map();
  }

  tokens(key, now) {
    const [tokens, last] = this.buckets.get(key) ?? [this.spec.burst, now];
    return Math.min(this.spec.burst, tokens + (Math.max(0, now - last) / 1000) * this.spec.perSecond);
  }

  allows(key, now) {
    return this.tokens(key, now) >= 1;
  }

  take(key, now) {
    const tokens = this.tokens(key, now);
    if (tokens < 1) return false;
    this.buckets.delete(key);
    if (this.buckets.size >= this.maxKeys) this.prune(now);
    this.buckets.set(key, [tokens - 1, now]);
    return true;
  }

  // Idle keys are as good as new ones; if every key is busy the oldest go.
  prune(now) {
    for (const key of [...this.buckets.keys()]) {
      if (this.tokens(key, now) >= this.spec.burst) this.buckets.delete(key);
    }
    for (const key of this.buckets.keys()) {
      if (this.buckets.size < this.maxKeys) break;
      this.buckets.delete(key);
    }
  }
}

// Per-address limits group IPv6 clients by their /64, whatever the spelling.
export function ipGroup(ip) {
  if (typeof ip !== 'string' || !ip.includes(':')) return String(ip);
  const mapped = /^::ffff:(\d{1,3}(?:\.\d{1,3}){3})$/i.exec(ip);
  if (mapped) return mapped[1];
  const [head, tail] = ip.split('::');
  const before = head ? head.split(':') : [];
  const after = tail ? tail.split(':') : [];
  const gap = tail === undefined ? [] : Array(Math.max(0, 8 - before.length - after.length)).fill('0');
  return [...before, ...gap, ...after].slice(0, 4).map((g) => g.toLowerCase().replace(/^0+(?=.)/, '')).join(':');
}
