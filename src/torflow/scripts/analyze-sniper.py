#!/usr/bin/python

import sys, os, json, gzip, pylab
from numpy import mean, sqrt, floor, array
from scipy import stats
from lxml import etree

HOSTS='hosts.xml'
DIRLIST='dirlist'
TORGROUPS='top_relays_bws.2013-06-30-23.json.gz'
RAMSIZES=[1,2,4,8,16] # GiB
OUTDIR='graphs'

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
    weights = getWeights(HOSTS)
    experiments = getExperiments(DIRLIST)

    ramrates = {}
    for e in experiments:
        parts = e.split('-')
        mode, name = parts[3], parts[4]
        if name not in ramrates: ramrates[name] = {'tunnel':0.0,'direct':0.0}
        ramrates[name][mode] = getMeanRamConsumptionRate(e, name)

    ## mem/s compared to bandwidth weight, linear regression
    # r_value is the correlation coefficient and p_value is the p-value for a hypothesis test whose null hypothesis is that the slope is zero.

    linregdata = {'direct':{}, 'tunnel':{}}

    ### direct ###

    d = linregdata['direct']
    d['x'], d['y'] = getRamWeightScatter(ramrates, weights, 'direct')
    d['x'] = array(d['x'])
    d['slope'], d['intercept'], d['r_value'], d['p_value'], d['std_dev'] = stats.linregress(d['x'], d['y'])
    d['line'] = d['slope'] * d['x'] + d['intercept']
    d['mean_squared_err'] = sqrt(sum((d['line'] - d['y'])**2) / len(d['y']))

    ### tunnel ###

    d = linregdata['tunnel']
    d['x'], d['y'] = getRamWeightScatter(ramrates, weights, 'tunnel')
    d['x'] = array(d['x'])
    d['slope'], d['intercept'], d['r_value'], d['p_value'], d['std_dev'] = stats.linregress(d['x'], d['y'])
    d['line'] = d['slope'] * d['x'] + d['intercept']
    d['mean_squared_err'] = sqrt(sum((d['line'] - d['y'])**2) / len(d['y']))

    ### print results ###

    print linregToString(linregdata, 'direct')
    print linregToString(linregdata, 'tunnel')
    print "\n"

    ### plot results ###

    pylab.figure()

    pylab.plot(linregdata['direct']['x'], linregdata['direct']['line'], 'k-', linregdata['direct']['x'], linregdata['direct']['y'], 'ro', label='direct')
    pylab.plot(linregdata['tunnel']['x'], linregdata['tunnel']['line'], 'g-', linregdata['tunnel']['x'], linregdata['tunnel']['y'], 'bs', label='tunnel')

    pylab.title("Correlation of Consensus Weight to Mean RAM Consumption", fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Mean Target Ram Consumption Rate (KiB/s)")
    pylab.ylabel("Consensus Weight")
    pylab.xlim(xmin=0.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="lower right")
    if not os.path.exists(OUTDIR): os.makedirs(OUTDIR)
    pylab.savefig("{0}/direct.weight.ram.pdf".format(OUTDIR))

    ### extend to live tor relays ###

    torgroups = getContents(TORGROUPS)
    for k in torgroups:
        if isinstance(torgroups[k][0], (int, long)): torgroups[k] = [torgroups[k]]

    for k in torgroups:
        print "========== relay group '{0}'".format(k)
        directsnipetimes, directprob = computeSnipeTime(torgroups[k], linregdata['direct'])
        tunnelsnipetimes, tunnelprob = computeSnipeTime(torgroups[k], linregdata['tunnel'])
        print "----------"
        print "direct, group {0}: H:M={1} perc={2}".format(k, formatTime(directsnipetimes), directprob*100)
        print "tunnel, group {0}: H:M={1} perc={2}".format(k, formatTime(tunnelsnipetimes), tunnelprob*100)
        print "==========\n"
        
# hours
def computeSnipeTime(group, linregd):
    snipetimes, totalprob = {}, 0.0
    minobsv = min(linregd['x'])
    #print >>sys.stderr, "minimum observed rate is {0}".format(minobsv)
    for relay in group:
        # y=mx+b --> x=(y-b)/m
        weight, prob = relay[0], relay[1]
        totalprob += prob if prob is not None else 0.0

        y = weight * 1000.0 # shadow generate script does this, so our weights match
        rate = float((y-linregd['intercept'])/linregd['slope'])
        if rate < 0:
            #print >>sys.stderr, "replacing negative rate {0} with minimum observed rate {1} for weight {2}".format(rate, minobsv, weight)
            rate = minobsv
        #else: print "yay got {0} for {1}".format(rate, relay)
        assert(rate > 0)

        for ramgib in RAMSIZES:
            ramkib = float(ramgib*1024.0*1024.0)
            seconds = ramkib/rate
            hours = seconds / 3600.0
            if ramgib not in snipetimes: snipetimes[ramgib] = 0.0
            snipetimes[ramgib] += hours

        print "rate={0} weight={1} prob={2} H:M={3}".format(int(round(rate)), weight, formatProb(prob), formatTime(snipetimes))

    return snipetimes, totalprob

def formatProb(prob): return "{0:.2e}".format(prob) if prob is not None else "None"

def formatTime(snipetimes):
    time = {}
    for gb in snipetimes:
        hours = snipetimes[gb]
        h = int(floor(hours))
        m = int(round((hours - h) * 60.0))
        time[gb] = "{0}:{1}".format(h, m)
    return ["{0}: {1}".format(gb, time[gb]) for gb in sorted(time.keys())]

def linregToString(linregdata, key):
    d = linregdata[key]
    return "linear regression, {6}: slope={0} intercept={1} r_value={2} p_value={3} std_dev={4} mean_squared_err={5}".format(d['slope'], d['intercept'], d['r_value'], d['p_value'], d['std_dev'], d['mean_squared_err'], key)

def getRamWeightScatter(rates, weights, key):
    data = [(rates[n][key], weights[n]) for n in rates]
    x, y = [], []
    for d in data:
        if d[0] != 0:
            x.append(d[0])
            y.append(d[1])
    return x, y

def getMeanRamConsumptionRate(dirname, name):
    fname = "{0}/{1}-ram.dat.gz".format(dirname, name)
    if not os.path.exists(fname):
        print >> sys.stderr, "WARNING: cant find file path '{0}'".format(fname)
        return 0.0
    ramstats = getContents(fname)
    newram = getNewRam(ramstats)
    ram = []
    for nr in newram:
        # attack is active when ram is above 10 kib/s
        if nr > 10.0: ram.append(nr)
    return mean(ram)

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

def getExperiments(dirlistFilename):
    experiments = []
    with open(dirlistFilename, 'r') as f:
        for line in f: experiments.append(line.strip())
    return experiments

def getWeights(hostsFilename):
    # get the current XML file
    parser = etree.XMLParser(remove_blank_text=True)
    tree = etree.parse(hostsFilename, parser)
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

if __name__ == '__main__':
    main()
