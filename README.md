Async C Tutorial
================

Demonstrate async programming in Aerospike C client on RedHat 6.

## Prerequisites

One of the following event libraries:

[libev](http://dist.schmorp.de/libev) 4.20 and above.
[libuv](http://docs.libuv.org) 1.8.0 and above.
[libevent](http://libevent.org) 2.0.22 and above.

[Aerospike C client](http://www.aerospike.com/download/client/c) package with chosen event library on RedHat 6.

```bash
tar xvf aerospike-client-c-4.1.3.el6.x86_64.tgz
cd aerospike-client-c-4.1.3.el6.x86_64
sudo rpm -i aerospike-client-c-libev-devel-4.1.3-1.el6.x86_64.rpm
```

## Build

```bash
make EVENT_LIB=libev|libuv|libevent
```

Example

```bash
make EVENT_LIB=libev
```

## Usage

```bash
./target/async_tutorial [-h <host>] [-p <port>] [-n <namespace>] [-s <set>] [-e] [-l]
-e: share event loop
-l: use pipeline writes
```
