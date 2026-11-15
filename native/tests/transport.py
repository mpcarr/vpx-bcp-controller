"""Exercise the compiled native clients against two independent TCP peers."""
import concurrent.futures
import socket
import subprocess
import sys
import time

def server(listener, monitor):
    with listener:
        listener.settimeout(8)
        conn, _ = listener.accept()
        with conn:
            conn.settimeout(8)
            stream = conn.makefile('rb')
            assert stream.readline().startswith(b'hello?version=21&')
            if monitor:
                assert stream.readline() == b'trigger?name=monitor\n'
                conn.sendall(b'hello\ntrigger?name=debug&debug=bool:true\n')
                assert stream.readline() == b'trigger?name=still_connected\n'
            else:
                assert stream.readline() == b'trigger?name=first\n'
                assert stream.readline() == b'trigger?name=second\n'
                conn.sendall(b'hello\ntrigger?name=caf\xc3')
                time.sleep(.02)
                conn.sendall(b'\xa9\nrgb_dmd_frame?name=Unregistered&bytes=3\n\n\x00\xff')
                conn.sendall(b'rgb_dmd_frame?name=Main&bytes=6\n\x01\x02')
                time.sleep(.02)
                conn.sendall(b'\x03\x04\x05\x06rgb_dmd_frame?name=Main&bytes=8\n\x0a\x0d\x00')
                time.sleep(.02)
                conn.sendall(b'\xff\xff\x80\x01\xfftrigger?json={"text":"' + b'x' * 120000 + b'"}\n')
            assert stream.readline() == b'goodbye\n'

listeners = []
for _ in range(2):
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listeners.append(listener)
ports = [str(s.getsockname()[1]) for s in listeners]
with concurrent.futures.ThreadPoolExecutor(2) as pool:
    jobs = [pool.submit(server, s, i == 1) for i, s in enumerate(listeners)]
    subprocess.run([sys.argv[1], *ports], check=True, timeout=15)
    for job in jobs:
        job.result()
print('Two connections, events, fragmented UTF-8 and binary RGB/RGBA, large JSON, shutdown: OK')
