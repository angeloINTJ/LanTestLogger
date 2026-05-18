#!/usr/bin/env python3
"""Send oui.dat to Pico W LittleFS via serial."""

import serial, sys, time, os

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD = 115200

def send_file(port, local_path, remote_path):
    sz = os.path.getsize(local_path)
    print(f"Sending {local_path} ({sz} bytes) to {remote_path}...")

    with serial.Serial(port, BAUD, timeout=5) as ser:
        time.sleep(2)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Send command and wait for READY
        cmd = f"uploadoui {os.path.basename(remote_path)} {sz}\r\n"
        ser.write(cmd.encode())
        ser.flush()

        # Wait for READY (Pico pode estar ocupado com testes)
        deadline = time.time() + 35  # timeout de 35s
        ack = ""
        while time.time() < deadline:
            if ser.in_waiting:
                ack = ser.read_until(b"\n").decode(errors="ignore").strip()
                if "READY" in ack:
                    break
                print(f"  got: {ack}")
            time.sleep(0.2)

        if "READY" not in ack:
            print(f"Error: expected READY after {35}s, last: {ack}")
            print("Pico may be busy testing. Try again between cycles.")
            return False
        print(f"  Pico: {ack}")

        # Send binary data in chunks
        with open(local_path, "rb") as f:
            sent = 0
            while True:
                chunk = f.read(256)
                if not chunk:
                    break
                ser.write(chunk)
                sent += len(chunk)
                # Small delay to avoid buffer overflow on Pico
                if sent % 4096 == 0:
                    time.sleep(0.05)
                    print(f"  {sent}/{sz} bytes ({sent*100//sz}%)", end="\r")

        time.sleep(0.5)
        result = ser.read_until(b"\n").decode(errors="ignore").strip()
        print(f"\nResult: {result}")
        return "OK" in result

if __name__ == "__main__":
    base = os.path.dirname(os.path.abspath(__file__))

    send_file(PORT, os.path.join(base, "build/oui.dat"), "/oui.dat")
    print()

    names_path = os.path.join(base, "build/ouinames.dat")
    if os.path.exists(names_path):
        send_file(PORT, names_path, "/ouinames.dat")
