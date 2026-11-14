#!/usr/bin/env python3
"""Observe existing nice/ps commands; run with Python 3 on a POSIX host.

Build and boot only a disposable source copy. No original fs.img is opened.
This does not test fairness, scheduling order, yield, or negative kernel inputs
(xv6 atoi does not parse a minus sign). See --help for timeout controls.
"""

import argparse
import math
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import subprocess
import tempfile
import time


def stop_group(proc):
    """Reap make and stop its QEMU descendants, including on timeout."""
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(proc.pid, sig)
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            continue
        # make may exit before a descendant; still kill the remaining group.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        return
    proc.wait(timeout=2)


class Shell:
    def __init__(self, directory, boot_timeout, command_timeout):
        self.timeout = command_timeout
        self.pending = b""
        self.transcript = bytearray()
        self.proc = subprocess.Popen(
            ["make", "qemu", "CPUS=1"], cwd=directory,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, start_new_session=True,
        )
        self.selector = selectors.DefaultSelector()
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.selector.register(self.proc.stdout, selectors.EVENT_READ)
        try:
            self.read_prompt(boot_timeout)
        except BaseException:
            self.close()
            raise

    def read_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            prompt = re.search(rb"(?:^|\n)\$ ", self.pending)
            if prompt:
                output = self.pending[:prompt.start()]
                self.pending = self.pending[prompt.end():]
                return output.decode("utf-8", "replace").replace("\r", "")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"shell prompt timed out after {timeout}s")
            if not self.selector.select(remaining):
                continue
            chunk = os.read(self.proc.stdout.fileno(), 4096)
            if not chunk:
                raise RuntimeError("QEMU closed output before the shell prompt")
            self.transcript.extend(chunk)
            self.pending += chunk

    def command(self, command):
        self.proc.stdin.write((command + "\n").encode())
        self.proc.stdin.flush()
        output = self.read_prompt(self.timeout)
        lines = output.splitlines()
        if not lines or lines[0] != command:
            raise AssertionError(f"missing command echo for {command!r}: {output!r}")
        output = "\n".join(lines[1:])
        print(f"$ {command}\n{output}", flush=True)
        return output

    def close(self):
        try:
            if self.proc.poll() is None:
                try:
                    self.proc.stdin.write(b"\x01x")  # QEMU nographic exit
                    self.proc.stdin.flush()
                    self.proc.wait(timeout=2)
                except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
                    pass
        finally:
            stop_group(self.proc)
            self.selector.close()
            self.proc.stdin.close()
            self.proc.stdout.close()


def expect(output, expected):
    if output != expected:
        raise AssertionError(f"expected {expected!r}, got {output!r}")


def rows(output):
    result = []
    for line in output.splitlines():
        match = re.fullmatch(r"(\d+) (\d+)\s+(unused|used|sleep|runble|run|zombie|\?\?\?)\s+(\S+)", line)
        if not match:
            raise AssertionError(f"invalid ps row: {line!r}")
        pid, nice, state, name = match.groups()
        result.append((int(pid), int(nice), state, name))
    return result


def check_commands(shell):
    listing = rows(shell.command("ps"))
    shells = [row for row in listing if row[3] == "sh"]
    if len(shells) != 1:
        raise AssertionError(f"expected one shell: {listing!r}")
    pid, original, _, _ = shells[0]
    expect(str(original), "20")
    if not any(row[3] == "init" for row in listing):
        raise AssertionError("ps omitted init")
    if len([row for row in listing if row[3] == "ps" and row[1] == 20]) != 1:
        raise AssertionError("ps child did not have default nice 20")

    def get(value):
        expect(shell.command(f"nice {pid}"), f"getnice({pid}) = {value}")

    def set_value(value, status=0):
        expect(shell.command(f"nice {pid} {value}"), f"setnice({pid}, {value}) = {status}")

    try:
        get(20)
        for value in (7, 0, 40):
            set_value(value)
            get(value)
        set_value(41, -1)
        get(40)
        missing = max(row[0] for row in listing) + 10000
        expect(shell.command(f"nice {missing}"), f"getnice({missing}) failed")
        expect(shell.command(f"nice {missing} 10"), f"setnice({missing}, 10) = -1")
        single = rows(shell.command(f"ps {pid}"))
        if len(single) != 1 or single[0][:2] != (pid, 40) or single[0][3] != "sh":
            raise AssertionError(f"ps PID did not select only the shell: {single!r}")
        expect(shell.command(f"ps {missing}"), "")
        for command in ("nice", f"nice {pid} 20 extra"):
            expect(shell.command(command), "usage: nice pid [value]")
        expect(shell.command(f"ps {pid} extra"), "usage: ps [pid]")
        get(40)  # rejected arity must not change the shell's nice
        set_value(7)
        inherited = rows(shell.command("ps"))
        children = [row for row in inherited if row[3] == "ps"]
        parents = [row for row in inherited if row[0] == pid]
        if (len(children) != 1 or children[0][0] == pid or children[0][1] != 7
                or len(parents) != 1 or parents[0][1] != 7):
            raise AssertionError(f"shell/exec child nice inheritance failed: {inherited!r}")
    finally:
        set_value(original)
        get(original)
    print("PASS: nice defaults, set/get, bounds, missing PID, ps, arity, shell child inheritance")


def ignore_generated(directory, names):
    ignored = []
    for name in names:
        path = Path(directory) / name
        if (name in {".git", "__pycache__", "fs.img", ".gdbinit"}
                or name.endswith((".o", ".d", ".asm", ".sym", ".pyc", ".tar.gz"))
                or (path.parent.name == "user" and (name.startswith("_") or name == "usys.S"))
                or (path.parent.name == "kernel" and name == "kernel")
                or (path.parent.name == "mkfs" and name == "mkfs")):
            ignored.append(name)
    return ignored


def positive_timeout(value):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and positive")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-timeout", type=positive_timeout, default=120)
    parser.add_argument("--boot-timeout", type=positive_timeout, default=30)
    parser.add_argument("--command-timeout", type=positive_timeout, default=10)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="xv6-assignment2-") as temporary:
        directory = Path(temporary) / "source"
        shutil.copytree(Path(__file__).resolve().parent, directory, ignore=ignore_generated)
        build = subprocess.Popen(
            ["make", "kernel/kernel", "fs.img", "CPUS=1"], cwd=directory,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True,
        )
        try:
            output, _ = build.communicate(timeout=args.build_timeout)
        except BaseException:
            stop_group(build)
            raise
        if build.returncode:
            raise RuntimeError("build failed:\n" + output.decode("utf-8", "replace"))
        print("PASS: isolated kernel and filesystem build", flush=True)
        shell = Shell(directory, args.boot_timeout, args.command_timeout)
        try:
            check_commands(shell)
        except BaseException:
            print("QEMU transcript:\n" + shell.transcript.decode("utf-8", "replace"), flush=True)
            raise
        finally:
            shell.close()


if __name__ == "__main__":
    main()
