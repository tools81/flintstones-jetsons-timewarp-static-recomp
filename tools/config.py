"""Per-game configuration (game.json in the project root).

{
  "title":    window title / messages,
  "exe_name": name of the built executable,
  "update_repo": GitHub "owner/name" whose releases the program checks for
              updates (optional),
  "modules":  program modules to recompile.  The first one is the main
              program the runtime starts.  Each entry:
      "file":          module file name on the disc, or "auto" for the
                       application named in the disc label (main program)
      "base":          fixed load address (hex string).  Every module needs
                       its own non-overlapping range below 0x140000 (the heap
                       starts there); the runtime loads it at this address.
      "extra_entries": code addresses the analyzer cannot find on its own
                       (hex strings, absolute, i.e. base + offset)
      "a5_table":      true / false / "auto": the program uses an MPW-style
                       A5 jump table ("jsr N(a5)" calls)
      "data_module":   file holding the program's initialized data, for
                       programs that load it themselves (F$Load "<name>_data")
                       instead of carrying idata.  "auto" uses
                       extracted/<name>_data if it exists; null for none.
                       Its "jmp abs.l" stubs resolve "jsr N(a6)" calls.
      "symbols":       Microware linker symbol table module (<name>.stb) for
                       function names and entry points; "auto" or null.
}
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _hex(v):
    return int(v, 16) if isinstance(v, str) else int(v)


def load():
    with open(os.path.join(ROOT, 'game.json')) as f:
        cfg = json.load(f)
    cfg.setdefault('title', 'CD-i Game')
    cfg.setdefault('exe_name', 'cdigame')
    cfg.setdefault('update_repo', '')
    mods = []
    for m in cfg.get('modules', []):
        mods.append({
            'file': m.get('file', 'auto'),
            'base': _hex(m.get('base', '0x100000')),
            'extra_entries': [_hex(x) for x in m.get('extra_entries', [])],
            'a5_table': m.get('a5_table', 'auto'),
            'data_module': m.get('data_module', 'auto'),
            'symbols': m.get('symbols', 'auto'),
        })
    if not mods:
        mods.append({'file': 'auto', 'base': 0x100000, 'extra_entries': [], 'a5_table': 'auto',
                     'data_module': 'auto', 'symbols': 'auto'})
    cfg['modules'] = mods
    return cfg


def app_name():
    """main program name recorded by tools/cdi_extract.py from the disc label"""
    p = os.path.join(ROOT, 'extracted', '_app.txt')
    if os.path.exists(p):
        return open(p).read().strip()
    return None


def module_file(m):
    f = m['file']
    if f == 'auto':
        f = app_name()
        if not f:
            raise SystemExit('cannot determine the main program: run tools/cdi_extract.py first')
    return f


def _companion(m, key, suffix):
    """path of a module's companion file (data module / symbol table) or None"""
    v = m.get(key, 'auto')
    if not v:
        return None
    if v == 'auto':
        v = module_file(m) + suffix
    p = os.path.join(ROOT, 'extracted', v)
    return p if os.path.exists(p) else None


def data_module_path(m):
    return _companion(m, 'data_module', '_data')


def symbols_path(m):
    return _companion(m, 'symbols', '.stb')
