"""Merge/restore integration tests; all targets are temporary image files."""
import hashlib
import json
import os
import time
import pathlib
import struct
import subprocess
import sys
import tempfile

exe = sys.argv[1]
def run(*args, code=0, data=None):
    result = subprocess.run([exe, *map(str, args)], input=data, text=True, capture_output=True)
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result

def confirm_without_backup(target, source, answer):
    # A real terminal verifies the safety prompt; pipe input must never approve it.
    import pty
    import select
    master, slave = pty.openpty()
    process = subprocess.Popen([exe, "-i", str(target), "--partition", "1", "merge", "-f", str(source)],
                               stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    output = b""
    deadline = time.monotonic() + 10
    try:
        while b"Proceed without a backup? [y/N]:" not in output:
            assert time.monotonic() < deadline, output
            if select.select([master], [], [], 0.1)[0]:
                output += os.read(master, 4096)
        os.write(master, answer.encode() + b"\n")
        assert process.wait(timeout=10) == (0 if answer.lower() == "y" else 3)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)

def report(*args, code=0):
    return json.loads(run(*args, "--json", code=code).stdout)

def image(path, size="256KiB", mbr=True, partitions=("64KiB",)):
    run("image", "create", path, "--size", size, *(["--mbr"] if mbr else []), "--yes")
    for size in partitions:
        run("-i", path, "partition", "create", "--size", size, "--alignment", "512", "--yes")

def put(path, name, content, root, part=1):
    host = root / "input.bin"
    host.write_bytes(content)
    run("-i", path, "--partition", part, "import", host, name, "--yes")

def listing(path, directory="/", part=1):
    return report("-i", path, "--partition", part, "ls", directory)["data"]

def stamp(path, name):
    data = bytearray(path.read_bytes())
    at = data.index(name.encode()) + 22
    data[at:at+8] = bytes([0x20, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07])
    path.write_bytes(data)

def partition_range(data, index=0):
    entry = 446 + index * 16
    start, size = struct.unpack_from("<II", data, entry+8)
    return start*512, size*512

with tempfile.TemporaryDirectory(prefix="zdt-merge-") as tmp:
    root = pathlib.Path(tmp)
    source, target, backup = (root / name for name in ("community.img", "user.img", "before.img"))
    image(source, "128KiB")
    image(target, partitions=("64KiB", "64KiB"))
    for path in [source, target]:
        run("-i", path, "mkdir", "games", "--yes")
        put(path, "games/unchanged", b"identical", root)
    put(source, "games/update.bin", b"new community version", root)
    put(target, "games/update.bin", b"locally changed old version", root)
    put(target, "games/personal", b"personal data", root)
    run("-i", target, "mkdir", "private", "--yes")
    put(target, "private/save", b"saved progress", root)
    put(target, "other-part", b"other partition", root, part=2)
    run("-i", source, "mkdir", "newdir", "--yes")
    put(source, "newdir/empty", b"", root)
    stamp(source, "update.bin")
    stamp(source, "newdir")
    original = target.read_bytes()
    personal_before = listing(target, "games")
    # Ambiguity requires explicit partition selection; no backup is made.
    run("-i", target, "merge", "--from-image", source, "--backup", backup, "--yes", code=2)
    assert target.read_bytes() == original and not backup.exists()
    preview = report("-i", target, "--partition", 1, "merge", "--from-image", source, "--dry-run")["data"]
    assert preview["files_added"] == 1 and preview["files_replaced"] == 1
    assert preview["files_unchanged"] == 1 and preview["files_preserved"] == 2
    assert preview["directories_created"] == 1 and preview["directories_preserved"] == 1
    assert target.read_bytes() == original and not backup.exists()
    # Noninteractive merge needs explicit confirmation, before creating backup.
    report("-i", target, "--partition", 1, "merge", "--from-image", source, "--backup", backup, code=3)
    assert target.read_bytes() == original and not backup.exists()
    applied = report("-i", target, "--partition", 1, "merge", "--from-image", source, "-b", backup, "--yes")["data"]
    assert applied["backup_created"] and applied["applied"]
    offset, size = partition_range(original)
    assert backup.read_bytes() == original[offset:offset+size]
    merged = target.read_bytes()
    assert merged[:offset] == original[:offset] and merged[offset+size:] == original[offset+size:]
    assert {e["name"] for e in listing(target)} == {"games", "private", "newdir"}
    after = {e["name"]: e for e in listing(target, "games")}
    before = {e["name"]: e for e in personal_before}
    assert after["personal"] == before["personal"] and after["unchanged"] == before["unchanged"]
    assert after["update.bin"]["modified"] == "2001-02-03T05:06:07"
    assert next(e for e in listing(target) if e["name"] == "newdir")["modified"] == "2001-02-03T05:06:07"
    # Repeat merges are no-ops and skip backup creation.
    skipped = root / "not-created.img"
    noop = report("-i", target, "--partition", 1, "merge", "--from-image", source, "--backup", skipped, "--yes")["data"]
    assert noop["noop"] and not noop["backup_created"] and not skipped.exists()
    # Restore replaces exactly the selected partition, including deleting added files.
    restored = report("-i", target, "--partition", 1, "restore", "--from-image", backup, "--yes")["data"]
    assert restored["applied"] and target.read_bytes() == original
    # Existing backups cannot be overwritten, and neither failure nor preview touches target.
    report("-i", target, "--partition", 1, "merge", "--from-image", source, "--backup", backup, "--yes", code=1)
    assert target.read_bytes() == original and backup.read_bytes() == original[offset:offset+size]
    report("-i", target, "--partition", 1, "merge", "--from-image", source, "--backup", root/"missing"/"backup.img", "--yes", code=1)
    assert target.read_bytes() == original
    # Omitting backup needs confirmation; --yes explicitly accepts it.
    run("-i", target, "--partition", 1, "merge", "-f", source, code=3)
    assert target.read_bytes() == original
    if os.name != "nt":
        confirm_without_backup(target, source, "")
        assert target.read_bytes() == original
        confirm_without_backup(target, source, "y")
        assert target.read_bytes() != original
        run("-i", target, "--partition", 1, "restore", "-f", backup, "--yes")
        assert target.read_bytes() == original
    unbacked = report("-i", target, "--partition", 1, "merge", "-f", source, "--yes")["data"]
    assert unbacked["applied"] and not unbacked["backup_created"] and not unbacked.get("backup")
    run("-i", target, "--partition", 1, "restore", "-f", backup, "--yes")
    assert target.read_bytes() == original
    script = f'merge -f "{source}" --partition 1\nquit\n'
    run("-i", target, "shell", data=script, code=3)
    assert target.read_bytes() == original
    # Same-file identities (including aliases) are rejected.
    report("-i", source, "merge", "--from-image", source, "--dry-run", code=1)
    alias = root / "alias.img"
    try:
        alias.hardlink_to(source)
    except (OSError, AttributeError):
        alias = None
    if alias:
        report("-i", source, "merge", "--from-image", alias, "--dry-run", code=1)
    # Both directions of type conflict are reported in one preflight pass.
    conflict_source, conflict_target = root/"conflict-source.img", root/"conflict-target.img"
    image(conflict_source); image(conflict_target)
    run("-i", conflict_source, "mkdir", "directory", "--yes")
    put(conflict_target, "directory", b"file", root)
    put(conflict_source, "file", b"file", root)
    run("-i", conflict_target, "mkdir", "file", "--yes")
    put(conflict_target, "file/personal", b"keep me", root)
    digest = hashlib.sha256(conflict_target.read_bytes()).digest()
    conflict_backup = root/"conflict-backup.img"
    conflicts = report("-i", conflict_target, "merge", "--from-image", conflict_source, "--backup", conflict_backup, "--yes", code=1)
    assert conflicts["error"]["code"] == "type conflict"
    assert set(conflicts["data"]["conflicts"]) == {"/directory", "/file"}
    assert not conflict_backup.exists() and hashlib.sha256(conflict_target.read_bytes()).digest() == digest
    # Shell -d selects a source device, without switching the open target.
    # A regular image is deliberately invalid as a physical device.
    script = f'merge -d "{source}" --partition 1\nquit\n'
    device_source = run("-i", target, "shell", data=script, code=1)
    assert "Use open to switch targets" not in device_source.stderr
    assert "Device must have 512-byte sectors" in device_source.stderr or "Cannot open device" in device_source.stderr
    assert target.read_bytes() == original
    script = f'merge -i "{source}" --from-image "{source}"\nquit\n'
    duplicate = run("-i", target, "shell", data=script, code=2)
    assert "Choose exactly one merge source" in duplicate.stderr
    # Shell merge creates backup immediately but only Apply writes the target.
    shell_backup, staged_export = root/"shell-backup.img", root/"staged.bin"
    script = f'-i "{source}" merge --partition 1 --backup "{shell_backup}"\nexport games/update.bin "{staged_export}"\ndiscard\nquit\n'
    run("-i", target, "shell", data=script)
    assert shell_backup.read_bytes() == backup.read_bytes() and target.read_bytes() == original
    assert staged_export.read_bytes() == b"new community version"
    shell_backup.unlink()
    script = f'merge --from-image "{source}" --partition 1 --backup "{shell_backup}"\nmkdir later\napply\nrestore --from-image "{shell_backup}" --partition 1\napply\nquit\n'
    run("-i", target, "shell", data=script)
    assert target.read_bytes() == original
    # Dirty shell targets cannot be merged; no backup should be produced.
    dirty_backup = root/"dirty.img"
    script = f'mkdir dirty\nmerge --from-image "{source}" --partition 1 --backup "{dirty_backup}"\ndiscard\nquit\n'
    run("-i", target, "shell", data=script, code=1)
    assert not dirty_backup.exists() and target.read_bytes() == original
    # Insufficient capacity is discovered before backup; freeing replaced files
    # first also allows final trees that fit but cannot hold both versions.
    small, big = root/"small.img", root/"big.img"
    image(small, "8KiB", False, ("8KiB",)); image(big)
    put(big, "large", bytes(20000), root)
    small_before = small.read_bytes()
    run("-i", small, "merge", "--from-image", big, "--backup", root/"overflow.img", "--yes", code=1)
    assert small.read_bytes() == small_before and not (root/"overflow.img").exists()
    put(small, "large", bytes(5000), root)
    put(big, "replacement", bytes(5000), root)
    run("-i", big, "rm", "large", "--yes")
    run("-i", big, "rm", "replacement", "--yes")
    put(big, "large", bytes([7])*5000, root)
    run("-i", small, "merge", "--from-image", big, "--backup", root/"fits.img", "--yes")
    # A raw target with a damaged header can be restored using its exact-size backup.
    damaged = bytearray(small.read_bytes()); damaged[:512] = bytes(512); small.write_bytes(damaged)
    run("-i", small, "restore", "--from-image", root/"fits.img", "--yes")
    assert small.read_bytes() == (root/"fits.img").read_bytes()
    # Raw filesystem data can incidentally contain an MBR signature.
    raw_bytes = bytearray(small.read_bytes()); raw_bytes[510:512] = bytes([0x55, 0xaa]); small.write_bytes(raw_bytes)
    assert report("-i", small, "info")["data"]["mbr"] is False
    raw_backup = root / "signature.img"; raw_backup.write_bytes(raw_bytes)
    damaged = bytearray(raw_bytes); damaged[:512] = bytes(512); small.write_bytes(damaged)
    run("-i", small, "restore", "--from-image", raw_backup, "--yes")
    assert small.read_bytes() == raw_bytes
    run("-i", small, "restore", "--from-image", backup, "--yes", code=1)
    run("-i", small, "restore", "--from-image", source, "--yes", code=1)
print("Merge/restore CLI: backup equality, preservation, conflicts, capacity, shell and recovery passed")
