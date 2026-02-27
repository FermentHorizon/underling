#!/usr/bin/env python3
"""
setup_scale.py — Tare and calibrate without fighting the serial reset.

Opening the port WILL reset the ESP32 (hardware auto-reset, unavoidable).
This script accepts that, waits for boot, then does tare + cal in ONE session
so the port only opens ONCE.

Usage:
    python3 setup_scale.py              # tare only
    python3 setup_scale.py --cal 3500   # tare, then calibrate with 3500 g
"""

import serial
import sys
import time

PORT = "/dev/cu.usbserial-110"
BAUD = 115200
TIMEOUT = 2   # short readline timeout — we loop anyway
BOOT_TIMEOUT = 120  # total seconds to wait for boot


def safe_readline(ser):
    """Read a line, returning b'' on transient serial errors."""
    try:
        return ser.readline()
    except (serial.SerialException, OSError):
        return b""


def wait_for_boot(ser):
    """Read lines until 'System ready' appears."""
    print("\n⏳ Board is booting (this takes ~30-40 s with 50-sample averaging)...")
    deadline = time.time() + BOOT_TIMEOUT
    while time.time() < deadline:
        raw = safe_readline(ser)
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            print(f"  {line}")
        if "System ready" in line:
            print("✅ Boot complete.\n")
            return
    print("❌ Timed out waiting for boot. Check USB cable / board.")
    sys.exit(1)


def send_command(ser, cmd, done_marker, label="", timeout=90):
    """Send a command and wait for the done marker in output."""
    # Drain any pending output first
    time.sleep(0.5)
    while ser.in_waiting:
        safe_readline(ser)

    print(f"📡 Sending: {cmd}")
    ser.write(f"{cmd}\n".encode())
    ser.flush()

    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = safe_readline(ser)
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            print(f"  {line}")
        if done_marker in line:
            print(f"✅ {label or cmd} complete.\n")
            return
    print(f"⚠️  Timed out waiting for '{done_marker}' — check board output.")


def read_n(ser, n=3, timeout=60):
    """Read n weight lines and print them."""
    count = 0
    deadline = time.time() + timeout
    while count < n and time.time() < deadline:
        raw = safe_readline(ser)
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line and line.startswith("["):
            print(f"  📊 {line}")
            count += 1


def main():
    cal_weight = None
    if "--cal" in sys.argv:
        idx = sys.argv.index("--cal")
        if idx + 1 < len(sys.argv):
            cal_weight = sys.argv[idx + 1]
        else:
            print("Error: --cal requires a weight value (e.g. --cal 3500)")
            sys.exit(1)

    print(f"Opening {PORT} at {BAUD} baud (no reset)...")
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.timeout = TIMEOUT
    ser.dtr = False
    ser.rts = False
    ser.open()

    try:
        wait_for_boot(ser)

        # Read a couple of lines to see current state
        print("Current readings (before tare):")
        read_n(ser, 2)

        # --- TARE ---
        input("\n🔧 Make sure the platform is EMPTY, then press Enter to TARE...")
        send_command(ser, "TARE", "TARE] Done", "TARE")
        print("Readings after tare (should be ~0 g):")
        read_n(ser, 3)

        # --- CAL ---
        if cal_weight:
            input(f"\n🔧 Put {cal_weight} g on the platform, then press Enter to CAL...")
            send_command(ser, f"CAL {cal_weight}", "CAL] Done", "CAL")
            print(f"Readings after cal (should be ~{cal_weight} g):")
            read_n(ser, 3)

        print("\n✅ All done. Calibration saved to NVS.")
        print("   Board will survive resets — no more auto-tare.\n")

    except KeyboardInterrupt:
        print("\n\nAborted by user.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
