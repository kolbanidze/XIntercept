#!/bin/bash
sudo ifconfig eth0 192.168.56.120 netmask 255.255.255.0 up
sudo systemctl restart NetworkManager
