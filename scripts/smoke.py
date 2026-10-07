#!/usr/bin/env python3
"""Boot the actual GRUB ISO and exercise IRQs and exception stubs via QMP."""
import json
import pathlib
import re
import socket
import subprocess
import tempfile
import time

def wait_for(predicate, description, process, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("QEMU exited unexpectedly")
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise RuntimeError("Timed out waiting for " + description)

def run_case(fault_key, vector, error, page_fault=None):
    with tempfile.TemporaryDirectory(prefix="minios-") as directory:
        root = pathlib.Path(directory)
        log = root / "debug.log"
        qmp_path = root / "qmp.sock"
        process = subprocess.Popen([
            "qemu-system-i386", "-accel", "tcg", "-m", "32M",
            "-cdrom", "os.iso", "-display", "none", "-serial", "none",
            "-no-reboot", "-no-shutdown", "-debugcon", "file:" + str(log),
            "-qmp", "unix:" + str(qmp_path) + ",server=on,wait=off",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        connection = None
        try:
            def output():
                return log.read_text() if log.exists() else ""
            wait_for(lambda: qmp_path.exists(), "QMP socket", process)
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.settimeout(5)
            connection.connect(str(qmp_path))
            stream = connection.makefile("rwb")
            json.loads(stream.readline())
            def command(name, arguments=None):
                payload = {"execute": name}
                if arguments is not None:
                    payload["arguments"] = arguments
                stream.write((json.dumps(payload) + "\n").encode())
                stream.flush()
                while True:
                    response = json.loads(stream.readline())
                    if "error" in response:
                        raise RuntimeError(response["error"])
                    if "return" in response:
                        return response["return"]
            def monitor(text):
                return command("human-monitor-command", {"command-line": text})
            command("qmp_capabilities")
            wait_for(lambda: "READY:" in output(), "kernel boot", process)
            assert "PAGING:" in output(), "Paging initialization missing"
            assert "PAGING MAP TEST: PASS" in output(), "Dynamic map/unmap failed"
            assert re.search(r"FRAMES: free=[1-9][0-9]*", output()), "No free RAM frames"
            registers = monitor("info registers")
            match = re.search(r"CR0=([0-9a-fA-F]+)", registers)
            assert match and int(match[1], 16) & 0x80010000 == 0x80010000, "PG/WP disabled"
            match = re.search(r"CR3=([0-9a-fA-F]+)", registers)
            assert match and int(match[1], 16) != 0 and int(match[1], 16) % 4096 == 0, "Invalid CR3"
            wait_for(lambda: "TICK 300\n" in output(), "three seconds of PIT IRQs", process)
            # CPU-bound workers never yield; all must progress through PIT preemption.
            pattern = r"SCHED ticks=(\d+) switches=(\d+) workers=(\d+),(\d+),(\d+)\n"
            wait_for(lambda: len(re.findall(pattern, output())) >= 3,
                     "preemptive worker progress", process)
            snapshots = [tuple(map(int, entry)) for entry in re.findall(pattern, output())]
            first, last = snapshots[0], snapshots[-1]
            assert last[1] > first[1] > 0, "No repeated context switches"
            assert all(b > a > 0 for a, b in zip(first[2:], last[2:])), "A worker starved"
            assert "PANIC:" not in output(), "Task context corrupted"
            # Send one key at a time to preserve order and verify actual IRQ1 echo.
            for key in ("a", "b", "c", "ret"):
                monitor("sendkey " + key)
                time.sleep(0.15)
            wait_for(lambda: "abc\n" in re.sub(r"(?:TICK [0-9]+|SCHED ticks=[^\n]+)\n", "", output()), "keyboard echo", process)
            # Verify the text really reached VGA, including the timer status row.
            dump = root / "vga.bin"
            command("pmemsave", {"val": 0xB8000, "size": 4000, "filename": str(dump)})
            text = dump.read_bytes()[::2].decode("latin1")
            assert "abc" in text, "Keyboard output missing from VGA"
            assert re.search(r"PIT 100 Hz \| ticks: [1-9][0-9]+", text), "No VGA ticks"
            assert re.search(r"task: [0-3] \| switches: [1-9][0-9]*", text), "No VGA scheduler status"
            monitor("sendkey " + fault_key)
            expected = f"EXCEPTION vector={vector} error={error} eip="
            wait_for(lambda: expected in output(), "exception diagnostic", process)
            if page_fault is not None:
                address, reason = page_fault
                diagnostic = f"PAGE FAULT cr2=0x{address:08x} {reason}"
                wait_for(lambda: diagnostic in output(), "page-fault address and cause", process)
                registers = monitor("info registers")
                match = re.search(r"CR2=([0-9a-fA-F]+)", registers)
                assert match and int(match[1], 16) == address, "CR2 mismatch"
                command("pmemsave", {"val": 0xB8000, "size": 4000, "filename": str(dump)})
                assert diagnostic in dump.read_bytes()[::2].decode("latin1"), "Page fault missing from VGA"
            before = output()
            time.sleep(0.3)
            assert output() == before, "Kernel did not halt after exception"
            assert output().count("MiniOS: booting...") == 1, "Kernel rebooted"
            assert command("query-status")["status"] == "running", "CPU triple-faulted"
            print(f"PASS: ISO boot, paging/map/unmap, preemption, PIT, keyboard, VGA, exception {vector} (error {error})")
        except Exception:
            print(output())
            raise
        finally:
            if connection is not None:
                connection.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            diagnostics = process.stderr.read().decode()
            if process.returncode not in (0, -15):
                print(diagnostics)

if __name__ == "__main__":
    run_case("f12", 6, 0)   # UD2: no CPU-pushed error code.
    run_case("f11", 13, 24) # Invalid GDT selector: CPU-pushed error code.
    run_case("f10", 14, 0, (0x00400000, "not-present read supervisor"))
    run_case("f8", 14, 0, (0, "not-present read supervisor"))
    symbols = subprocess.check_output(["nm", "-n", "build/kernel.elf"], text=True)
    text_start = int(re.search(r"^([0-9a-fA-F]+) \w __text_start$", symbols, re.MULTILINE)[1], 16)
    run_case("f9", 14, 3, (text_start, "protection write supervisor"))
