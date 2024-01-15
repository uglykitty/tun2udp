# tun2udp

tun2udp

# Build Instructions

## Linux:

1. Make sure gcc-c++, make, cmake, libevent-devel and log4cplus-devel are installed
2. Clone or download the zip for this repo
3. 
```shell
git clone https://github.com/uglykitty/tun2udp.git
```

3. Build
4. 
```shell
cd tun2udp
cmake -S . -B build
cd build
make
```

4. *Optional*
```shell
make install
```
(will install tun2udp into your path)

# Quickstart

## Server

Create TUN interface tun0 and assign an IP address for it.

```shell
ip tuntap add mode tun dev tun0 $USER
ip addr add 192.168.5.1/24 dev tun0
ip link set dev tun0 up
```

or 

```shell
nmcli connection add type tun mode tun group $GROUPS ifname tun0 con-name tun0 ip4 192.168.5.1/24
```

```shell
tun2udp tun0 12345
```

## Client

Create TUN interface tun0 and assign an IP address for it.

```shell
ip tuntap add mode tun dev tun0 user $USER
ip addr add 192.168.5.2/24 dev tun0
ip link set dev tun0 up
```

or

```shell
nmcli connection add type tun mode tun group $GROUPS ifname tun0 con-name tun0 ip4 192.168.5.2/24
```

```shell
tun2udp tun0 12345 host.of.server
```

```shell
ping 192.168.5.1
```
