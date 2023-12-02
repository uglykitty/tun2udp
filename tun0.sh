#!/bin/sh

TUN=tun0
sudo ip tuntap add mode tun dev $TUN user $USER
sudo ip link set $TUN up
sudo ip addr add 192.168.5.1/24 dev $TUN
#sudo ip link set mtu 1452 $TUN
#sudo ip addr add 192.168.5.1/24 peer 192.168.5.2 dev $TUN
