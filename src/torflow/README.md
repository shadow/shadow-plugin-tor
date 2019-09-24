## "torflow": a Shadow plug-in

This plug-in duplicates the functionality of TorFlow, Tor's bandwidth 
measurement system, within Shadow.

## Usage

Torflow arguments should be configured as follows:
	arguments="torflow key=value ..."

The valid keys and the type of the valid values are listed below, along with any defaults.
The format is: `key`:ValueType (default=val) [Mode=ValidModes] - explanation

Specifying the run mode is option, the default mode is TorFlow:

 + `Mode`:String (default=TorFlow) [Mode=TorFlow,FileServer]  
    The running mode of this instance. Valid values are 'TorFlow' and 'FileServer'  
    TorFlow mode runs a full network scanner, FileServer mode runs only a server  
    that serves as the server side of network scanner connections.  
      
    The default mode ('TorFlow') is to run a full TorFlow instance that  
    runs a file server and a network scanner that transfers data from  
    the file server through pairs of Tor relays. The output of each round  
    of scans is a bandwidth file that can be used by directory authorities  
    to vote on and produce a network consensus.  
      
    The other mode ('FileServer') is to run a file server only, that can  
    be connected to from another full TorFlow instance. In this case, the  
    network info (name and port) of this FileServer instance should be  
    provided to a TorFlow instance using the FileServerInfo option.  

The following are required arguments (default values do not exist):

 + `V3BWFilePath`:String [Mode=TorFlow]  
    The path to which the output v3bw file should be written.

 + `TorSocksPort`:Integer [Mode=TorFlow]  
    The Tor SOCKS server port, set in the torrc file of the Tor instance.

 + `TorControlPort`:Integer [Mode=TorFlow]  
    The Tor Control server port, set in the torrc file of the Tor instance.

 + `FileServerInfo`:String/Integer [Mode=TorFlow]  
    The network name and port given as 'name:port' of a TorFlow file server  
    that our probes will connect with to perform measurements.  
    This argument can be supplied multiple times to append several such  
    servers to the list of servers used during the probes.

The following are optional arguments (default values exist):

 + `LogLevel`:String (default=info) [Mode=TorFlow,FileServer]  
    The log level to use while running TorFlow. Valid values are:  
    'debug' > 'info' > 'message' > 'warning'  
    Messages logged at a higher level than the configured level will be filtered.

 + `ListenPort`:Integer (default=18080) [Mode=TorFlow,FileServer]  
    The port that the file server should listen on for incoming connections  
    from TorFlow network scanner connections.

 + `ScanIntervalSeconds`:Integer (default=0) [Mode=TorFlow]  
    The amount of time in seconds to pause between complete network scans.  
    Useful for speeding up debug trials, especially in the minimal case.

 + `NumParallelProbes`:Integer (default=4) [Mode=TorFlow]  
    The number of TorFlow workers constructing measurement circuits in parallel.

 + `NumRelaysPerSlice`:Integer (default=50) [Mode=TorFlow]  
    The number of relays to include in a single slice. Slices that  
    contain no exit relays, or only exit relays, are skipped completely.

 + `MaxRelayWeightFraction`:Float (default=0.05) [Mode=TorFlow]  
    The fraction of network bandwidth any one relay is allowed to have in the  
    final measurement. Any relay that has a measured bandwidth greater than  
    (MaxRelayWeightFraction * sum of measured bandwidths) has its measurement  
    clipped to that value.

 + `ProbeTimeoutSeconds`:Integer (default=300) [Mode=TorFlow]  
    Time in seconds to wait before marking an unfinished probe as failed.

 + `NumProbesPerRelay`:Integer (default=5) [Mode=TorFlow]  
    Number of times we need to measure each relay before a slice is done.

## Example

To run TorFlow in your ShadowTor network, add something like the following to an
existing `shadow.config.xml` file.

```
<plugin id="torflow" path="~/.shadow/lib/libshadow-plugin-torflow.so"/>

<host id="torflowauthority" bandwidthdown="12207" bandwidthup="12207" geocodehint="US">
  <process plugin="tor" preload="tor-preload" starttime="1700" arguments="--Address ${NODEID} --Nickname ${NODEID} --DataDirectory shadow.data/hosts/${NODEID} --GeoIPFile ~/.shadow/share/geoip --defaults-torrc conf/tor.common.torrc -f conf/tor.client.torrc"/>
  <process plugin="torctl" starttime="1701" arguments="localhost 9051 STREAM,CIRC,CIRC_MINOR,ORCONN,BW,STREAM_BW,CIRC_BW,CONN_BW"/>
  <process plugin="torflow" starttime="1800" arguments="Mode=TorFlow LogLevel=info ListenPort=18080 TorSocksPort=9000 TorControlPort=9051 FileServerInfo=torflowauthority:18080 V3BWFilePath=shadow.data/hosts/torflowauthority/v3bw ScanIntervalSeconds=0 NumParallelProbes=4 NumRelaysPerSlice=10 MaxRelayWeightFraction=0.25 ProbeTimeoutSeconds=300 NumProbesPerRelay=3"/>
</host>
```

You may need to modify the above example depending on your simulation setup.

