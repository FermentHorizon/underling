#!/usr/bin/env python3
"""Bare-minimum: open port, dump everything for 60s, send TARE, dump more."""
import serial, time, sys

ser = serial.Serial()
ser.port = "/dev/cu.usbserial-110"
ser.baudrate = 115200
ser.timeout = 1
ser.dtr = False
ser.rts = False
ser.open()
print("PORT OPEN (no reset). Dumping output for 45s...")

start = time.time()
while time.time() - start < 45:
    try:
        raw = ser.readline()
        if raw:
            print(raw.decode("utf-8", errors="replace").rstrip())
    except Exception as e:
        print(f"[ERR] {e}")

# Send TARE
print("\n>>> SENDING TARE <<<")
ser.write(b"TARE\n")
ser.flush()

# Read for 30s
start = time.time()
while time.time() - start < 30:
    try:
        raw = ser.readline()
        if raw:
            print(raw.decode("utf-8", errors="replace").rstrip())
    except Exception as e:
        print(f"[ERR] {e}")

cal_weight = sys.argv[1] if len(sys.argv) > 1 else None
if cal_weight:
    print(f"\n>>> PUT {cal_weight}g ON PLATFORM NOW — sending CAL in 30s <<<")
    start = time.time()
    while time.time() - start < 30:
        try:
            raw = ser.readline()
            if raw:
                print(raw.decode("utf-8", errors="replace").rstrip())
        except Exception as e:
            print(f"[ERR] {e}")

    print(f"\n>>> SENDING CAL {cal_weight} <<<")
    ser.write(f"CAL {cal_weight}\n".encode())
    ser.flush()

    start = time.time()
    while time.time() - start < 30:
        try:
            raw = ser.readline()
            if raw:
                print(raw.decode("utf-8", errors="replace").rstrip())
        except Exception as e:
            print(f"[ERR] {e}")

print("\nDONE.")
ser.close()
