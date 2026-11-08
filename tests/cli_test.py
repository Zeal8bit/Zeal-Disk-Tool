"""End-to-end CLI tests on temporary images only."""
import hashlib
import json
import pathlib
import os
import subprocess
import sys
import tempfile

exe = sys.argv[1]
def run(*args, code=0, data=None, cwd=None):
    result = subprocess.run([exe, *map(str, args)], input=data, text=True, capture_output=True, cwd=cwd)
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result

def prompt(args, answer, code=0):
    import pty
    master, slave = pty.openpty()
    try:
        child = subprocess.Popen([exe, *map(str, args)], stdin=slave, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        os.close(slave)
        slave = None
        os.write(master, answer.encode())
        stdout, stderr = child.communicate(timeout=30)
        assert child.returncode == code, (args, child.returncode, stdout, stderr)
        return stdout, stderr
    finally:
        os.close(master)
        if slave is not None:
            os.close(slave)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

with tempfile.TemporaryDirectory(prefix="zdt-cli-") as directory:
    root = pathlib.Path(directory)
    image = root / "test image.img"
    source = root / "payload.bin"
    source.write_bytes(bytes(range(256)) * 300)
    exported = root / "export.bin"
    help_result = run("--help")
    assert help_result.stdout.startswith("zdt ") and "Usage: zdt " in help_result.stdout
    assert run("unknown-command", code=2).stderr.startswith("zdt: ")
    run("image", "create", image, "--size", "2MiB", "--mbr", code=3)
    assert not image.exists()
    run("image", "create", image, "--size", "2MiB", "--mbr", "--dry-run")
    assert not image.exists()
    run("image", "create", image, "--size", "2MiB", "--mbr", "--yes")
    original = digest(image)
    result = run("--image", image, "partition", "create", "--size", "1MiB", "--json", "--yes")
    assert json.loads(result.stdout)["ok"]
    run("--image", image, "mkdir", "/folder", code=3)
    after_partition = digest(image)
    run("--image", image, "mkdir", "/folder", "--yes")
    run("--image", image, "import", source, "/folder/file.bin", "--yes")
    result = run("--image", image, "ls", "/folder", "--json")
    listing = json.loads(result.stdout)["data"]
    assert listing[0]["name"] == "file.bin" and listing[0]["size"] == source.stat().st_size
    # All CLI filesystem arguments are root-relative; optional slashes work in
    # one-shot and shell modes without changing host import/export paths.
    for path in ("folder", "folder/", "/folder", "/folder/"):
        assert json.loads(run("--image", image, "ls", path, "--json").stdout)["data"] == listing
    assert json.loads(run("--image", image, "ls", "////", "--json").stdout) == json.loads(run("--image", image, "ls", "--json").stdout)
    run("--image", image, "mkdir", "paths/", "--yes")
    run("--image", image, "import", source.name, "paths/file.bin", "--yes", cwd=root)
    run("--image", image, "export", "paths/file.bin/", "relative export.bin", cwd=root)
    assert (root / "relative export.bin").read_bytes() == source.read_bytes()
    run("--image", image, "export", "paths/file.bin", "preview.bin", "--dry-run", cwd=root)
    assert not (root / "preview.bin").exists()
    run("--image", image, "rm", "paths/file.bin/", "--yes")
    run("--image", image, "rmdir", "/paths/", "--yes")
    before_shell = digest(image)
    path_script = f'mkdir relaxed/\nimport "{source}" relaxed/file\nls relaxed/\nexport relaxed/file "{root / "relaxed.bin"}"\nrm relaxed/file\nrmdir /relaxed/\ndiscard\nquit\n'
    run("--image", image, "shell", data=path_script)
    assert (root / "relaxed.bin").read_bytes() == source.read_bytes()
    assert digest(image) == before_shell
    for invalid in ("", "a" * 511):
        assert not json.loads(run("--image", image, "ls", invalid, "--json", code=1).stdout)["ok"]
    run("--image", image, "export", "/folder/file.bin", exported)
    assert exported.read_bytes() == source.read_bytes()
    run("--image", image, "export", "/folder/file.bin", exported, code=3)
    run("--image", image, "export", "/folder/file.bin", exported, "--yes")
    original = digest(image)
    run("--image", image, "rm", "/folder/file.bin", "--dry-run")
    assert digest(image) == original
    script = f'mkdir /staged\nimport "{source}" /staged/file\nexport /staged/file "{root / "staged.bin"}"\nstatus\ndiscard\nls /\nquit\n'
    result = run("--image", image, "shell", data=script)
    assert (root / "staged.bin").read_bytes() == source.read_bytes()
    assert digest(image) == original
    run("--image", image, "shell", data="mkdir /persist\napply\nquit\n")
    listing = json.loads(run("--image", image, "ls", "--json").stdout)["data"]
    assert "persist" in [e["name"] for e in listing]
    original = digest(image)
    run("--image", image, "shell", data="mkdir /lost\n", code=3)
    assert digest(image) == original
    run("--partition", "0", "info", code=2)
    run("--image", image, "--device", "anything", "info", code=2)
    run("--image", image, "partition", "create", "--alignment", "513", code=2)
    result = run("--image", image, "ls", "/missing", "--json", code=1)
    assert not json.loads(result.stdout)["ok"]
    run("--image", image, "rm", "/folder/file.bin", "--yes")
    run("--image", image, "rmdir", "/folder", "--yes")
    if os.name == "posix":
        original = digest(image)
        stdout, stderr = prompt(["--image", image, "mkdir", "/declined", "--json"], "n\n", code=3)
        assert not json.loads(stdout)["ok"] and digest(image) == original
        stdout, stderr = prompt(["--image", image, "mkdir", "/approved", "--json"], "y\n")
        assert json.loads(stdout)["ok"] and "Apply changes?" in stderr
        _, shell_stderr = prompt(["--image", image, "shell"], "mkdir /quitapply\nquit\na\n")
        assert "zdt> " in shell_stderr
        names = [e["name"] for e in json.loads(run("--image", image, "ls", "--json").stdout)["data"]]
        assert "approved" in names and "quitapply" in names and "declined" not in names
    new_image = root / "new.img"
    run("shell", data=f'image create "{new_image}" --size 32KiB\npartition create\nmkdir /hello\napply\nquit\n')
    assert new_image.exists()
    listing = json.loads(run("--image", new_image, "ls", "--json").stdout)["data"]
    assert listing[0]["name"] == "hello"
    unicode_image = root / "image-ζ.img"
    unicode_source = root / "source-ζ.bin"
    unicode_source.write_bytes(b"unicode host paths")
    run("image", "create", unicode_image, "--size", "32KiB", "--yes")
    run("--image", unicode_image, "partition", "create", "--yes")
    run("--image", unicode_image, "import", unicode_source, "/file.bin", "--yes")
    unicode_export = root / "export-ζ.bin"
    run("--image", unicode_image, "export", "/file.bin", unicode_export)
    assert unicode_export.read_bytes() == unicode_source.read_bytes()
print("CLI commands, JSON, confirmation, dry-run, shell and transfers passed")
