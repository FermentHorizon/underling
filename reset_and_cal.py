#!/usr/bin/env python3
"""Force-reset the board and dump everything."""
import serial, time

ser = serial.Serial()
ser.port = "/dev/cu.usbserial-110"
ser.baudrate = 115200
ser.timeout = 1
ser.dtr = False
ser.rts = False
ser.open()

# Force reset via DTR/RTS toggle (ESP32 auto-reset circuit)
print("Forcing board reset via DTR/RTS...")
ser.dtr = False
ser.rts = True
time.sleep(0.1)
ser.dtr = True
ser.rts = False
time.sleep(0.1)
ser.dtr = False
time.sleep(0.5)

# Drain any garbage from bootloader
while ser.in_waiting:
    ser.read(ser.in_waiting)

print("Waiting for board output (60s)...")
start = time.time()
while time.time() - start < 60:
    try:
        raw = ser.readline()
        if raw:
            line = raw.decode("utf-8", errors="replace").rstrip()
            if line:
                print(f"[{time.time()-start:5.1f}s] {line}")
    except Exception as e:
        print(f"[ERR] {e}")

print("\nSending TARE...")
ser.write(b"TARE\n")
ser.flush()

start = time.time()
while time.time() - start < 60:
    try:
        raw = ser.readline()
        if raw:
            line = raw.decode("utf-8", errors="replace").rstrip()
            if line:
                print(f"[{time.time()-start:5.1f}s] {line}")
    except Exception as e:
        print(f"[ERR] {e}")

print("\n=== PUT 3500g WEIGHT ON NOW - CAL in 30s ===")
start = time.time()
while time.time() - start < 30:
    try:
        raw = ser.readline()
        if raw:
            line = raw.decode("utf-8", errors="replace").rstrip()
            if line:
                print(f"[{time.time()-start:5.1f}s] {line}")
    except Exception as e:
        print(f"[ERR] {e}")

print("\nSending CAL 3500...")
ser.write(b"CAL 3500\n")
ser.flush()

start = time.time()
while time.time() - start < 30:
    try:
        raw = ser.readline()
        if raw:
            line = raw.decode("utf-8", errors="replace").rstrip()
            if line:
                print(f"[{time.time()-start:5.1f}s] {line}")
    except Exception as e:
        print(f"[ERR] {e}")

print("\nDONE.")
ser.close()
