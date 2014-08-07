#!/usr/bin/python

import sys, os, json, gzip, pylab
from numpy import mean
from lxml import etree

HOSTS='hosts.xml'
DIRLIST='dirlist'
OUTDIR="graphs"
if not os.path.exists(OUTDIR): os.makedirs(OUTDIR)

pylab.rcParams.update({
    'backend': 'PDF',
    'figure.figsize': (4,3),
    'figure.dpi': 100.0,
    'figure.subplot.left': 0.14,
    'figure.subplot.right': 0.96,
    'figure.subplot.bottom': 0.15,
    'figure.subplot.top': 0.87,
    'grid.color': '0.1',
    'axes.grid' : True,
    'axes.titlesize' : 'small',
    'axes.labelsize' : 'small',
    'axes.formatter.limits': (-4,4),
    'xtick.labelsize' : 'small',
    'ytick.labelsize' : 'small',
    'lines.linewidth' : 2.0,
    'lines.markeredgewidth' : 0.5,
    'lines.markersize' : 5,
    'legend.fontsize' : 'x-small',
    'legend.fancybox' : False,
    'legend.shadow' : False,
    'legend.ncol' : 1.0,
    'legend.borderaxespad' : 0.5,
    'legend.numpoints' : 1,
    'legend.handletextpad' : 0.5,
    'legend.handlelength' : 2.25,
    'legend.labelspacing' : 0.25,
    'legend.markerscale' : 1.0,
    'ps.useafm' : True,
    'pdf.use14corefonts' : True,
    'text.usetex' : True,
})

def main():
    weights = getWeights()
    experiments = getExperiments()

    ramrates, bwrates = {}, {}
    for e in experiments:
        parts = e.split('-')
        mode = parts[3]
        name = parts[4]
        if name not in ramrates: ramrates[name] = {'tunnel':0.0,'direct':0.0}
        if name not in bwrates: bwrates[name] = {'tunnel':0.0,'direct':0.0}
        ramrates[name][mode] = getMeanRamConsumptionRate(e, name)
        bwrates[name][mode] = getMeanBwConsumptionRate(e, 'sniper')

    meanramdirect = [ramrates[n]['direct'] for n in ramrates]
    meanramtunnel = [ramrates[n]['tunnel'] for n in ramrates]
    meanbwdirect = [bwrates[n]['direct'] for n in bwrates]
    meanbwtunnel = [bwrates[n]['tunnel'] for n in bwrates]

    ## cdf of mem/s rate for direct and tunnel

    pylab.figure()

    x, y = getcdf(meanramdirect)
    pylab.plot(x, y, 'k-', label='direct')
    x, y = getcdf(meanramtunnel)
    pylab.plot(x, y, 'r--', label='tunnel')

    #pylab.suptitle("RAM Usage", fontsize="small")
    pylab.title("Mean RAM Consumption, measured every 60 seconds", fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Mean Target Ram Consumption Rate (KiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.legend(loc="lower right")
    pylab.savefig("{0}/cdf.ram.pdf".format(OUTDIR))

    ## cdf of bw/s rate for direct and tunnel

    pylab.figure()

    x, y = getcdf(meanbwdirect)
    pylab.plot(x, y, 'k-', label='direct')
    x, y = getcdf(meanbwtunnel)
    pylab.plot(x, y, 'r--', label='tunnel')

    #pylab.suptitle("RAM Usage", fontsize="small")
    pylab.title("Mean RAM Consumption, measured every 60 seconds", fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Mean Target Ram Consumption Rate (KiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.legend(loc="lower right")
    pylab.savefig("{0}/cdf.bw.pdf".format(OUTDIR))

    ## mem/s compared to bandwidth weight, correlations

    pylab.figure()

    x, y = getRamWeightScatter(ramrates, weights, 'direct')
    pylab.scatter(x, y, marker='o', s=10, c='w', edgecolor='k', label='direct')
    (m, b) = pylab.polyfit(x, y, 1)
    yp = pylab.polyval([m,b], x)
    pylab.plot(x, yp, 'k-', label='direct fit')

    x, y = getRamWeightScatter(ramrates, weights, 'tunnel')
    pylab.scatter(x, y, marker='s', s=10, c='w', edgecolor='r', label='tunnel')
    (m, b) = pylab.polyfit(x, y, 1)
    yp = pylab.polyval([m,b], x)
    pylab.plot(x, yp, 'r--', label='tunnel fit')

    #pylab.suptitle("RAM Usage", fontsize="small")
    pylab.title("Correlation of Consensus Weight to Mean RAM Consumption", fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Mean Target Ram Consumption Rate (KiB/s)")
    pylab.ylabel("Consensus Weight")
    pylab.xlim(xmin=0.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="lower right")
    pylab.savefig("{0}/weight.ram.pdf".format(OUTDIR))

def getRamWeightScatter(rates, weights, key):
    data = [(rates[n][key], weights[n]) for n in rates]
    x, y = [], []
    for d in data:
        if d[0] != 0:
            x.append(d[0])
            y.append(d[1])
    return x, y

def getMeanBwConsumptionRate(dirname, name):
    fname = "{0}/stats/{1}-node.dat.gz".format(dirname, name)
    if not os.path.exists(fname): return 0.0
    nodestats = getContents(fname)
    totalbw = getTotalBW(nodestats)
    return mean(totalbw)

def getMeanRamConsumptionRate(dirname, name):
    fname = "{0}/stats/{1}-ram.dat.gz".format(dirname, name)
    if not os.path.exists(fname): return 0.0
    ramstats = getContents(fname)
    newram = getNewRam(ramstats)
    ram = []
    for nr in newram:
        # attack is active when ram is above 10 kib/s
        if nr > 10.0: ram.append(nr)
    return mean(ram)

# in KiB/s
def getTotalBW(data):
    rx, tx = getRxBW(data), getTxBW(data)
    return [rx[i]+tx[i] for i in xrange(len(rx))]

# in KiB/s
def getRxBW(data): return [(d[2]/1024.0)/d[1] for d in data[30:]] # skip header + non-attack period

# in KiB/s
def getTxBW(data): return [(d[3]/1024.0)/d[1] for d in data[30:]] # skip header + non-attack period

# KiB/s
def getNewRam(data):
    period = data[1:][0][1] # 60
    ram = [(d[4]/1024.0) for d in data[30:]] # skip header + non-attack period
    newram = []
    last = ram[0]
    for r in ram:
        val = r - last
        newram.append(val/period if val > 0 else 0)
        last = r
    return newram

def getExperiments():
    experiments = []
    with open(DIRLIST, 'r') as f:
        for line in f: experiments.append(line.strip())
    return experiments

def getWeights():
    # get the current XML file
    parser = etree.XMLParser(remove_blank_text=True)
    tree = etree.parse(HOSTS, parser)
    root = tree.getroot()
    # get list of relays and consensus weights
    weights = {}
    for n in root.iter('node'):
        id = n.attrib['id']
        if 'relay' in id or 'uthority' in id:
            for a in n.iter('application'):
                if 'scallion' in a.attrib['plugin']:
                    args = a.attrib['arguments'].split()
                    weights[id] = int(args[1])
    return weights

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
