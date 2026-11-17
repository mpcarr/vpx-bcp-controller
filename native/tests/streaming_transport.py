"""Real loopback transport with 720p payloads and control traffic. No GUI required."""
import concurrent.futures
import socket
import subprocess
import sys

PIXELS = 1280 * 720

def frame(value, rgba=False):
    data = bytes([value, value, value, 255] if rgba else [value] * 3) * PIXELS
    return f'rgb_dmd_frame?name=Main&bytes={len(data)}\n'.encode() + data

def server(listener):
    with listener:
        listener.settimeout(12)
        conn, _ = listener.accept()
        with conn:
            conn.settimeout(12)
            stream = conn.makefile('rb')
            assert stream.readline().startswith(b'hello?')
            conn.sendall(frame(1) + b'hold\n')
            assert stream.readline() == b'held\n'
            for sequence in range(24):
                # Alternating formats, events and much larger bodies than recv's buffer.
                conn.sendall(frame(sequence + 2, sequence % 2 == 0) + f'ping?seq={sequence}\n'.encode())
                assert stream.readline() == f'pong?seq={sequence}\n'.encode()
            # One Feed may parse replace + the following header before the script
            # clears/re-registers. A frame belongs to the registration at its header.
            partial = frame(66, True)
            conn.sendall(b'replace\n' + partial[:65536])
            assert stream.readline() == b'replaced\n'
            conn.sendall(partial[65536:] + frame(77) + b'complete\n')
            assert stream.readline() == b'checked\n'
            conn.sendall(frame(99)[:12345])
            stream.close()
        conn, _ = listener.accept()
        with conn:
            conn.settimeout(12)
            stream = conn.makefile('rb')
            assert stream.readline().startswith(b'hello?')
            conn.sendall(frame(88, True))
            assert stream.readline() == b'reconnected\n'
            assert stream.readline() == b'goodbye\n'
            stream.close()

listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen()
port = listener.getsockname()[1]
with concurrent.futures.ThreadPoolExecutor(1) as pool:
    job = pool.submit(server, listener)
    subprocess.run([sys.argv[1], str(port)], check=True, timeout=35)
    job.result()
