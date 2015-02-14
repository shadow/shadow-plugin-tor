"torflow": a Shadow plug-in
=========================

This plug-in duplicates the functionality of TorFlow within Shadow.

usage
-----

Torflow arguments should be configured as follows:
	arguments="filename pausetime workers sliceSize nodeCap socksPort ctlPort server:port"

The arguments are listed below, along with the value used by real TorFlow if
applicable, and an explanation.

 + filename: The path to which the output v3bw file should be written.

 + pausetime (0): The amount of time in seconds to pause between complete network
scans. Useful for speeding up debug trials, especially in the minimal case.

 + workers (4): The number of TorFlow workers constructing measurement circuits in
parallel.

 + sliceSize (50): The number of relays to include in a single slice. Slices that
contain no exit relays, or only exit nodes, are skipped completely.

 + nodeCap (0.05): The fraction of network bandwidth any one relay is allowed to
have in the final measurement. Any relay that has a measured bandwidth than
(nodeCap * sum of measured bandwidths) has its measurement clipped to that
value.

 + socksPort: The port used to connect to the Tor instance.

 + ctlPort: The TorControl port, set in the arguments to the tor plugin.

 + server: The name of the test fileserver which TorFlow builds circuits to connect to.

 + port: The port to connect to on the test fileserver.

