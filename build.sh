#!/bin/bash
mkdir -p bin/
docker build -t xintercept-builder .
docker create --name temp xintercept-builder
docker cp temp:/build/vxclient bin/
docker cp temp:/build/vxserver bin/
docker cp temp:/build/generate_keys bin/
docker rm temp

