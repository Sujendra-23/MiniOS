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
            assert re.search(r"task: [0-9] \| switches: [1-9][0-9]*", text), "No VGA scheduler status"
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


class Machine:
    """One QEMU boot with a serial shell, for the hardware-driver cases."""
    def __init__(self, root, extra):
        self.root = root
        self.log = root / "debug.log"
        self.serial_path = root / "serial.sock"
        self.process = subprocess.Popen([
            "qemu-system-i386", "-accel", "tcg", "-m", "32M",
            "-cdrom", "os.iso", "-display", "none",
            "-chardev", "socket,id=com1,path=" + str(self.serial_path) + ",server=on,wait=off",
            "-serial", "chardev:com1", "-no-reboot", "-no-shutdown",
            "-debugcon", "file:" + str(self.log), *extra,
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.port = None
        self.received = bytearray()
    def output(self):
        return self.log.read_text() if self.log.exists() else ""
    def boot(self):
        wait_for(lambda: "READY:" in self.output(), "kernel boot", self.process)
        wait_for(lambda: self.serial_path.exists(), "serial socket", self.process)
        self.port = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.port.settimeout(5)
        self.port.connect(str(self.serial_path))
        self.port.sendall(b"\r")
        self.read_until("minios> ")
    def read_until(self, text, timeout=15):
        def ready():
            try:
                self.port.setblocking(False)
                chunk = self.port.recv(4096)
                if chunk:
                    self.received.extend(chunk)
            except BlockingIOError:
                pass
            return text.encode() in self.received
        wait_for(ready, "serial text " + repr(text), self.process, timeout)
    def run(self, command, timeout=15):
        self.received.clear()
        self.port.sendall(command.encode() + b"\r")
        self.read_until("minios> ", timeout)
        return self.received.decode("latin1")
    def close(self):
        if self.port:
            self.port.close()
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()

@contextlib.contextmanager
def machine(extra):
    with tempfile.TemporaryDirectory(prefix="minios-") as directory:
        vm = Machine(pathlib.Path(directory), extra)
        try:
            vm.boot()
            yield vm
        except Exception:
            print(vm.output())
            print("Serial output:", bytes(vm.received))
            raise
        finally:
            vm.close()

CAN_ARGS = ["-object", "can-bus,id=canbus0",
            "-device", "kvaser_pci,canbus=canbus0", "-device", "kvaser_pci,canbus=canbus0"]

def pci_and_can_case():
    """PCI config-space scan, then CAN frames between two QEMU-emulated controllers."""
    with machine(CAN_ARGS) as vm:
        log = vm.output()
        assert re.search(r"PCI: [0-9]+ devices \(config ports 0xCF8/0xCFC\)", log), "No PCI scan line"
        assert "PCI: 00:00.0 8086:1237" in log, "i440FX host bridge not enumerated"
        assert len(re.findall(r"PCI: 00:0[0-9a-f]\.0 10e8:8406", log)) == 2, "Two Kvaser CAN cards expected"
        assert re.search(r"CAN: 2 x SJA1000 .*io=0x[0-9a-f]{8},0x[0-9a-f]{8}", log), "CAN init line missing"
        listing = vm.run("pci")
        assert listing.count("10e8:8406") == 2 and "8086:1237" in listing, listing
        for frames in (64, 200):
            reply = vm.run(f"can {frames}")
            match = re.search(r"can: controllers=2 sent=(\d+) received=(\d+) mismatches=(\d+) "
                              r"tx_timeouts=(\d+) rx_timeouts=(\d+)", reply)
            assert match, reply
            sent, received, mismatches, tx_t, rx_t = map(int, match.groups())
            assert sent == frames and received == sent, f"frames sent={sent} received={received}"
            assert mismatches == 0 and tx_t == 0 and rx_t == 0, reply
        print(f"PASS: PCI scan + CAN: 264 frames sent == received, 0 ID/data mismatches")

def read_frame(sock, timeout=10):
    """QEMU's socket netdev frames each Ethernet frame with a 4-byte big-endian length."""
    sock.settimeout(timeout)
    header = b""
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            raise RuntimeError("netdev socket closed")
        header += chunk
    length = int.from_bytes(header, "big")
    body = b""
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk:
            raise RuntimeError("netdev socket closed")
        body += chunk
    return body

def write_frame(sock, frame):
    sock.sendall(len(frame).to_bytes(4, "big") + frame)

def checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(int.from_bytes(data[i:i + 2], "big") for i in range(0, len(data), 2))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return ~total & 0xFFFF

GUEST_MAC = bytes.fromhex("525400123456")
PEER_MAC = bytes.fromhex("52550a000202")
GUEST_IP = bytes([10, 0, 2, 15])
PEER_IP = bytes([10, 0, 2, 2])

def arp_packet(op, sha, spa, tha, tpa, dst):
    return dst + sha + b"\x08\x06" + b"\x00\x01\x08\x00\x06\x04" + op.to_bytes(2, "big") + sha + spa + tha + tpa

def echo_request(ident, seq, payload, corrupt=False):
    icmp = bytearray(b"\x08\x00\x00\x00" + ident.to_bytes(2, "big") + seq.to_bytes(2, "big") + payload)
    icmp[2:4] = checksum(bytes(icmp)).to_bytes(2, "big")
    if corrupt:
        icmp[-1] ^= 0xFF
    ip = bytearray(b"\x45\x00" + (20 + len(icmp)).to_bytes(2, "big") + b"\x00\x00\x40\x00\x40\x01\x00\x00" + PEER_IP + GUEST_IP)
    ip[10:12] = checksum(bytes(ip)).to_bytes(2, "big")
    return GUEST_MAC + PEER_MAC + b"\x08\x00" + bytes(ip) + bytes(icmp)

def net_stats(text):
    return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", text)}

def ethernet_case():
    """RTL8139 on a socket netdev: a scripted host peer exchanges ARP and ICMP echo with the guest."""
    server = socket.create_server(("127.0.0.1", 0))
    server.settimeout(20)
    port = server.getsockname()[1]
    extra = ["-netdev", f"socket,id=n0,connect=127.0.0.1:{port}",
             "-device", "rtl8139,netdev=n0,mac=52:54:00:12:34:56"]
    peer = None
    try:
        with machine(extra) as vm:
            peer, _ = server.accept()
            assert "NET: RTL8139 mac=52:54:00:12:34:56 ip=10.0.2.15" in vm.output(), "NIC init line missing"
            assert "10ec:8139" in vm.run("pci"), "RTL8139 not in PCI listing"
            # ARP: the peer asks who has 10.0.2.15; the guest must answer with its MAC.
            write_frame(peer, arp_packet(1, PEER_MAC, PEER_IP, bytes(6), GUEST_IP, b"\xff" * 6))
            reply = None
            for _ in range(10):
                frame = read_frame(peer)
                if frame[12:14] == b"\x08\x06" and frame[20:22] == b"\x00\x02":
                    reply = frame
                    break
            assert reply, "No ARP reply from guest"
            assert reply[0:6] == PEER_MAC and reply[6:12] == GUEST_MAC, "ARP reply Ethernet addresses"
            assert reply[22:28] == GUEST_MAC and reply[28:32] == GUEST_IP, "ARP reply sender fields"
            assert reply[32:38] == PEER_MAC and reply[38:42] == PEER_IP, "ARP reply target fields"
            # ICMP echo: three requests; replies must echo id/seq/payload with valid checksums.
            payload = bytes((i * 3 + 1) & 0xFF for i in range(56))
            for seq in (1, 2, 3):
                write_frame(peer, echo_request(0xBEEF, seq, payload))
                frame = read_frame(peer)
                assert frame[12:14] == b"\x08\x00" and frame[0:6] == PEER_MAC, "Reply is not unicast IPv4"
                ip, icmp = frame[14:34], frame[34:]
                assert checksum(ip) == 0, "Reply IPv4 header checksum invalid"
                assert ip[12:16] == GUEST_IP and ip[16:20] == PEER_IP and ip[9] == 1
                assert checksum(icmp) == 0, "Reply ICMP checksum invalid"
                assert icmp[0] == 0 and icmp[1] == 0, "Not an echo reply"
                assert icmp[4:6] == (0xBEEF).to_bytes(2, "big") and icmp[6:8] == seq.to_bytes(2, "big")
                assert icmp[8:] == payload, "Echo payload differs"
            # A request with a corrupted checksum must be dropped, not answered.
            write_frame(peer, echo_request(0xBEEF, 9, payload, corrupt=True))
            peer.settimeout(1.5)
            try:
                extra_frame = read_frame(peer, timeout=1.5)
                raise AssertionError("Guest answered a corrupt echo request: " + extra_frame.hex())
            except socket.timeout:
                pass
            # Guest-initiated ARP: `arp 10.0.2.2` sends who-has; the peer answers.
            assert "who-has sent" in vm.run("arp 10.0.2.2")
            request = None
            for _ in range(10):
                frame = read_frame(peer)
                if frame[12:14] == b"\x08\x06" and frame[20:22] == b"\x00\x01":
                    request = frame
                    break
            assert request, "Guest never sent its ARP request"
            assert request[0:6] == b"\xff" * 6 and request[28:32] == GUEST_IP and request[38:42] == PEER_IP
            write_frame(peer, arp_packet(2, PEER_MAC, PEER_IP, GUEST_MAC, GUEST_IP, GUEST_MAC))
            time.sleep(1.5)
            text = vm.run("net")
            stats = net_stats(text)
            assert "arp 10.0.2.2 is-at 52:55:0a:00:02:02" in text, text
            assert stats["icmp_echo_rx"] == 3 and stats["icmp_reply_tx"] == 3, text
            assert stats["bad_checksum"] == 1, text
            assert stats["arp_req_rx"] == 1 and stats["arp_reply_tx"] == 1, text
            assert stats["arp_req_tx"] == 1 and stats["arp_reply_rx"] == 1, text
            print("PASS: RTL8139 ARP reply + 3 ICMP echo replies verified on the wire, corrupt request dropped, "
                  "guest ARP request answered")
    finally:
        if peer:
            peer.close()
        server.close()

def slirp_arp_case():
    """Same NIC against QEMU's user-mode network stack: the guest resolves the SLIRP gateway."""
    extra = ["-netdev", "user,id=n0", "-device", "rtl8139,netdev=n0"]
    with machine(extra) as vm:
        assert "who-has sent" in vm.run("arp 10.0.2.2")
        text = ""
        for _ in range(20):
            time.sleep(0.5)
            text = vm.run("net")
            if "arp_reply_rx=1" in text:
                break
        assert "arp_reply_rx=1" in text and "arp 10.0.2.2 is-at 52:55:0a:00:02:02" in text, text
        print("PASS: RTL8139 on user-mode netdev: ARP request answered by QEMU's SLIRP gateway")

def parse_rt(text):
    tasks = {}
    for index, fields in re.findall(r"^rt([0-9]): (.*)$", text, re.MULTILINE):
        tasks[int(index)] = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", fields)}
    tick = {k: int(v) for k, v in re.findall(r"(tick_\w+)=(\d+)", text)}
    return tasks, tick

def realtime_case():
    """Round-robin misses deadlines; fixed priority meets them; an overloaded job is counted."""
    with machine([]) as vm:
        assert "RT: 3 periodic tasks" in vm.output()
        assert "policy=rr" in vm.run("sched")
        assert "policy=rr" in vm.run("sched rr")
        time.sleep(6)
        rr_tasks, rr_tick = parse_rt(vm.run("rt"))
        assert rr_tasks[0]["deadline_miss"] > 0, f"RR unexpectedly met rt0 deadlines: {rr_tasks[0]}"
        assert rr_tick["tick_samples"] > 100
        reply = vm.run("sched fp")
        assert "policy=fp" in reply, reply
        time.sleep(6)
        text = vm.run("rt")
        fp_tasks, fp_tick = parse_rt(text)
        for i, task in fp_tasks.items():
            assert task["deadline_miss"] == 0, f"rt{i} missed under fixed priority: {text}"
            assert task["done"] >= task["released"] - 1 > 0, f"rt{i} did not complete jobs: {task}"
        assert fp_tasks[0]["resp_max_ticks"] <= 2, "highest priority task should respond within 2 ticks"
        assert fp_tasks[0]["released"] >= 60 and fp_tick["tick_samples"] >= 300
        assert fp_tasks[0]["start_lat_pit_min"] > 0 or fp_tasks[0]["samples"] > 0
        assert fp_tick["tick_pit_min"] > 0 and fp_tick["tick_pit_max"] >= fp_tick["tick_pit_min"]
        # One 25-tick job against a 20-tick deadline: exactly the lowest-priority task misses.
        assert "needs 25 ticks" in vm.run("rt overload")
        time.sleep(4)
        over_tasks, _ = parse_rt(vm.run("rt"))
        assert over_tasks[2]["deadline_miss"] == 1, over_tasks
        assert over_tasks[0]["deadline_miss"] == 0 and over_tasks[1]["deadline_miss"] == 0, over_tasks
        # Workers and the shell must still make progress (background level shares the CPU).
        sched = vm.run("sched")
        assert re.search(r"workers=[1-9][0-9]*,[1-9][0-9]*,[1-9][0-9]*", sched), sched
        rr = rr_tasks[0]
        print(f"PASS: RT round-robin rt0 misses={rr['deadline_miss']}/{rr['released']}; "
              f"fixed priority 0 misses (rt0 done={fp_tasks[0]['done']}/{fp_tasks[0]['released']}, "
              f"rt1 done={fp_tasks[1]['done']}, rt2 done={fp_tasks[2]['done']}); overload counted 1 miss; "
              f"PIT tick jitter {fp_tick['tick_pit_max'] - fp_tick['tick_pit_min']} counts "
              f"(QEMU TCG emulated PIT)")

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
        ("PCI scan + CAN: frames between two emulated SJA1000 controllers", pci_and_can_case),
        ("Ethernet: RTL8139 ARP + ICMP echo against a scripted peer", ethernet_case),
        ("Ethernet: RTL8139 ARP via QEMU user-mode network", slirp_arp_case),
        ("Real-time: round-robin vs fixed priority, deadline misses, PIT jitter", realtime_case),
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
