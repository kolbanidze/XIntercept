#!/bin/bash
gcc -static vxclient.c -o vxclient -lsodium
gcc -static vxserver.c -o vxserver -lsodium
gcc -static generate_keys.c -o generate_keys -lsodium
