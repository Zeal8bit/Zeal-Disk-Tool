"""Bulk transfers, directory destinations, and atomic staged import failures."""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile

exe = str(pathlib.Path(sys.argv[1]).resolve())


def run(*args, code=0, data=None, cwd=None):
    result = subprocess.run([exe, *map(str, args)], input=data, text=True,
                            capture_output=True, cwd=cwd)
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


with tempfile.TemporaryDirectory(prefix='zdt-bulk-') as temporary:
    root = pathlib.Path(temporary)
    host = root / 'host files'
    host.mkdir()
    files = {'game.bin': b'program', 'other.bin': b'second program',
             'assets.zar': b'assets', 'file.txt': b'text', 'space name.bin': b'spaces',
             'π.bin': b'unicode name'}
    for name, contents in files.items():
        (host / name).write_bytes(contents)
    # A matching directory must be ignored; patterns only transfer regular files.
    (host / 'dir.bin').mkdir()
    (host / '.hidden.bin').write_bytes(b'hidden')
    image = root / 'disk.img'
    run('image', 'create', image, '--size', '1MiB', '--yes')
    run('-i', image, 'partition', 'create', '--yes')
    script = '\n'.join(['mkdir games', 'import *.bin *.zar games/',
                        'import file.txt /games', 'ls games', 'apply', 'quit', ''])
    result = run('-i', image, 'shell', data=script, cwd=host)
    assert 'game.bin' in result.stdout and 'assets.zar' in result.stdout
    listing = json.loads(run('-i', image, 'ls', 'games', '--json').stdout)['data']
    assert {entry['name'] for entry in listing} == set(files)
    exported = root / 'all'
    run('-i', image, 'export', 'games', exported)
    assert {p.name: p.read_bytes() for p in exported.iterdir()} == files

    # Wildcard exports select only matching files and create a host directory.
    subset = root / 'subset'
    run('-i', image, 'export', '/games/*.bin', subset, '--dry-run')
    assert not subset.exists()
    run('-i', image, 'export', 'games/*.bin', subset)
    bins = {name: data for name, data in files.items() if name.endswith('.bin')}
    assert {p.name: p.read_bytes() for p in subset.iterdir()} == bins
    (subset / 'game.bin').write_bytes(b'keep until approved')
    (subset / 'other.bin').unlink()
    run('-i', image, 'export', 'games/*.bin', subset, code=3)
    assert not (subset / 'other.bin').exists()
    assert (subset / 'game.bin').read_bytes() == b'keep until approved'
    run('-i', image, 'export', 'games/*.bin', subset, '--yes')
    assert {p.name: p.read_bytes() for p in subset.iterdir()} == bins
    no_match = root / 'no match'
    run('-i', image, 'export', 'games/*.missing', no_match, code=1)
    assert not no_match.exists()
    run('-i', image, 'export', 'ga*/*.bin', no_match, code=1)
    assert not no_match.exists()

    # Single files also retain their basename for existing directories or a
    # trailing host separator, while explicit renaming continues to work.
    run('-i', image, 'export', 'games/file.txt', subset)
    assert (subset / 'file.txt').read_bytes() == files['file.txt']
    new_folder = root / 'new folder'
    run('-i', image, 'export', 'games/file.txt', str(new_folder) + '/')
    assert (new_folder / 'file.txt').read_bytes() == files['file.txt']
    run('-i', image, 'export', 'games/file.txt', root / 'renamed.txt')
    assert (root / 'renamed.txt').read_bytes() == files['file.txt']

    # '?' matches one Unicode character, and '/' is a directory destination.
    run('-i', image, 'import', '?.bin', '/', '--yes', cwd=host)
    one_character = root / 'one character'
    run('-i', image, 'export', '/?.bin', one_character)
    assert {p.name: p.read_bytes() for p in one_character.iterdir()} == {'π.bin': files['π.bin']}

    # Imports see a directory even without a trailing slash, and preserve
    # one-shot confirmation/dry-run rules for the entire batch.
    run('-i', image, 'mkdir', 'copy', '--yes')
    original = digest(image)
    run('-i', image, 'import', '*.bin', 'copy', '--dry-run', cwd=host)
    assert digest(image) == original
    run('-i', image, 'import', '*.bin', 'copy', code=3, cwd=host)
    assert digest(image) == original
    run('-i', image, 'import', '*.bin', 'copy', '--yes', cwd=host)
    run('-i', image, 'import', 'file.txt', 'copy/', '--yes', cwd=host)
    run('-i', image, 'import', 'assets.zar', 'renamed.zar', '--yes', cwd=host)
    original = digest(image)
    for source, destination in [('*.missing', 'copy'), ('*.zar', 'missing/'),
                                ('*.zar', 'missing'), ('*.zar', 'renamed.zar'),
                                ('dir.bin', 'copy/'), ('*/game.bin', 'copy')]:
        run('-i', image, 'import', source, destination, '--yes', code=1, cwd=host)
        assert digest(image) == original

    # A late conflict or capacity failure rolls back this batch, including its
    # allocated pages, without discarding earlier shell changes.
    (host / 'aaa.bin').write_bytes(b'added before the conflict')
    script = 'mkdir pending\nimport *.bin games\nls games\nls /\napply\nquit\n'
    result = run('-i', image, 'shell', data=script, cwd=host, code=3)
    assert 'aaa.bin' not in result.stdout and 'pending' in result.stdout, result.stdout
    run('-i', image, 'mkdir', 'full', '--yes')
    (host / 'aaa.zar').write_bytes(b'first file')
    with (host / 'zzz.zar').open('wb') as large:
        large.truncate(2 * 1024 * 1024)
    original = digest(image)
    result = run('-i', image, 'shell', cwd=host, code=1,
                 data='import *.zar full\nls full\napply\nquit\n')
    assert 'aaa.zar' not in result.stdout
    assert digest(image) == original

    # Already-expanded argument lists from a host shell are supported too.
    run('-i', image, 'mkdir', 'expanded', '--yes')
    run('-i', image, 'import', 'game.bin', 'other.bin', 'expanded', '--yes', cwd=host)
    result = run('-i', image, 'ls', 'expanded', '--json')
    assert {entry['name'] for entry in json.loads(result.stdout)['data']} == {'game.bin', 'other.bin'}

    # Replacement requires approval, applies to the entire batch, and can be
    # validated without modifying the image.
    (host / 'game.bin').write_bytes(b'updated program')
    original = digest(image)
    run('-i', image, 'import', '*.bin', 'copy', '--dry-run', cwd=host)
    assert digest(image) == original
    run('-i', image, 'import', '*.bin', 'copy', code=3, cwd=host)
    assert digest(image) == original
    run('-i', image, 'import', '*.bin', 'copy', '--yes', cwd=host)
    replaced = root / 'replaced'
    run('-i', image, 'export', 'copy/game.bin', replaced)
    assert replaced.read_bytes() == b'updated program'

    # Replacing an existing file and then running out of space must restore its
    # previous contents as well as remove new files from the failed batch.
    run('-i', image, 'import', 'assets.zar', 'full', '--yes', cwd=host)
    (host / 'assets.zar').write_bytes(b'changed asset data')
    original = digest(image)
    run('-i', image, 'import', '*.zar', 'full', '--yes', cwd=host, code=1)
    assert digest(image) == original
    run('-i', image, 'mkdir', 'copy/dir.bin', '--yes')
    run('-i', image, 'import', 'game.bin', 'copy/dir.bin/child', '--yes', cwd=host)
    (host / 'dir.bin').rmdir()
    (host / 'dir.bin').write_bytes(b'not a directory')
    original = digest(image)
    run('-i', image, 'import', '*.bin', 'copy', '--yes', cwd=host, code=1)
    assert digest(image) == original
    (host / 'dir.bin').unlink()

    if os.name != 'nt':
        import pty

        def shell_with_confirmation(script, code=0):
            master, slave = pty.openpty()
            try:
                child = subprocess.Popen([exe, '-i', str(image), 'shell'], stdin=slave,
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                         cwd=host, text=True)
                os.close(slave)
                slave = None
                os.write(master, script.encode())
                stdout, stderr = child.communicate(timeout=30)
                assert child.returncode == code, (stdout, stderr)
                assert stderr.count('Overwrite existing import destination files?') == 1, stderr
                return stdout
            finally:
                if slave is not None:
                    os.close(slave)
                os.close(master)

        # A single prompt covers all conflicts; declining preserves prior staged
        # changes, and approving still requires apply to write the image.
        shell_with_confirmation('mkdir kept\nimport *.bin games/\nn\napply\nquit\n', code=3)
        original = digest(image)
        shell_with_confirmation('import *.bin games/\ny\ndiscard\nquit\n')
        assert digest(image) == original
        shell_with_confirmation('import *.bin games/\ny\napply\nquit\n')
        run('-i', image, 'export', 'games/game.bin', root / 'shell replacement')
        assert (root / 'shell replacement').read_bytes() == b'updated program'

print('Bulk import/export: patterns, directories, staging, overwrite preflight and rollback passed')
