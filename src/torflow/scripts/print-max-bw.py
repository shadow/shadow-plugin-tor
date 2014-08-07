#!/usr/bin/python

import sys, os, json, gzip

DIRLIST='dirlist'

def main():
    experiments = getExperiments()
    maxdown, maxup = 0, 0
    for e in experiments:
        down, up = getRates(e)
        if down > maxdown: maxdown = down
        if up > maxup: maxup = up
        print "{0}: max_recv={1} max_send={2}".format(e, down , up)
    print "max of max: up={0} down={1}".format(maxup, maxdown)

def getRates(dirname):
    bwname = "{0}/sniper-node.dat.gz".format(dirname)
    if not os.path.exists(bwname): return 0.0
    bwstats = getContents(bwname)
    rxbytes = [d[2] for d in bwstats[30:]] # skip header + non-attack
    txbytes = [d[3] for d in bwstats[30:]] # skip header + non-attack
    return max(rxbytes)/1024.0/60.0, max(txbytes)/1024.0/60.0
    
def getExperiments():
    experiments = []
    with open(DIRLIST, 'r') as f:
        for line in f: experiments.append(line.strip())
    return experiments

def getContents(filename):
    data = None
    with gzip.open(filename, 'r') as f: data = json.load(f)
    return data

if __name__ == '__main__':
    main()
