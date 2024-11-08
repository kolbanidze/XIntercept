import os

MESSAGE_FILE = "master_key.txt"
MASTER_KEY_FILE = "master_key.bin"
LUKS_FILE = "/dev/sdb3"
CRYPTDEVICE_NAME = "sdb3_crypt"

with open(MESSAGE_FILE, "r") as file:
    contents = file.read()
key = "".join(contents.split("\n")[-4:])
key = key.replace(" ", "")
key = key.replace("\t", "")
key = key.replace("MKdump:", "")
key = bytes.fromhex(key)

with open(MASTER_KEY_FILE, "wb") as file:
    file.write(key)
os.system(f"sudo cryptsetup luksOpen {LUKS_FILE} --master-key-file {MASTER_KEY_FILE} {CRYPTDEVICE_NAME}")
