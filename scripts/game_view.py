"""Merged, read-only-by-convention view of a base dump with an update dump laid over it.

Files are hard links into the dumps, so a multi-gigabyte game costs only directory entries and
neither dump is modified. Names are matched without regard to case, as on the console.
"""
import json
import os
from pathlib import Path
import shutil

MARKER = '.osl_view.json'
SKIP_NAMES = ('.pkg_extracted',)
SKIP_SUFFIXES = ('.part',)


def _link(source, dest):
    try:
        os.link(source, dest)
        return 'link'
    except OSError:
        shutil.copy2(source, dest)
        return 'copy'


def _lay(source, dest, stats):
    """Lays the files of `source` over `dest` (a real folder): files replace, folders merge."""
    existing = {entry.name.casefold(): entry for entry in dest.iterdir()}
    for item in sorted(source.iterdir()):
        if item.name in SKIP_NAMES or item.name.endswith(SKIP_SUFFIXES):
            continue
        match = existing.get(item.name.casefold())
        if item.is_dir():
            target = match if match is not None else dest / item.name
            target.mkdir(exist_ok=True)
            _lay(item, target, stats)
            continue
        if match is not None:
            match.unlink()
            stats['replaced'] += 1
        stats[_link(item, dest / item.name)] += 1


def is_view(path):
    return (Path(path) / MARKER).is_file()


def remove_view(path):
    """Deletes a view made by build_view; refuses any other folder."""
    path = Path(path)
    if not path.exists():
        return
    if not is_view(path):
        raise ValueError(f'{path} is not a game view (no {MARKER}); refusing to delete it')
    shutil.rmtree(path)


def build_view(layers, out, description=None):
    """Creates `out` from the dump folders in `layers` (base first). Returns the link statistics."""
    out = Path(out)
    remove_view(out)
    out.mkdir(parents=True)
    stats = {'link': 0, 'copy': 0, 'replaced': 0}
    try:
        for layer in layers:
            _lay(Path(layer), out, stats)
        (out / MARKER).write_text(json.dumps({'layers': [str(Path(l).resolve()) for l in layers],
                                              'description': description, **stats}, indent=2))
    except BaseException:
        shutil.rmtree(out, ignore_errors=True)
        raise
    return stats
