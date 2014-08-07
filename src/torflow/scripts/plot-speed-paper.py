#!/usr/bin/python

import sys, os, json, gzip, pylab
from numpy import mean, median
from lxml import etree

HOSTS='hosts.xml'
DIRLIST='dirlist'
OUTDIR="graphs"
if not os.path.exists(OUTDIR): os.makedirs(OUTDIR)

pylab.rcParams.update({
    'backend': 'PDF',
    'font.size' : 16,
    'figure.figsize': (4,3),
    'figure.dpi': 100.0,
    'figure.subplot.left': 0.14,
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
    'lines.markersize' : 5,
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
    experiments = getExperiments()

    ramrates, bwuprates, bwdownrates = {}, {}, {}
    for e in experiments:
        parts = e.split('-')
        mode = parts[3]
        name = parts[4]
        if name not in ramrates: ramrates[name] = {'tunnel':0.0,'direct':0.0}
        if name not in bwuprates: bwuprates[name] = {'tunnel':0.0,'direct':0.0}
        if name not in bwdownrates: bwdownrates[name] = {'tunnel':0.0,'direct':0.0}
        bwuprates[name][mode], bwdownrates[name][mode], ramrates[name][mode] = getRates(e, name)

    ## cdf of mem/s rate for direct and tunnel

    meanramdirect = filter(lambda x: x!=0, [ramrates[n]['direct'] for n in ramrates])
    meanramtunnel = filter(lambda x: x!=0, [ramrates[n]['tunnel'] for n in ramrates])

    pylab.figure()

    x, y = getcdf(meanramdirect)
    pylab.plot(x, y, 'k-', label='direct')
    x, y = getcdf(meanramtunnel)
    pylab.plot(x, y, 'r--', label='anonymous')

    pylab.xlabel("Mean Target RAM Consumption Rate (KiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.legend(loc="lower right")
    pylab.savefig("{0}/cdf.ram.pdf".format(OUTDIR))

    print "min of the mean consumption rates direct={0}KiB/s tunnel={1}KiB/s".format(min(meanramdirect), min(meanramtunnel))
    print "median of the mean consumption rates direct={0}KiB/s tunnel={1}KiB/s".format(median(meanramdirect), median(meanramtunnel))
    print "max of the mean consumption rates direct={0}KiB/s tunnel={1}KiB/s".format(max(meanramdirect), max(meanramtunnel))

    ## cdf of bw/s rate for direct and tunnel

    meanbwupdirect = filter(lambda x: x!=0, [bwuprates[n]['direct'] for n in bwuprates])
    meanbwuptunnel = filter(lambda x: x!=0, [bwuprates[n]['tunnel'] for n in bwuprates])
    meanbwdowndirect = filter(lambda x: x!=0, [bwdownrates[n]['direct'] for n in bwdownrates])
    meanbwdowntunnel = filter(lambda x: x!=0, [bwdownrates[n]['tunnel'] for n in bwdownrates])

    pylab.figure()

    x, y = getcdf(meanbwupdirect)
    pylab.plot(x, y, 'k-', label='direct Tx')
    x, y = getcdf(meanbwuptunnel)
    pylab.plot(x, y, 'b:', label='anonymous Tx')
    x, y = getcdf(meanbwdowndirect)
    pylab.plot(x, y, 'g-.', label='direct Rx')
    x, y = getcdf(meanbwdowntunnel)
    pylab.plot(x, y, 'r--', label='anonymous Rx')

    pylab.xlabel("Mean Sniper BW Consumption Rates (KiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.legend(loc="lower right")
    pylab.savefig("{0}/cdf.bw.pdf".format(OUTDIR))

    print "min of the mean sniper bw usage rates direct=up:{0}KiB/s,down:{1}KiB/s tunnel=up:{2}KiB/s,down:{3}KiB/s".format(min(meanbwupdirect), min(meanbwdowndirect), min(meanbwuptunnel), min(meanbwdowntunnel))
    print "median of the mean sniper bw usage rates direct=up:{0}KiB/s,down:{1}KiB/s tunnel=up:{2}KiB/s,down:{3}KiB/s".format(median(meanbwupdirect), median(meanbwdowndirect), median(meanbwuptunnel), median(meanbwdowntunnel))
    print "max of the mean sniper bw usage rates direct=up:{0}KiB/s,down:{1}KiB/s tunnel=up:{2}KiB/s,down:{3}KiB/s".format(max(meanbwupdirect), max(meanbwdowndirect), max(meanbwuptunnel), max(meanbwdowntunnel))

def getRates(dirname, name):
    bwname = "{0}/sniper-node.dat.gz".format(dirname)
    ramname = "{0}/{1}-ram.dat.gz".format(dirname, name)
    if not os.path.exists(bwname) or not os.path.exists(ramname): return 0.0, 0.0, 0.0

    bwstats = getContents(bwname)
    ramstats = getContents(ramname)

    rxbytes = [d[2] for d in bwstats[30:]] # skip header + non-attack
    txbytes = [d[3] for d in bwstats[30:]] # skip header + non-attack
    rambytes = [d[4] for d in ramstats[30:]] # skip header + non-attack
    
    while len(rambytes) > len(rxbytes): rambytes.pop(0)
    assert len(rxbytes) == len(txbytes) and len(txbytes) == len(rambytes)
    
    rambytes.insert(0, 0.0)
    newrambytes = [rambytes[i]-rambytes[i-1] for i in range(1,len(rambytes)-1)]

    bwup, bwdown, ram, count = 0.0, 0.0, 0.0, 0
    for i in range(len(newrambytes)):
        if newrambytes[i] > 10.0:
            ram += newrambytes[i]
            bwup += txbytes[i]
            bwdown += rxbytes[i]
            count += 1

    meanbwup = (bwup/1024.0) / (60 * count) # KiB/s
    meanbwdown = (bwdown/1024.0) / (60 * count) # KiB/s
    meanram = (ram/1024.0) / (60 * count) # KiB/s
    return meanbwup, meanbwdown, meanram

def getExperiments():
    experiments = []
    with open(DIRLIST, 'r') as f:
        for line in f: experiments.append(line.strip())
    return experiments

def getContents(filename):
    data = None
    with gzip.open(filename, 'r') as f: data = json.load(f)
    return data

## helper - cumulative fraction for y axis
def cf(d): return pylab.arange(1.0,float(len(d))+1.0)/float(len(d))

## helper - return step-based CDF x and y values
## only show to the 99th percentile by default
def getcdf(data, show=0.99):
    data.sort()
    frac = cf(data)
    x, y, lasty = [], [], 0.0
    for i in xrange(int(round(len(data)*show))):
        x.append(data[i])
        y.append(lasty)
        x.append(data[i])
        y.append(frac[i])
        lasty = frac[i]
    return x, y

if __name__ == '__main__':
    main()
