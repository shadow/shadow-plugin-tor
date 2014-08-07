#!/usr/bin/python

import sys, os, select, json, gzip

USAGE="{0} [dofilter]\n\treads shadow log file from **standard input** and\n\toutputs timestamps for various stages of hidden service .onion requests".format(sys.argv[0])

def main():
    valid, dofilter = True, False
    if len(sys.argv) > 1:
        for arg in sys.argv[1:]:
            if arg == "dofilter": dofilter = True
            else: valid = False
    if not select.select([sys.stdin,],[],[],0.0)[0]: valid = False
    if not valid: print USAGE; exit()

    # list of all the steps we found for each hs request 'flow'
    hstimes = []
    # dict mapping various steps in each flow to a human readable string
    phased = {}
    # current hidden service 'flows' between clients/servers
    currentflows = {}
    currentclientpaths = {}
    currentserverpaths = {}

    for line in sys.stdin:
        parts = line.strip().split()
        if len(parts) < 3 or 'n/a' in parts[2]: continue

        time = parsetimestamp(parts[2])
        name = parts[4][1:parts[4].index('-')]

        if 'hiddenserver' not in name and 'hiddenclient' not in name: continue
        if dofilter:
            if 'hs-tor' in line or "hs-step-" in parts[6] or "fs-new-connection" == parts[6]: print line.strip();
            continue

        flowid = name[len(name)-1:]

        phase, msg = None, None
        if 'hs-tor' in line:
            if flowid not in currentflows: continue
            circid = parts[16]
            oldphaseid = "hs-tor-"+parts[18]
            newphaseid = "hs-tor-"+parts[20]

            pathparts = parts[23].split(',')
            path = [p.split('~')[1] for p in pathparts]

            phasestrings = ' '.join(parts[24:])
            phasestringssplit = phasestrings.split("' to '")
            oldphasestring = phasestringssplit[0][2:]
            newphasestring = phasestringssplit[1][:len(phasestringssplit[1])-2]

            if oldphaseid not in phased: phased[oldphaseid] = oldphasestring
            if newphaseid not in phased: phased[newphaseid] = newphasestring

            if newphaseid == "hs-tor-12": currentserverpaths[flowid] = path
            elif newphaseid == "hs-tor-17": currentclientpaths[flowid] = path
        elif "hs-step-" in parts[6]:
            phaseid = parts[6]
            phasestring = ' '.join(parts[7:])
            if phaseid not in phased: phased[phaseid] = phasestring

            if phaseid == "hs-step-1":
                # start of flow
                currentflows[flowid] = [int(flowid)]
            currentflows[flowid].append(time)
            if phaseid == "hs-step-3":
                # end of flow
                currentflows[flowid].append(currentserverpaths[flowid])
                hstimes.append(currentflows[flowid])
                currentflows[flowid] = None
                currentclientpaths[flowid] = None
                currentserverpaths[flowid] = None
            
        elif "fs-new-connection" == parts[6]:
            phaseid = parts[6]
            phasestring = ' '.join(parts[7:10])
            if phaseid not in phased: phased[phaseid] = phasestring

    #hstimes.insert(0, phased)
    for p in phased: print p, phased[p]

    if not dofilter:            
        with gzip.open("hs-times.dat.gz", 'w') as f: json.dump(hstimes, f)

# helper - parse shadow timestamps
def parsetimestamp(stamp):
    parts = stamp.split(":")
    h, m, s, ns = int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3])
    seconds = float(h*3600.0) + float(m*60.0) + float(s) + float(ns/1000000000.0)
    return seconds

if __name__ == '__main__':
    main()
