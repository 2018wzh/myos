#!/usr/bin/env python3
"""Run the selected lab's functional fixtures without retaining console logs."""
import argparse
import os
from pathlib import Path
import queue
import re
import shlex
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


def command(argv, timeout=None):
    if os.name == "nt":
        text = shlex.join(argv)
        if timeout is not None:
            text = "exec timeout --signal=TERM --kill-after=2 " + str(timeout) + "s " + text
        else:
            text = "exec " + text
        return ["wsl", "-d", "Arch", "--cd", str(ROOT), "--", "sh", "-lc", text]
    return argv


def inject_kernel(main):
    declaration = "void kernel_test(void);\n"
    pattern = re.compile(r"\binit\s*\(\s*\)\s*;")
    found = list(pattern.finditer(main))
    if len(found) == 1:
        end = found[0].end()
    else:
        first = main.find("proc_make_first(")
        if first < 0:
            raise RuntimeError("No supported first-process marker in kernel main")
        prefix = main[:first]
        found = list(re.finditer(r"\b(?:proc_init|mmap_init)\s*\(\s*\)\s*;", prefix))
        if not found:
            raise RuntimeError("No global initialization marker for kernel fixture")
        end = found[-1].end()
        # A direct initialization path must include both prerequisites.
        if "proc_init" not in prefix[:end] or "mmap_init" not in prefix[:end]:
            raise RuntimeError("Kernel fixture needs process and mmap initialization")
    published = main.find("__atomic_store_n")
    if 0 <= published < end:
        raise RuntimeError("Kernel fixture would run after secondary harts were published")
    return declaration + main[:end] + "\n  kernel_test();" + main[end:]


def inject_getpid(source):
    match = re.search(r"\buint64\s+sys_getpid\s*\(\s*void\s*\)\s*\{", source)
    if match is None:
        raise RuntimeError("No supported sys_getpid definition for sleeping-lock fixture")
    opening = match.end() - 1
    # Ignore braces inside comments and C string/character literals.
    tokens = re.finditer(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]""",
                         source[opening:], re.DOTALL)
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                closing = opening + token.end()
                body = "{ extern uint64 kernel_test_getpid(void); return kernel_test_getpid(); }"
                return source[:opening] + body + source[closing:]
    raise RuntimeError("Unclosed sys_getpid definition")


def run_qemu(args):
    qemu = [os.environ.get("QEMU", "qemu-system-riscv64"),
            "-machine", "virt", "-bios", "none", "-kernel", "kernel-qemu.elf",
            "-m", "128M", "-smp", str(args.harts), "-nographic",
            "-serial", "mon:stdio"]
    proc = subprocess.Popen(command(qemu, args.timeout), cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    chunks = queue.Queue()

    def read_console():
        while True:
            part = proc.stdout.read1(4096)
            if not part:
                break
            chunks.put(part)
        chunks.put(None)

    reader = threading.Thread(target=read_console, daemon=True)
    reader.start()
    output = ""
    deadline = time.monotonic() + args.timeout
    sent_uart = False
    uart = "UART-ECHO-314159"
    success = False
    stable_since = None
    try:
        while time.monotonic() < deadline:
            try:
                chunk = chunks.get(timeout=0.1)
            except queue.Empty:
                if args.lab == 4 and stable_since is not None and time.monotonic() - stable_since >= 0.5:
                    return
                continue
            if chunk is None:
                break
            output += chunk.decode("utf-8", errors="replace")
            if "TEST:FAIL" in output or "panic:" in output:
                raise RuntimeError("Guest reported failure:\n" + output[-3000:])
            if args.lab == 4:
                if not sent_uart and "hello world" in output:
                    proc.stdin.write((uart + "\n").encode())
                    proc.stdin.flush()
                    sent_uart = True
                completed = len(re.findall(r"proczero hello world", output))
                if completed > 3:
                    raise RuntimeError("Unexpected extra Lab4 completion calls")
                if output.count("scause") > 1:
                    raise RuntimeError("Faulting user instruction was resumed repeatedly")
                success = completed == 3 and uart in output and "scause" in output
            else:
                success = "TEST:PASS" in output
                if args.lab == 5:
                    success = success and "TEST:KERNEL-PASS" in output
                if args.lab == 6:
                    begin = output.find("TEST:BLOCK-BEGIN")
                    child = output.find("TEST:CHILD")
                    end = output.find("TEST:BLOCK-END")
                    success = success and 0 <= begin < child < end and "TEST:SLEEPLOCK-PASS" in output
            if success:
                if args.lab != 4:
                    return
                if stable_since is None:
                    stable_since = time.monotonic()
        raise RuntimeError("Guest did not complete within the bound:\n" + output[-3000:])
    finally:
        if proc.poll() is None:
            try:
                proc.stdin.write(b"\x01x")
                proc.stdin.flush()
                proc.wait(timeout=3)
            except (OSError, subprocess.TimeoutExpired):
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        proc.stdin.close()
        proc.stdout.close()
        reader.join(timeout=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lab", type=int, choices=(4, 5, 6), required=True)
    parser.add_argument("--harts", type=int, choices=(1, 2), default=2)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--children", type=int, choices=(1, 2, 4), default=4)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    user = ROOT / "src/user/initcode.c"
    kernel_main = ROOT / "src/kernel/main.c"
    kernel_fixture = ROOT / "src/kernel/lab_test.c"
    if kernel_fixture.exists():
        raise RuntimeError("Temporary kernel fixture name is already in use")
    user_backup = user.read_bytes()
    main_backup = kernel_main.read_bytes()
    sysfunc = ROOT / "src/kernel/syscall/sysfunc.c"
    sysfunc_backup = sysfunc.read_bytes() if args.lab == 6 else None
    try:
        fixture = (ROOT / "tests" / ("lab%d_user.c" % args.lab)).read_text(encoding="utf-8")
        if args.lab == 6:
            fixture = "#define TEST_CHILDREN %d\n" % args.children + fixture
        user.write_text(fixture, encoding="utf-8")
        if args.lab in (5, 6):
            kernel_main.write_text(inject_kernel(main_backup.decode("utf-8")), encoding="utf-8")
            kernel_fixture.write_bytes((ROOT / "tests" / ("lab%d_kernel.c" % args.lab)).read_bytes())
        if args.lab == 6:
            sysfunc.write_text(inject_getpid(sysfunc_backup.decode("utf-8")), encoding="utf-8")
        build_args = ["make", "-j2", "HARTS=%d" % args.harts]
        if args.lab == 4:
            # Multiple timer traps must occur while the fixture keeps registers live.
            build_args.append("TIMER_INTERVAL=10000")
        built = subprocess.run(command(build_args),
                               cwd=ROOT, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=180)
        if built.returncode:
            raise RuntimeError("Incremental build failed:\n" +
                               built.stdout.decode("utf-8", errors="replace")[-5000:])
        run_qemu(args)
        print("TEST:PASS lab%d harts%d" % (args.lab, args.harts))
    finally:
        user.write_bytes(user_backup)
        kernel_main.write_bytes(main_backup)
        if sysfunc_backup is not None:
            sysfunc.write_bytes(sysfunc_backup)
        if kernel_fixture.exists():
            kernel_fixture.unlink()
        target = ROOT / "target"
        for pattern in ("lab_test.o", "lab_test.d"):
            for artifact in target.rglob(pattern):
                artifact.unlink()


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print("TEST:FAIL " + str(error))
        raise SystemExit(1)
