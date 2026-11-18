#!/bin/bash

sudo timeout 15s ./build/linux_network_performance   veth-tx veth-rx   "$(cat /sys/class/net/veth-tx/address)"   "$(cat /sys/class/net/veth-rx/address)"   Hello 10 1
