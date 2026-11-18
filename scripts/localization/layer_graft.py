"""Keys of another language's layout drawn in this one.

Layouts of the same atlas share its sprites, so a key of a donor language (its sprite part, quad and colour) can be
appended to a layout and take the place of a key of the same layer. Used to draw a sprite the donor localizes (and the
Portuguese version of which is painted into the donor's atlas slot) where the English layout draws its English sprite.
"""
import struct

import layout_text as T
import mbs as M


class Graft:
    def __init__(self, layout):
        self.layout = layout
        self.index = {s: {bytes(layout.record(s, i)): i for i in range(layout.count(s))}
                      for s in (M.PARTS, M.GEOMS, M.COLOURS)}

    def bring(self, donor, section, i):
        raw = bytes(donor.record(section, i))
        found = self.index[section].get(raw)
        if found is None:
            found = self.layout.append(section, [raw, raw] if section == M.COLOURS else [raw])
            self.index[section][raw] = found
        return found

    def key(self, donor, k):
        raw = bytearray(donor.record(M.KEYS, k))
        part, colour, geom = struct.unpack_from('<HHH', raw, 10)
        struct.pack_into('<HHH', raw, 10, self.bring(donor, M.PARTS, part), self.bring(donor, M.COLOURS, colour),
                         self.bring(donor, M.GEOMS, geom))
        return bytes(raw)

    def set_keys(self, layer, records):
        layout = self.layout
        start = layout.append(M.KEYS, records)
        x0, y0, x1, y1 = struct.unpack_from('<4f', layout.record(M.LAYERS, layer))
        boxes = [T.quad_box(layout.geom(struct.unpack_from('<H', raw, 14)[0])) for raw in records]
        if boxes:
            x0, y0 = min(x0, *[b[0] for b in boxes]), min(y0, *[b[1] for b in boxes])
            x1, y1 = max(x1, *[b[2] for b in boxes]), max(y1, *[b[3] for b in boxes])
        layout.set_layer(layer, start, len(records), (x0, y0, x1, y1))

    def replace_layer(self, layer, donor, donor_layer):
        """The layer drawn exactly as the donor draws its layer."""
        first, count = donor.layer_range(donor_layer)
        self.set_keys(layer, [self.key(donor, k) for k in range(first, min(first + count, donor.count(M.KEYS)))])

    def swap_sprite(self, layer, donor, donor_layer, english, slot):
        """The keys of the layer that draw the `english` sprite take the donor's keys drawing `slot`, in order."""
        layout = self.layout
        first, count = layout.layer_range(layer)
        mine = [k for k in range(first, first + count) if layout.key(k)['part'] < layout.count(M.PARTS)
                and (layout.key(k)['texture'],) + layout.part_rect(layout.key(k)['part']) == english]
        d_first, d_count = donor.layer_range(donor_layer)
        theirs = [k for k in range(d_first, d_first + d_count) if donor.key(k)['part'] < donor.count(M.PARTS)
                  and (donor.key(k)['texture'],) + donor.part_rect(donor.key(k)['part']) == slot]
        if len(mine) != len(theirs):
            raise ValueError(f'layer {layer}: {len(mine)} keys draw {english}, the donor draws {slot} {len(theirs)} times')
        swap = dict(zip(mine, (self.key(donor, k) for k in theirs)))
        self.set_keys(layer, [swap.get(k) or bytes(layout.record(M.KEYS, k)) for k in range(first, first + count)])
        return len(swap)


    def part_for(self, part, english, slot):
        """A copy of the part of `english` that shows the `slot` rectangle of the atlas."""
        values = list(struct.unpack('<12f', self.layout.record(M.PARTS, part)))
        for i in range(1, 6):
            x, y = values[2 * i], values[2 * i + 1]
            values[2 * i] = slot[1] if x <= english[1] else slot[3]
            values[2 * i + 1] = slot[2] if y <= english[2] else slot[4]
        values[0], values[1] = (slot[1] + slot[3]) / 2, (slot[2] + slot[4]) / 2
        record = struct.pack('<12f', *values)
        found = self.index[M.PARTS].get(record)
        if found is None:
            found = self.layout.append(M.PARTS, [record])
            self.index[M.PARTS][record] = found
        return found

    def retarget_sprite(self, layer, english, slot):
        """The keys of the layer drawing the `english` sprite draw the donor's `slot` sprite instead, on their own
        quad resized about its centre in the proportion of the two sprites' sizes (the letters keep their scale)."""
        layout = self.layout
        factor = (slot[3] - slot[1]) / (english[3] - english[1]), (slot[4] - slot[2]) / (english[4] - english[2])
        first, count = layout.layer_range(layer)
        records, done = [], 0
        for k in range(first, first + count):
            key = layout.key(k)
            raw = bytearray(layout.record(M.KEYS, k))
            if key['part'] < layout.count(M.PARTS) and (key['texture'],) + layout.part_rect(key['part']) == english:
                quad = layout.geom(key['geom'])
                cx, cy = sum(p[0] for p in quad) / 4, sum(p[1] for p in quad) / 4
                wide = [(cx + (x - cx) * factor[0], cy + (y - cy) * factor[1]) for x, y in quad]
                box = T.quad_box(wide)
                flat = [v for c in wide for v in c]
                record = struct.pack('<12f', (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, *flat, *wide[0])
                geom = self.index[M.GEOMS].get(record)
                if geom is None:
                    geom = layout.append(M.GEOMS, [record])
                    self.index[M.GEOMS][record] = geom
                struct.pack_into('<H', raw, 6, (slot[0] << 8) | (struct.unpack_from('<H', raw, 6)[0] & 0xff))
                struct.pack_into('<H', raw, 10, self.part_for(key['part'], english, slot))
                struct.pack_into('<H', raw, 14, geom)
                done += 1
            records.append(bytes(raw))
        if not done:
            raise ValueError(f'layer {layer} does not draw {english}')
        self.set_keys(layer, records)
        return done


def layers_drawing(layout, rect):
    out = []
    for layer in range(layout.count(M.LAYERS)):
        first, count = layout.layer_range(layer)
        for k in range(first, min(first + count, layout.count(M.KEYS))):
            key = layout.key(k)
            if key['part'] < layout.count(M.PARTS) and (key['texture'],) + layout.part_rect(key['part']) == rect:
                out.append(layer)
                break
    return out
