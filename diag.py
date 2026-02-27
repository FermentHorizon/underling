import serial, time, sys

s = serial.Serial()
s.port = '/dev/cu.usbserial-110'
s.baudrate = 115200
s.timeout = 2
s.dtr = False
s.rts = False
s.open()

print("Waiting for a reading...")
end = time.time() + 120
while time.time() < end:
    line = s.readline().decode('utf-8', errors='replace').strip()
    if line:
        print(line)
    if '] W:' in line:
        break

print("\n--- SENDING TARE ---")
s.write(b'TARE\n')
s.flush()

end = time.time() + 60
while time.time() < end:
    line = s.readline().decode('utf-8', errors='replace').strip()
    if line:
        print(line)
    if '[TARE] Done' in line:
        break

print("\n--- COLLECTING READINGS ---")
end = time.time() + 120
count = 0
while time.time() < end:
    line = s.readline().decode('utf-8', errors='replace').strip()
    if line and '] W:' in line:
        print(line)
        count += 1
        if count >= 6:
            break

s.close()
