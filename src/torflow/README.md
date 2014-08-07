"hello": a Shadow plug-in
=========================

This plug-in provides a quick example of how to interface with Shadow.
The most important features of the code that enable this are:
 + completely non-blocking I/O and non-blocking system calls
 + polling I/O events using the `epoll` interface (see `$ man epoll`)
 + no process forking or thread creation

copyright holders
-----------------

No copyright is claimed by the United States Government.

licensing deviations
--------------------

No deviations from LICENSE.

last known working version
--------------------------

This plug-in was last tested and known to work with 
Shadow 1.8.0-dev commit 70add41110cc98f03b32a7eb16106c06eff34a6c

usage
-----

Please see the `example.xml`, which may be run in Shadow

```bash
shadow example.xml
```

A binary version of the code is available for usage outside of Shadow.
Run the program `hello` with no arguments to start the server:

```bash
hello
```

Run the program `hello` with the IP address or hostname of the listening
server to run client mode:

```bash
hello localhost
```
