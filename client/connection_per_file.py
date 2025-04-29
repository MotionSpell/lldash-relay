import sys
import requests
import os

def upload_file(server_url, local_path, remote_path):
    with open(local_path, 'rb') as f:
        data = f.read()
    url = f"{server_url.rstrip('/')}/{remote_path.lstrip('/')}"
    resp = requests.put(url, data=data)
    print(f"PUT {url} -> {resp.status_code} {resp.reason}")

def upload_all_in_folder(server_url, folder):
    for fname in os.listdir(folder):
        fpath = os.path.join(folder, fname)
        if os.path.isfile(fpath):
            upload_file(server_url, fpath, f"{folder}/{fname}")

if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[2] == "ALL":
        upload_all_in_folder(sys.argv[1], "upload")
    elif len(sys.argv) == 4:
        upload_file(sys.argv[1], sys.argv[2], sys.argv[3])
    else:
        print("Usage:")
        print("  python main.py <server_url> <local_file> <remote_path>")
        print("  python main.py <server_url> ALL")
        sys.exit(1)