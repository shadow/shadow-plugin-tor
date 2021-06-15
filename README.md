# :bangbang: ARCHIVAL NOTICE - 2021-06-15 :bangbang:
In Shadow v2.x.x, we have transitioned to a new architecture that can run the Tor binary directly rather than building it as a plugin that is loaded into Shadow. This means you can build Tor directly from the Tor source using the Tor build scripts, and no longer need to use the custom, Shadow-specific build process from this repository.

We've archived this repository for posterity and for those wanting to use Shadow v1.15.0 or earlier, but **no further development updates will be posted here**.

**Use at your own risk**; if it breaks, you get to keep both pieces.

For those wanting to run Tor experiments in Shadow, see https://github.com/shadow/tornettools

-----

# shadow-plugin-tor

This repository holds a Shadow plug-in that runs the Tor anonymity software.
It can be used to run private Tor networks of clients and relays on a 
single machine using the Shadow discrete-event network simulator.

Setup and Usage Instructions:
  + https://github.com/shadow/shadow-plugin-tor/wiki  

More Information about Shadow:
  + https://shadow.github.io
  + https://github.com/shadow/shadow

More Information about Tor:
  + https://www.torproject.org
  + https://gitweb.torproject.org/  

# Contributing

Contributions can be made by submitting pull requests via GitHub.
