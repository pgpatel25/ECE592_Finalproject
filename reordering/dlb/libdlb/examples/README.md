# libdlb examples

`hello_world` sends one event through a load-balanced DLB queue and prints the
message when a consumer port receives it. It is the smallest example here of
the basic DLB path: create a scheduling domain, queue, and ports; link the
consumer port to the queue; start the domain; then send, receive, and release
an event.

The DLB driver must already be loaded, and the user must have permission to
open `/dev/dlbN`. From `libdlb`:

```sh
make -C examples
LD_LIBRARY_PATH="$PWD" ./examples/hello_world
```

The optional argument selects a device, for example `./examples/hello_world 1`.
Use `./examples/ldb_traffic -h` for the bundled throughput and scheduling
demonstration options.
