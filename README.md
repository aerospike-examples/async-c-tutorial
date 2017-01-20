Async C Tutorial
================

Demonstrate async programming in Aerospike C client with libev on RedHat 6.

## Prerequisites

[libev](http://dist.schmorp.de/libev) 4.20 and above.

```bash
tar xvf libev-4.20.tar.gz
cd libev-4.20
./configure
make
sudo make install
```

[Aerospike C client](http://www.aerospike.com/download/client/c) package with libev on RedHat 6.

```bash
tar xvf aerospike-client-c-4.1.3.el6.x86_64.tgz
cd aerospike-client-c-4.1.3.el6.x86_64
sudo rpm -i aerospike-client-c-libev-devel-4.1.3-1.el6.x86_64.rpm
```

## Build

```bash
make
```

## Usage

```bash
./target/async_tutorial [-h <host>] [-p <port>] [-n <namespace>] [-s <set>] [-e] [-l]
-e: share event loop
-l: use pipeline writes
```
