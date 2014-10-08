#!/usr/bin/python

import os, sys, random
from lxml import etree
from copy import deepcopy
import bisect
import random
from collections import Counter, Sequence

def main():
    if len(sys.argv) != 7:
        print 'Usage: {0} <old-hosts-filename(str)> <new-hosts-directory(str)> <num-attacks(int)> <sets-per-attack(int)> <sybils-per-set(int)> <probe-wait-ms(int)>'.format(sys.argv[0])
        sys.exit(0)

    infile = sys.argv[1]
    outdir = os.path.abspath(sys.argv[2])
    nattacks = int(sys.argv[3])
    nsets = int(sys.argv[4])
    nsybils = int(sys.argv[5])
    probewaitms = int(sys.argv[6])

    if not os.path.exists(outdir): os.makedirs(outdir)

    # get the current XML file
    parser = etree.XMLParser(remove_blank_text=True)
    tree = etree.parse(infile, parser)
    root = tree.getroot()

    # get list of relays and consensus weights, and file servers
    relays, servers = [], []
    for n in root.iter('node'):
        id = n.attrib['id']
        if 'relay' in id or 'uthority' in id:
            weight = None
            for a in n.iter('application'):
                if 'scallion' in a.attrib['plugin']:
                    args = a.attrib['arguments'].split()
                    weight = int(args[1])
            relays.append((id, weight))
        elif 'server' in id:
            servers.append(id)

    # sort by consensus weight
    relays.sort(key=lambda tup: tup[1], reverse=True)

    # choose targets without replacement
    assert len(relays) >= nattacks
    #targets = random.sample(relays, nattacks)
    targets = weightedSample(relays, nattacks)

    for (target, weight) in targets:
        # generate each new xml file
        directroot, tunnelroot = deepcopy(root), deepcopy(root)
        roots = [directroot, tunnelroot]

        # add sniper plugin
        directplugin = etree.SubElement(directroot, 'plugin')
        directplugin.set("id", "snipetor")
        directplugin.set("path", "~/.shadow/plugins/libshadow-plugin-snipetor.so")
        tunnelplugin = etree.SubElement(tunnelroot, 'plugin')
        tunnelplugin.set("id", "snipetor")
        tunnelplugin.set("path", "~/.shadow/plugins/libshadow-plugin-snipetor.so")

        # turn on memory tracking for target
        for r in roots:
            for n in r.iter('node'):
                if target == n.attrib['id']:
                    n.set("heartbeatloginfo", "node,ram")
                    n.set("loglevel", "info")

        # add new attacker node
        directsniper = etree.SubElement(directroot, 'node')
        directsniper.set('id', 'sniper')
        directsniper.set('cluster', 'USUS')
        directsniper.set('bandwidthdown', '102400')
        directsniper.set('bandwidthup', '102400')
        directsniper.set('quantity', '1')
        directsniper.set('cpufrequency', '16000000')
        directsniper.set('heartbeatloginfo', 'node,ram')
        directsniper.set('loglevel', 'info')
        tunnelsniper = etree.SubElement(tunnelroot, 'node')
        tunnelsniper.set('id', 'sniper')
        tunnelsniper.set('cluster', 'USUS')
        tunnelsniper.set('bandwidthdown', '102400')
        tunnelsniper.set('bandwidthup', '102400')
        tunnelsniper.set('quantity', '1')
        tunnelsniper.set('cpufrequency', '16000000')
        tunnelsniper.set('heartbeatloginfo', 'node,ram')
        tunnelsniper.set('loglevel', 'info')

        # list of ports for our sniper nodes
        # 2 ports per tor instance, 2 instances per attack set for direct, 4 for tunnel
        # 4 so we have enough ports for tunnel mode
        # 9111 is OR port, 9112 is dirport, 9000 is socks port
        ports = [p for p in xrange(9000-(nsets*2*4), 9000)]

        # stager sybil startup over 5 minutes
        starttimes = range(1500, 1800, int(300/(nsets*2))) # 1 prober + 1 sender

        for j in xrange(nsets):
            # each set of sybils uses a different middle and exit
            pool = [target]
            m, e, g2, m2, e2 = target, target, target, target, target
            while m in pool: m = weightedChoice(relays)
            pool.append(m)
            while e in pool or 'exit' not in e: e = weightedChoice(relays)
            pool.append(e)
            while g2 in pool: g2 = weightedChoice(relays)
            pool.append(g2)
            while m2 in pool: m2 = weightedChoice(relays)
            pool.append(m2)
            while e2 in pool or 'exit' not in e2: e2 = weightedChoice(relays)
            pool.append(e2)

            # create the prober tor instances
            ctlp, socksp, tunnelctlp, tunnelsocksp = ports.pop(), ports.pop(), ports.pop(), ports.pop()
            starttimestr = str(starttimes.pop(0))

            # add prober sybil tor client for direct snipe mode
            directsybil = etree.SubElement(directsniper, 'application')
            directsybil.set('plugin', 'scallion')
            directsybil.set('time', starttimestr)
            directsybil.set('arguments', 'client 102400 102400000 102400000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip {0} {1}'.format(ctlp, socksp))

            # add proxy layer prober tor client for tunnel snipe mode
            tunnelsybil1 = etree.SubElement(tunnelsniper, 'application')
            tunnelsybil1.set('plugin', 'scallion')
            tunnelsybil1.set('time', starttimestr)
            tunnelsybil1.set('arguments', 'client 102400 102400000 102400000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip {0} {1}'.format(tunnelctlp, tunnelsocksp))

            # add client layer prober tor client for tunnel snipe mode
            tunnelsybil2 = etree.SubElement(tunnelsniper, 'application')
            tunnelsybil2.set('plugin', 'scallion')
            tunnelsybil2.set('time', starttimestr)
            tunnelsybil2.set('arguments', 'client 102400 102400000 102400000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip {0} {1} {2}'.format(ctlp, socksp, tunnelsocksp))

            # create the sender tor instances
            ctlp2, socksp2, tunnelctlp2, tunnelsocksp2 = ports.pop(), ports.pop(), ports.pop(), ports.pop()
            starttimestr = str(starttimes.pop(0))

            # add sender sybil tor client for direct snipe mode
            directsybil = etree.SubElement(directsniper, 'application')
            directsybil.set('plugin', 'scallion')
            directsybil.set('time', starttimestr)
            directsybil.set('arguments', 'client 102400 102400000 102400000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip {0} {1}'.format(ctlp2, socksp2))

            # add proxy layer sender tor client for tunnel snipe mode
            tunnelsybil1 = etree.SubElement(tunnelsniper, 'application')
            tunnelsybil1.set('plugin', 'scallion')
            tunnelsybil1.set('time', starttimestr)
            tunnelsybil1.set('arguments', 'client 102400 102400000 102400000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip {0} {1}'.format(tunnelctlp2, tunnelsocksp2))

            # add client layer sender tor client for tunnel snipe mode
            tunnelsybil2 = etree.SubElement(tunnelsniper, 'application')
            tunnelsybil2.set('plugin', 'scallion')
            tunnelsybil2.set('time', starttimestr)
            tunnelsybil2.set('arguments', 'client 102400 102400000 102400000 ./client.torrc ./data/clientdata ~/.shadow/share/geoip {0} {1} {2}'.format(ctlp2, socksp2, tunnelsocksp2))

            # servers: one per sybil
            serverarg = ','.join([':'.join([random.choice(servers), "80"]) for i in xrange(nsybils)])

            # now add the sniper tor controllers
            directsnipetor = etree.SubElement(directsniper, 'application')
            directsnipetor.set('plugin', 'snipetor')
            directsnipetor.set('time', '1800')
            directsnipetor.set('arguments', '{0},{1},{2} {3} {4}:{5}:{6}:{7} {8}'.format(target, m, e, probewaitms, ctlp, socksp, ctlp2, socksp2, serverarg))

            tunnelsnipetor = etree.SubElement(tunnelsniper, 'application')
            tunnelsnipetor.set('plugin', 'snipetor')
            tunnelsnipetor.set('time', '1800')
            tunnelsnipetor.set('arguments', '{0},{1},{2} {3} {4}:{5}:{6}:{7} {8} {9},{10},{11} {12}:{13}'.format(target, m, e, probewaitms, ctlp, socksp, ctlp2, socksp2, serverarg, g2, m2, e2, tunnelctlp, tunnelctlp2))

        # write out new xml file
        with open("{0}/snipetor-direct-{1}.xml".format(outdir, target), 'w') as fout: fout.write(etree.tostring(directroot, pretty_print=True, xml_declaration=False))
        with open("{0}/snipetor-tunnel-{1}.xml".format(outdir, target), 'w') as fout: fout.write(etree.tostring(tunnelroot, pretty_print=True, xml_declaration=False))

    print "finished generating {0} direct and {0} tunnel sniper attack xml files in {1}".format(len(targets), outdir)

def weightedChoice(choices):
   total = sum(w for c, w in choices)
   r = random.uniform(0, total)
   upto = 0
   for c, w in choices:
      if upto + w > r:
         return c
      upto += w
   assert False, "Shouldn't get here"

def weightedSample(choices, k):
    population = [d[0] for d in choices]
    weights = [d[1] for d in choices]
    di = {}
    for d in choices: di[d[0]] = d[1]
    samples = {}
    result = []
    while len(result) < k:
        samp = random.sample(WeightedPopulation(population, weights), 1)
        for s in samp:
            if s not in samples:
                samples[s] = True
                result.append((s, di[s]))
    return result

class WeightedPopulation(Sequence):
    def __init__(self, population, weights):
        assert len(population) == len(weights) > 0
        self.population = population
        self.cumweights = []
        cumsum = 0 # compute cumulative weight
        for w in weights:
            cumsum += w   
            self.cumweights.append(cumsum)  
    def __len__(self):
        return self.cumweights[-1]
    def __getitem__(self, i):
        if not 0 <= i < len(self):
            raise IndexError(i)
        return self.population[bisect.bisect(self.cumweights, i)]

if __name__ == '__main__':
    main()
