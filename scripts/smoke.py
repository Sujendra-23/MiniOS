#!/usr/bin/env python3
"""Boot the actual GRUB ISO and exercise IRQs and exception stubs via QMP."""
import contextlib
import datetime
import io
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

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

def check_serial(serial_path, process):
    """Drive the COM1 shell through a host socket: prompt, commands, and a burst."""
    port = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    port.settimeout(5)
    port.connect(str(serial_path))
    received = bytearray()
    sent = 0
    def read_until(text, description):
        def ready():
            try:
                port.setblocking(False)
                chunk = port.recv(4096)
                if chunk:
                    received.extend(chunk)
            except BlockingIOError:
                pass
            return text.encode() in received
        try:
            wait_for(ready, description, process)
        except RuntimeError:
            print("Serial output:", bytes(received))
            raise
    def run(command, expected):
        nonlocal sent
        received.clear()
        port.sendall(command.encode() + b"\r")
        sent += len(command) + 1
        read_until(expected, "serial reply to " + repr(command))
        return received.decode("latin1")
    try:
        # The banner may predate the connection; a bare Enter re-prints the prompt.
        port.sendall(b"\r")
        sent += 1
        read_until("minios> ", "serial prompt")
        assert "uptime, sched, mem, uart" in run("help", "minios> ")
        reply = run("uptime", "minios> ")
        assert re.search(r"uptime: [0-9]+s ticks=[1-9][0-9]*", reply), reply
        reply = run("sched", "minios> ")
        assert re.search(r"sched: task=0 switches=[1-9][0-9]* workers=[1-9][0-9]*,[1-9][0-9]*,[1-9][0-9]*", reply), reply
        reply = run("mem", "minios> ")
        assert re.search(r"mem: free_frames=[1-9][0-9]*", reply), reply
        # 205 bytes in one write: far beyond the 16-byte FIFO, so IRQ4 must
        # keep moving bytes into the RX ring while task 0 is descheduled.
        payload = "".join(chr(ord("a") + i % 26) for i in range(200))
        reply = run("echo " + payload, payload + "\r\nminios> ")
        assert payload + "\r\n" in reply, "Burst lost or reordered"
        # Backspace editing: "echx" + DEL + "o hi" runs "echo hi".
        reply = run("echx\x7fo hi", "minios> ")
        assert "\r\nhi\r\n" in reply, reply
        assert "unknown command: nope" in run("nope", "minios> ")
        reply = run("uart", "minios> ")
        match = re.search(r"uart: irqs=(\d+) rx=(\d+) tx=(\d+) rx_dropped=(\d+) overruns=(\d+)", reply)
        assert match, reply
        irqs, rx, tx, dropped, overruns = map(int, match.groups())
        # Every byte the host sent must have been received by the IRQ handler.
        assert irqs > 0 and rx == sent and tx > rx, f"sent={sent}: {reply}"
        assert dropped == 0 and overruns == 0, "Serial bytes lost: " + reply
        return f"serial shell (irqs={irqs} rx={rx} tx={tx})"
    finally:
        port.close()

def run_case(fault_key, vector, error, page_fault=None, serial=False):
    with tempfile.TemporaryDirectory(prefix="minios-") as directory:
        root = pathlib.Path(directory)
        log = root / "debug.log"
        qmp_path = root / "qmp.sock"
        serial_path = root / "serial.sock"
        process = subprocess.Popen([
            "qemu-system-i386", "-accel", "tcg", "-m", "32M",
            "-cdrom", "os.iso", "-display", "none",
            "-chardev", "socket,id=com1,path=" + str(serial_path) + ",server=on,wait=off",
            "-serial", "chardev:com1",
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
            assert "SERIAL: COM1 16550" in output(), "UART not detected"
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
            serial_result = check_serial(serial_path, process) + ", " if serial else ""
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
            print(f"PASS: ISO boot, paging/map/unmap, preemption, PIT, keyboard, VGA, {serial_result}exception {vector} (error {error})")
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

def junit_report(path, cases, elapsed):
    """Write JUnit XML (one <testcase> per boot or test binary) for CI tooling."""
    failures = sum(1 for c in cases if c["failure"])
    suite = ET.Element("testsuite", name="minios-smoke", tests=str(len(cases)),
                       failures=str(failures), errors="0", skipped="0",
                       time=f"{elapsed:.3f}", timestamp=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"))
    for c in cases:
        case = ET.SubElement(suite, "testcase", classname="minios.smoke", name=c["name"], time=f"{c['time']:.3f}")
        if c["failure"]:
            failure = ET.SubElement(case, "failure", message=c["failure"].splitlines()[0][:200], type="AssertionError")
            failure.text = c["failure"]
        ET.SubElement(case, "system-out").text = c["output"]
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    root = ET.Element("testsuites", tests=str(len(cases)), failures=str(failures))
    root.append(suite)
    ET.indent(root)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)

def main():
    """Run every case; always write the report, then exit non-zero on any failure."""
    report = os.environ.get("JUNIT_XML", "build/junit.xml")
    symbols = subprocess.check_output(["nm", "-n", "build/kernel.elf"], text=True)
    text_start = int(re.search(r"^([0-9a-fA-F]+) \w __text_start$", symbols, re.MULTILINE)[1], 16)
    plan = [
        ("boot + serial shell + exception 6 (UD2, no error code)", lambda: run_case("f12", 6, 0, serial=True)),
        ("exception 13 (invalid GDT selector, error code 24)", lambda: run_case("f11", 13, 24)),
        ("page fault: unmapped read at 4 MiB", lambda: run_case("f10", 14, 0, (0x00400000, "not-present read supervisor"))),
        ("page fault: null read", lambda: run_case("f8", 14, 0, (0, "not-present read supervisor"))),
        ("page fault: write to protected kernel code", lambda: run_case("f9", 14, 3, (text_start, "protection write supervisor"))),
    ]
    cases = []
    started = time.monotonic()
    for name, body in plan:
        buffer, failure = io.StringIO(), None
        t0 = time.monotonic()
        try:
            with contextlib.redirect_stdout(buffer):
                body()
        except BaseException as error:  # a failed case must not hide the report
            failure = f"{type(error).__name__}: {error}"
        sys.stdout.write(buffer.getvalue())
        if failure:
            print("FAIL:", name, "-", failure.splitlines()[0])
        cases.append({"name": name, "time": time.monotonic() - t0, "failure": failure, "output": buffer.getvalue()})
    junit_report(report, cases, time.monotonic() - started)
    print(f"JUnit report: {report} ({sum(1 for c in cases if c['failure'])} failed of {len(cases)})")
    return 1 if any(c["failure"] for c in cases) else 0

if __name__ == "__main__":
    sys.exit(main())
