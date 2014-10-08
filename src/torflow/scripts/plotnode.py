#!/usr/bin/python

import sys, os, json, gzip, pylab
from numpy import mean

def main():
    if len(sys.argv) < 2:
        print 'Usage: {0} <nodename-node.dat.gz> ...'.format(sys.argv[0])
        sys.exit(0)

    stats = {}
    for filename in sys.argv[1:]:
        starti = filename.rindex('/')+1 if '/' in filename else 0
        fn = filename[starti:]
        name = filename#fn.split('-')[0]
        with gzip.open(filename, 'r') as f: stats[name] = json.load(f)

    pylab.figure()
    for name in stats:
        time = [d[0]/60.0 for d in stats[name][1:]] # skip header
        rx = [d[2]/(d[1]*1024) for d in stats[name][1:]] # skip header
        tx = [d[3]/(d[1]*1024) for d in stats[name][1:]] # skip header
        #x, y = getcdf(total)
        pylab.plot(time, rx, label=name+"-rx")
        pylab.plot(time, tx, label=name+"-tx")

        rxbytes = [d[2] for d in stats[name][30:]] # skip header + non-attack
        txbytes = [d[3] for d in stats[name][30:]] # skip header + non-attack
        rxmeankib = (sum(rxbytes)/1024.0) / (60 * len(rxbytes))
        txmeankib = (sum(txbytes)/1024.0) / (60 * len(txbytes))
        print "{0} mean bw down={1} up={2} KiB/s".format(name, rxmeankib, txmeankib)

    #pylab.suptitle("Network Bandwidth Usage, measured in 60 second intervals", fontsize="x-small")
    pylab.title("Bandwidth Usage, measured every 60 seconds", fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("Bandwidth Rate (KiB/s)")
    pylab.legend(loc="upper left")
    pylab.savefig("bw.time.pdf")

if __name__ == '__main__':
    main()
