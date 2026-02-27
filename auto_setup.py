#!/usr/bin/env python3
"""
auto_setup.py — Fully non-interactive tare + calibrate.
  1) Opens port (board resets)
  2) Waits for boot
  3) Immediately tares (platform must be empty NOW)
  4) Prints readings, then gives you 30 s to place weight
  5) Calibrates
  6) Prints final readings and exits

Usage:  python3 auto_setup.py 3500
"""

import serial, sys, time

PORT = "/dev/cu.usbserial-110"
BAUD = 115200

def safe_read(ser):
    try:
        return ser.readline()
    except (serial.SerialException, OSError):
        return b""

def wait_for(ser, marker, label, timeout=120):
    print(f"⏳ Waiting for '{marker}' ({label})...")
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = safe_read(ser)
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            print(f"  {line}")
        if marker in line:
            print(f"✅ {label} done.\n")
            return True
    print(f"❌ Timeout waiting for '{marker}'")
    return False

def read_lines(ser, n=3, timeout=60):
    count = 0
    deadline = time.time() + timeout
    while count < n and time.time() < deadline:
        raw = safe_read(ser)
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line and "[" in line:
            print(f"  📊 {line}")
            count += 1

def drain(ser):
    time.sleep(0.3)
    while ser.in_waiting:
        safe_read(ser)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 auto_setup.py <cal_weight_grams>")
        print("  e.g. python3 auto_setup.py 3500")
        sys.exit(1)
    cal_weight = sys.argv[1]

    print(f"Opening {PORT} and resetting board...")
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.timeout = 2
    ser.dtr = False
    ser.rts = False
    ser.open()
    # Force a clean reset so we always catch "System ready"
    ser.dtr = True
    ser.rts = True
    time.sleep(0.1)
    ser.dtr = False
    ser.rts = False
    ser.reset_input_buffer()

    try:
        # 1. Wait for boot
        if not wait_for(ser, "System ready", "Boot"):
            sys.exit(1)

        # 2. Show a couple of readings
        print("Pre-tare readings:")
        read_lines(ser, 2)

        # 3. TARE immediately (user confirmed platform empty)
        drain(ser)
        print("\n🔧 TARING (platform should be empty)...")
        ser.write(b"TARE\n")
        ser.flush()
        if not wait_for(ser, "TARE] Done", "TARE", timeout=90):
            sys.exit(1)

        print("Post-tare readings (should be ~0 g):")
        read_lines(ser, 3)

        # 4. Countdown — user places weight
        print(f"\n{'='*50}")
        print(f"  PUT {cal_weight} g ON THE PLATFORM NOW!")
        print(f"  You have 60 seconds...")
        print(f"{'='*50}")
        for i in range(60, 0, -1):
            print(f"  {i}...", end="\r")
            time.sleep(1)
            # keep reading serial so buffer doesn't overflow
            while ser.in_waiting:
                safe_read(ser)
        print(f"\n  Time's up — calibrating now.\n")

        # 5. CAL
        drain(ser)
        print(f"🔧 CALIBRATING with {cal_weight} g...")
        ser.write(f"CAL {cal_weight}\n".encode())
        ser.flush()
        if not wait_for(ser, "CAL] Done", "CAL", timeout=90):
            sys.exit(1)

        print(f"Post-cal readings (should be ~{cal_weight} g):")
        read_lines(ser, 5)

        print("\n✅ ALL DONE. Calibration saved to NVS.")
        print("   Board survives resets — no auto-tare on boot.\n")

    except KeyboardInterrupt:
        print("\nAborted.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
