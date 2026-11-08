"""Recursive host exports, preflight conflicts, staged data, and overwrite rules."""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile

exe = sys.argv[1]
def run(*args, data=None, code=0):
    result = subprocess.run([exe, *map(str, args)], input=data, text=True, capture_output=True)
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result

def tree(path):
    return {str(p.relative_to(path)): None if p.is_dir() else p.read_bytes() for p in path.rglob('*')}

with tempfile.TemporaryDirectory(prefix='zdt-export-') as temp:
    root = pathlib.Path(temp)
    image, source = root/'disk.img', root/'payload.bin'
    payload = bytes(range(256)) * 300
    source.write_bytes(payload)
    run('image', 'create', image, '--size', '1MiB', '--yes')
    run('-i', image, 'partition', 'create', '--yes')
    run('-i', image, 'shell', data=f'mkdir apps\nmkdir apps/empty\nmkdir private\nimport "{source}" apps/game\nimport "{source}" root.bin\napply\nquit\n')
    original = hashlib.sha256(image.read_bytes()).digest()
    destination = root/'nested'/'export-ζ'
    run('-i', image, 'export', '/', destination, '--dry-run')
    assert not destination.exists() and not destination.parent.exists()
    result = run('-i', image, 'export', '/', destination, '--json')
    assert json.loads(result.stdout)['ok']
    expected = {'apps': None, 'apps/empty': None, 'apps/game': payload, 'private': None, 'root.bin': payload}
    assert tree(destination) == expected
    # Existing unrelated host files are retained. Preflight prevents any writes
    # or new directories when overwrite approval is missing.
    (destination/'personal').write_bytes(b'host personal')
    (destination/'apps'/'game').write_bytes(b'old host contents')
    (destination/'private').rmdir()
    snapshot = tree(destination)
    run('-i', image, 'export', '/', destination, code=3)
    assert tree(destination) == snapshot
    run('-i', image, 'export', '/', destination, '--yes')
    assert tree(destination) == dict(expected, personal=b'host personal')
    # A file/directory type mismatch aborts preflight even with --yes.
    (destination/'root.bin').unlink()
    (destination/'root.bin').mkdir()
    (destination/'apps'/'game').write_bytes(b'must stay old')
    snapshot = tree(destination)
    run('-i', image, 'export', '/', destination, '--yes', code=1)
    assert tree(destination) == snapshot
    # Export a subdirectory's contents, including empty directories, and staged data.
    sub = root/'sub'
    run('-i', image, 'shell', data=f'import "{source}" apps/staged\nexport apps "{sub}"\ndiscard\nquit\n')
    assert tree(sub) == {'empty': None, 'game': payload, 'staged': payload}
    assert hashlib.sha256(image.read_bytes()).digest() == original
    empty = root/'empty'
    run('-i', image, 'export', 'apps/empty', empty)
    assert empty.is_dir() and not list(empty.iterdir())
    # Export must never replace the active image, including within a tree.
    protected = root/'protected'
    protected.mkdir()
    os.link(image, protected/'root.bin')
    run('-i', image, 'export', '/', protected, '--yes', code=1)
    assert not (protected/'apps').exists()
    assert hashlib.sha256(image.read_bytes()).digest() == original
    # Links inside an export destination must not redirect writes outside it.
    linked = root/'linked'
    linked.mkdir()
    outside = root/'outside'
    outside.mkdir()
    try:
        (linked/'apps').symlink_to(outside, target_is_directory=True)
    except OSError:
        pass  # Windows may require developer mode or elevation for symlinks.
    else:
        run('-i', image, 'export', '/', linked, '--yes', code=1)
        assert not list(outside.iterdir()) and not (linked/'root.bin').exists()
    if os.name != 'nt':
        # Traverse a deep tree without allocating full path buffers per level.
        components = ['x'] * 200
        paths = ['/'.join(components[:i]) for i in range(1, len(components) + 1)]
        deep = root/'deep'
        script = ''.join(f'mkdir {path}\n' for path in paths)
        script += f'import "{source}" {paths[-1]}/leaf\nexport x "{deep}"\ndiscard\nquit\n'
        run('-i', image, 'shell', data=script)
        assert deep.joinpath(*components[1:], 'leaf').read_bytes() == payload
    run('-i', image, 'export', 'missing', root/'missing', code=1)
    assert not (root/'missing').exists()
print('Recursive export: contents, empty directories, staged data, preflight, overwrite and alias protection passed')
