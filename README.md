Async C Tutorial
================

Demonstrate async programming in Aerospike C client.

## Prerequisites

One of the following event libraries:

[libev](http://dist.schmorp.de/libev) 4.20 and above.
[libuv](http://docs.libuv.org) 1.8.0 and above.
[libevent](http://libevent.org) 2.0.22 and above.

[Aerospike C client](http://www.aerospike.com/download/client/c) package with chosen event library.

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
