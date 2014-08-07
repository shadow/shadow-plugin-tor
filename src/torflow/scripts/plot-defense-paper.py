#!/usr/bin/python

import sys, os, json, gzip, pylab
from math import ceil

OUTDIR="graphs"
if not os.path.exists(OUTDIR): os.makedirs(OUTDIR)

pylab.rcParams.update({
    'backend': 'PDF',
    'font.size': 16,
    'figure.figsize': (4,3),#(3,2.25),
    'figure.dpi': 100.0,
    'figure.subplot.left': 0.18,
    'figure.subplot.right': 0.95,
    'figure.subplot.bottom': 0.15,
    'figure.subplot.top': 0.95,
    'grid.color': '0.1',
    'axes.grid' : True,
    'axes.titlesize' : 'small',
    'axes.labelsize' : 'small',
    'axes.formatter.limits': (-4,4),
    'xtick.labelsize' : 'small',
    'ytick.labelsize' : 'small',
    'lines.linewidth' : 3.0,
    'lines.markeredgewidth' : 0.5,
    'lines.markersize' : 10,
    'legend.fontsize' : 'x-small',
    'legend.fancybox' : False,
    'legend.shadow' : False,
    'legend.ncol' : 1.0,
    'legend.borderaxespad' : 0.5,
    'legend.numpoints' : 1,
    'legend.handletextpad' : 0.5,
    'legend.handlelength' : 1.6,
    'legend.labelspacing' : 0.25,
    'legend.markerscale' : 1.0,
    'ps.useafm' : True,
    'pdf.use14corefonts' : True,
    'text.usetex' : True,
})

def main():

    directAttack_10set10 = getContents("../scalability-nocpu/large-64GB-snipetor-direct-relayguard1-10set10/relayguard1-ram.dat.gz")
    directDefense_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/relayguard1-ram.dat.gz")
    tunnelAttack_10set10 = getContents("../scalability-nocpu/large-64GB-snipetor-tunnel-relayguard1-10set10/relayguard1-ram.dat.gz")
    tunnelDefense_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/relayguard1-ram.dat.gz")
    noattack = getContents("../scalability-nocpu/large-64GB-m2.4xlarge-relayguard1-vanilla/relayguard1-ram.dat.gz")

    pylab.figure()

    pylab.plot(getTime(directAttack_10set10), getRam(directAttack_10set10), 'r--', label='direct, no defense')
    pylab.plot(getTime(tunnelAttack_10set10), getRam(tunnelAttack_10set10), 'g-.', label='anon, no defense')
    pylab.plot(getTime(directDefense_10set10), getRam(directDefense_10set10), 'b:', label='direct, MaxQMem=500')
    pylab.plot(getTime(tunnelDefense_10set10), getRam(tunnelDefense_10set10), 'k-', alpha=0.4, label='anon, MaxQMem=250')
    pylab.plot(getTime(noattack), getRam(noattack), 'k-', label="no attack")

    pylab.axvspan(30, 60, facecolor='k', alpha=0.3)
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM Consumed (MiB)")
    pylab.xlim(xmin=20)
    pylab.ylim(ymax=2500)
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/defense.ram.time.pdf".format(OUTDIR))

# in seconds
def getTime(data): return [ceil(d[0]/60.0) for d in data[1:]] # skip header

# in MiB
def getRam(data): return [d[4]/(1024.0*1024.0) for d in data[1:]] # skip header

def getContents(filename):
    data = None
    with gzip.open(filename, 'r') as f: data = json.load(f)
    return data

if __name__ == '__main__':
    main()
