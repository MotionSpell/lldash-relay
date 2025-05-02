import http.client
import time
import sys
import os
import threading

def put_file(host, port, remote_path, local_path):
    conn = http.client.HTTPConnection(host, port, timeout=10)
    try:
        with open(local_path, "rb") as f:
            data = f.read()
        conn.request("PUT", f"/{remote_path.lstrip('/')}", body=data)
        resp = conn.getresponse()
        print(f"PUT /{remote_path} -> {resp.status} {resp.reason}")
        resp.read()
    except Exception as e:
        print(f"PUT /{remote_path} -> Exception: {e}")
    finally:
        conn.close()

def get_file(host, port, path):
    conn = http.client.HTTPConnection(host, port, timeout=10)
    start = time.time()
    try:
        conn.request("GET", f"/{path.lstrip('/')}")
        resp = conn.getresponse()
        elapsed = time.time() - start
        print(f"GET /{path} -> {resp.status} {resp.reason} (elapsed: {elapsed:.3f}s)")
        resp.read()
    except Exception as e:
        print(f"GET /{path} -> Exception: {e}")
    finally:
        conn.close()

def delayed_put(host, port, remote_path, local_path, delay):
    time.sleep(delay)
    print(f"\n[delayed PUT] Uploading {local_path} after {delay}s delay...")
    put_file(host, port, remote_path, local_path)

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    local_exists = "upload/test_exists.ts"
    remote_exists = "upload/test_exists.ts"
    local_delay = "upload/test_delay.ts"
    remote_delay = "upload/test_delay.ts"
    missing_file = "upload/test_missing.ts"

    # Check files
    for f in [local_exists, local_delay]:
        if not os.path.exists(f):
            print(f"Local file {f} does not exist. Please provide it in upload/")
            sys.exit(1)

    print("Uploading test_exists.ts to server:")
    put_file(host, port, remote_exists, local_exists)

    print("\nTesting existing file (should be instant):")
    get_file(host, port, remote_exists)

    print("\nTesting missing file (should wait ~5s):")
    get_file(host, port, missing_file)

    print("\nTesting delayed file (GET first, then PUT after 2s):")
    # Start GET in a thread
    get_thread = threading.Thread(target=get_file, args=(host, port, remote_delay))
    get_thread.start()
    # Wait 2 seconds, then PUT the file
    delayed_put(host, port, remote_delay, local_delay, delay=2)
    get_thread.join()