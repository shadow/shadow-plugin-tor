#!/usr/bin/python

import sys, os, select, json, gzip

STATSDIR = "stats"
USAGE="{0} [skipram] [skipnode]\n\treads sniper attack log file from **standard input** and\n\toutputs memory and bandwidth stats to the '{1}/' directory".format(sys.argv[0], STATSDIR)

def main():
    valid = True
    skipram, skipnode = False, False
    if len(sys.argv) > 1:
        for arg in sys.argv[1:]:
            if arg == "skipram": skipram = True
            elif arg == "skipnode": skipnode = True
            else: valid = False
    if not select.select([sys.stdin,],[],[],0.0)[0]: valid = False
    if not valid: print USAGE; exit()

    ramstats, nodestats = {}, {}
    ramheaders, nodeheaders = None, None
    lasttime = 0
    complete = False

    for line in sys.stdin:
        parts = line.strip().split()
        if len(parts) < 3 or 'n/a' in parts[2]: continue

        time = parsetimestamp(parts[2])
        lasttime = time
        name = parts[4][1:parts[4].index('-')]

        if not skipnode and '_tracker_logNode' in parts[5]:
            if 'node-header' in parts[7]:
                #print "found node named '{0}' with node statistics".format(name)
                if nodeheaders is None:
                    nodeheaders = parts[8].split(',')
                    nodeheaders.insert(0, 'time')
                if name not in nodestats: nodestats[name] = [nodeheaders]
            elif 'node' in parts[7]:
                rowdata = parts[8].split(',')
                rowdata = [float(r) for r in rowdata]
                rowdata.insert(0, time)
                nodestats[name].append(rowdata)
        elif not skipram and '_tracker_logRAM' in parts[5]:
            if 'ram-header' in parts[7]:
                #print "found node named '{0}' with memory statistics".format(name)
                if ramheaders is None:
                    ramheaders = parts[8].split(',')
                    ramheaders.insert(0, 'time')
                if name not in ramstats: ramstats[name] = [ramheaders]
            elif 'ram' in parts[7]:
                rowdata = parts[8].split(',')
                rowdata = [float(r) for r in rowdata]
                rowdata.insert(0, time)
                ramstats[name].append(rowdata)

    if False:pass#lasttime < 3540: print "experiment reached only {0}/3600 seconds, no stats were saved".format(lasttime)
    else:
        if not os.path.exists(STATSDIR): os.makedirs(STATSDIR)
        if not skipnode:
            for name in nodestats:
                with gzip.open("{0}/{1}-node.dat.gz".format(STATSDIR, name), 'w') as f:
                    json.dump(nodestats[name], f)
        if not skipram:
            for name in ramstats:
                with gzip.open("{0}/{1}-ram.dat.gz".format(STATSDIR, name), 'w') as f:
                    json.dump(ramstats[name], f)

# helper - parse shadow timestamps
def parsetimestamp(stamp):
    parts = stamp.split(":")
    h, m, s, ns = int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3])
    seconds = float(h*3600.0) + float(m*60.0) + float(s) + float(ns/1000000000.0)
    return seconds

if __name__ == '__main__':
    main()
