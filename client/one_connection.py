import sys
import os
import http.client
import time

def upload_file(conn, host, remote_path, local_path):
    with open(local_path, 'rb') as f:
        data = f.read()
    conn.putrequest("PUT", f"/{remote_path.lstrip('/')}")
    conn.putheader("Host", host)
    conn.putheader("Content-Length", str(len(data)))
    conn.endheaders()
    conn.send(data)
    try:
        resp = conn.getresponse()
        print(f"PUT /{remote_path} -> {resp.status} {resp.reason}")
        resp.read()  # Ensure the response is fully read
    except Exception as e:
        print(f"PUT /{remote_path} -> Exception: {e}")

def upload_all_in_folder(server_host, server_port, folder):
    files = [fname for fname in os.listdir(folder) if os.path.isfile(os.path.join(folder, fname))]
    conn = http.client.HTTPConnection(server_host, server_port)
    try:
        for idx, fname in enumerate(files):
            fpath = os.path.join(folder, fname)
            upload_file(conn, server_host, f"{folder}/{fname}", fpath)
            if idx == 39:
                print("Sent 40 files, sleeping for 10 seconds to trigger server timeout...")
                time.sleep(10)
                print("Trying to send another file on the same connection after timeout...")
                # Try to send the 41st file again (should fail)
                upload_file(conn, server_host, f"{folder}/{files[40]}", os.path.join(folder, files[40]))
    finally:
        conn.close()
    # Now try with a new connection
    print("Opening a new connection and retrying the same file...")
    conn2 = http.client.HTTPConnection(server_host, server_port)
    try:
        upload_file(conn2, server_host, f"{folder}/{files[40]}", os.path.join(folder, files[40]))
    finally:
        conn2.close()

if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[3] == "ALL":
        url = sys.argv[1]
        folder = sys.argv[2]
        if url.startswith("http://"):
            url = url[len("http://"):]
        host, port = url.split(":")
        upload_all_in_folder(host, int(port), folder)
    else:
        print("Usage:")
        print("  python one_connection.py <server_host:port> <folder> ALL")
        print("Example: python one_connection.py localhost:9000 upload ALL")
        sys.exit(1)