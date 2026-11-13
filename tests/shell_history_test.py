"""Exercise the actual terminal editor; no images or physical devices are used."""
import os
import pty
import select
import subprocess
import sys
import termios
import time

exe = sys.argv[1]
expected_version = subprocess.check_output([exe, '--version']).strip()
master, slave = pty.openpty()
before = termios.tcgetattr(slave)
child = subprocess.Popen([exe, 'shell'], stdin=slave, stdout=slave, stderr=slave)
pending = b''

def expect(marker):
    global pending
    deadline = time.monotonic() + 10
    while marker not in pending:
        assert time.monotonic() < deadline, (marker, pending)
        if select.select([master], [], [], 0.1)[0]:
            pending += os.read(master, 65536)
    end = pending.index(marker) + len(marker)
    result, pending = pending[:end], pending[end:]
    return result

def send(data):
    os.write(master, data)

def shown(line):
    return b'zdt> ' + line + b'\x1b7\x1b8'

prompt = shown(b'')
try:
    expect(prompt)
    # Recall and execute a previous command; down restores an unfinished draft.
    send(b'version\n')
    expect(prompt)
    send(b'\x1b[A')
    expect(shown(b'version'))
    send(b'\n')
    assert expected_version in expect(prompt)
    send(b'vers\x1b[A\x1b[B')
    expect(shown(b'vers'))
    send(b'ion\n')
    assert expected_version in expect(prompt)
    # Clear with Ctrl-U, edit a recalled line, and cancel with Ctrl-C.
    send(b'\x1b[A\x01\x1b[3~')
    expect(b'zdt> \x1b7ersion\x1b8')
    send(b'\x03')
    expect(prompt)
    # Each command has distinct trailing spaces. Only the latest 32 survive.
    for n in range(34):
        send(b'version' + b' ' * n + b'\n')
        expect(prompt)
    send(b'\x1b[A' * 32)
    expect(shown(b'version  '))
    send(b'\x1b[A\x1b[A\x1b[B')
    expect(shown(b'version   '))
    send(b'\x1b[B' * 31)
    expect(prompt)
    send(b'\x04')
    assert child.wait(timeout=10) == 0
    after = termios.tcgetattr(slave)
    # macOS may set PENDIN when canonical processing is restored.
    after[3] &= ~getattr(termios, "PENDIN", 0)
    before[3] &= ~getattr(termios, "PENDIN", 0)
    assert after == before, (before, after)
finally:
    if child.poll() is None:
        child.kill()
        child.wait()
    os.close(master)
    os.close(slave)

# A new shell starts with an empty history.
master, slave = pty.openpty()
child = subprocess.Popen([exe, 'shell'], stdin=slave, stdout=slave, stderr=slave)
pending = b''
try:
    expect(prompt)
    send(b'\x1b[Aquit\n')
    assert child.wait(timeout=10) == 0
finally:
    if child.poll() is None:
        child.kill()
        child.wait()
    os.close(master)
    os.close(slave)
print('Shell history: recall, editing, draft, 32-command limit, reset and terminal restoration passed')
