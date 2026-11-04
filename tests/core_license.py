"""C8 许可证边界扫描：core/ 只能是 MIT 来源，且不得混入 GPL 或 C 的私有类型。"""
import pathlib
import sys

core = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'core')
license_file = core / 'LICENSE'
if not license_file.is_file():
    print('core/LICENSE missing')
    sys.exit(1)
license_text = license_file.read_text(encoding='utf-8', errors='replace')
if 'MIT License' not in license_text:
    print('core/LICENSE is not MIT')
    sys.exit(1)

forbidden = (
    'GNU General Public License',
    'GPL-3',
    'GPLv3',
    'riverfinder_native',
    'drowned-weighted-area',
)
failures = []
for path in sorted(core.rglob('*')):
    if not path.is_file() or path.name == 'LICENSE':
        continue
    if path.suffix not in {'.hpp', '.h', '.cpp', '.c', '.md'}:
        continue
    text = path.read_text(encoding='utf-8', errors='replace')
    for token in forbidden:
        if token in text:
            failures.append(f'{path.relative_to(core)}: {token}')
if failures:
    print('core/ license boundary violated:')
    for item in failures:
        print(' ', item)
    sys.exit(2)
print(f'core/ license scan passed ({len(list(core.rglob("*")))} entries, MIT only)')
