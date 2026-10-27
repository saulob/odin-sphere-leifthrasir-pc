#!/usr/bin/env python3
"""A BC3 (DXT5) encoder for UI art, vectorised over blocks with numpy: interpolated alpha
(8-level, min/max endpoints) and a 4-colour block fitted along the alpha-weighted principal axis,
then refined by least squares.

    encode_blocks(blocks) -> bytes     blocks: uint8 array (N, 16, 4), pixels in row order
"""
import numpy as np

COLOR_T = np.array([0.0, 1.0, 1 / 3, 2 / 3])  # position of each colour index between c0 and c1


def _alpha_palette(a0, a1):
    """The eight values a BC4 block decodes to: interpolated when a0 > a1, else six plus 0 and 255."""
    eight = (a0[:, None] * np.array([7, 0, 6, 5, 4, 3, 2, 1]) + a1[:, None] * np.array([0, 7, 1, 2, 3, 4, 5, 6])) // 7
    six = (a0[:, None] * np.array([5, 0, 4, 3, 2, 1, 0, 0]) + a1[:, None] * np.array([0, 5, 1, 2, 3, 4, 0, 0])) // 5
    six[:, 6], six[:, 7] = 0, 255
    return np.where((a0 > a1)[:, None], eight, six)


def _alpha(a):
    """a: (N,16) int. Returns (N,8) uint8 BC4 blocks: the better of the 8-value and the 6-value-plus-0/255 modes."""
    inner = np.where((a > 0) & (a < 255), a, -1)
    ihi = inner.max(axis=1)
    ilo = np.where(inner >= 0, inner, 256).min(axis=1)
    ihi, ilo = np.where(ihi < 0, 0, ihi), np.where(ilo > 255, 0, ilo)
    best = None
    for a0, a1 in ((a.max(axis=1), a.min(axis=1)), (np.minimum(ilo, ihi), np.maximum(ilo, ihi))):
        pal = _alpha_palette(a0, a1)
        d = np.abs(a[:, :, None] - pal[:, None, :])
        idx = d.argmin(axis=2)
        err = (np.take_along_axis(d, idx[..., None], 2)[..., 0] ** 2).sum(axis=1)
        if best is None:
            best = (a0, a1, idx, err)
        else:
            take = err < best[3]
            best = (np.where(take, a0, best[0]), np.where(take, a1, best[1]),
                    np.where(take[:, None], idx, best[2]), np.where(take, err, best[3]))
    a0, a1, idx, _ = best
    bits = np.zeros(len(a), dtype=np.uint64)
    for i in range(16):
        bits |= idx[:, i].astype(np.uint64) << np.uint64(3 * i)
    out = np.zeros((len(a), 8), dtype=np.uint8)
    out[:, 0], out[:, 1] = a0, a1
    for j in range(6):
        out[:, 2 + j] = (bits >> np.uint64(8 * j)) & np.uint64(0xFF)
    return out


def _to565(c):
    c = np.clip(np.rint(c), 0, 255).astype(np.int64)
    return ((c[..., 0] * 31 + 127) // 255) << 11 | ((c[..., 1] * 63 + 127) // 255) << 5 | ((c[..., 2] * 31 + 127) // 255)


def _from565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=-1).astype(np.float64)


def _palette(q0, q1):
    e0, e1 = _from565(q0), _from565(q1)
    return np.stack([e0, e1, (2 * e0 + e1) // 3, (e0 + 2 * e1) // 3], axis=1)  # (N,4,3)


def _assign(px, w, q0, q1):
    pal = _palette(q0, q1)
    d = ((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)
    idx = d.argmin(axis=2)
    err = (np.take_along_axis(d, idx[..., None], 2)[..., 0] * w).sum(axis=1)
    return idx, err


def _fit_axis(px, w):
    ws = w.sum(axis=1, keepdims=True)
    mean = (px * w[..., None]).sum(axis=1, keepdims=True) / ws[..., None]
    d = px - mean
    cov = np.einsum('ni,nia,nib->nab', w, d, d)
    v = np.tile(np.array([0.577, 0.577, 0.577]), (len(px), 1))
    for _ in range(8):
        v = np.einsum('nab,nb->na', cov, v)
        n = np.linalg.norm(v, axis=1, keepdims=True)
        v = np.where(n > 1e-9, v / np.maximum(n, 1e-9), np.array([[0.577, 0.577, 0.577]]))
    t = np.einsum('nia,na->ni', d, v)
    big = 1e9
    tmin = np.where(w > 0.02, t, big).min(axis=1)
    tmax = np.where(w > 0.02, t, -big).max(axis=1)
    tmin = np.where(tmin == big, 0, tmin); tmax = np.where(tmax == -big, 0, tmax)
    return mean[:, 0] + v * tmin[:, None], mean[:, 0] + v * tmax[:, None]


def _refit(px, w, idx):
    """Least-squares endpoints for fixed indices; None rows keep the old ones."""
    t = COLOR_T[idx]
    a, b = 1 - t, t
    aa, bb, ab = (w * a * a).sum(1), (w * b * b).sum(1), (w * a * b).sum(1)
    ax = (w[..., None] * a[..., None] * px).sum(1)
    bx = (w[..., None] * b[..., None] * px).sum(1)
    det = aa * bb - ab * ab
    ok = np.abs(det) > 1e-6
    det = np.where(ok, det, 1)
    e0 = (bb[:, None] * ax - ab[:, None] * bx) / det[:, None]
    e1 = (aa[:, None] * bx - ab[:, None] * ax) / det[:, None]
    return e0, e1, ok


def _color(px, alpha):
    w = alpha / 255.0 + 1e-3
    e0, e1 = _fit_axis(px, w)
    q0, q1 = _to565(e1), _to565(e0)
    idx, err = _assign(px, w, q0, q1)
    for _ in range(3):
        f0, f1, ok = _refit(px, w, idx)
        n0, n1 = np.where(ok, _to565(f0), q0), np.where(ok, _to565(f1), q1)
        nidx, nerr = _assign(px, w, n0, n1)
        better = nerr < err
        q0, q1 = np.where(better, n0, q0), np.where(better, n1, q1)
        idx = np.where(better[:, None], nidx, idx); err = np.where(better, nerr, err)
    # c0 > c1 keeps the block in four-colour mode for any decoder; swapping exchanges indices 0<->1 and 2<->3
    swap = q0 < q1
    q0, q1 = np.where(swap, q1, q0), np.where(swap, q0, q1)
    idx = np.where(swap[:, None], idx ^ 1, idx)
    idx[q0 == q1] = 0
    bits = np.zeros(len(px), dtype=np.uint64)
    for i in range(16):
        bits |= idx[:, i].astype(np.uint64) << np.uint64(2 * i)
    out = np.zeros((len(px), 8), dtype=np.uint8)
    out[:, 0], out[:, 1], out[:, 2], out[:, 3] = q0 & 0xFF, q0 >> 8, q1 & 0xFF, q1 >> 8
    for j in range(4):
        out[:, 4 + j] = (bits >> np.uint64(8 * j)) & np.uint64(0xFF)
    return out


def encode_blocks(blocks):
    blocks = np.asarray(blocks)
    a = blocks[..., 3].astype(np.int64)
    px = blocks[..., :3].astype(np.float64)
    return np.concatenate([_alpha(a), _color(px, a)], axis=1).tobytes()
