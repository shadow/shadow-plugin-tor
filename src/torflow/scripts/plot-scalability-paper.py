#!/usr/bin/python

import sys, os, json, gzip, pylab
from math import ceil

OUTDIR="graphs"
if not os.path.exists(OUTDIR): os.makedirs(OUTDIR)

pylab.rcParams.update({
    'backend': 'PDF',
    'font.size': 16,
    'figure.figsize': (4,3),
    'figure.dpi': 100.0,
    'figure.subplot.left': 0.18,
    'figure.subplot.right': 0.96,
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
    'legend.labelspacing' : .75,
    'legend.markerscale' : 1.0,
    'ps.useafm' : True,
    'pdf.use14corefonts' : True,
    'text.usetex' : True,
})

def main():

    label_1set5, lineformat_1set5, = "1 team\n5 circs", "k-"
    directTargetRam_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/relayguard1-ram.dat.gz")
    directTargetBW_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/relayguard1-node.dat.gz")
    directSniperRam_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/sniper-ram.dat.gz")
    directSniperBW_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/sniper-node.dat.gz")
    tunnelTargetRam_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/relayguard1-ram.dat.gz")
    tunnelTargetBW_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/relayguard1-node.dat.gz")
    tunnelSniperRam_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/sniper-ram.dat.gz")
    tunnelSniperBW_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/sniper-node.dat.gz")

    label_1set10, lineformat_1set10, = "1 team\n10 circs", "b:"
    directTargetRam_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/relayguard1-ram.dat.gz")
    directTargetBW_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/relayguard1-node.dat.gz")
    directSniperRam_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/sniper-ram.dat.gz")
    directSniperBW_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/sniper-node.dat.gz")
    tunnelTargetRam_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/relayguard1-ram.dat.gz")
    tunnelTargetBW_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/relayguard1-node.dat.gz")
    tunnelSniperRam_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/sniper-ram.dat.gz")
    tunnelSniperBW_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/sniper-node.dat.gz")

    label_5set10, lineformat_5set10, = "5 teams\n50 circs", "g-."
    directTargetRam_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/relayguard1-ram.dat.gz")
    directTargetBW_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/relayguard1-node.dat.gz")
    directSniperRam_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/sniper-ram.dat.gz")
    directSniperBW_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/sniper-node.dat.gz")
    tunnelTargetRam_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/relayguard1-ram.dat.gz")
    tunnelTargetBW_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/relayguard1-node.dat.gz")
    tunnelSniperRam_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/sniper-ram.dat.gz")
    tunnelSniperBW_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/sniper-node.dat.gz")

    label_10set10, lineformat_10set10, = "10 teams\n100 circs", "r--"
    directTargetRam_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/relayguard1-ram.dat.gz")
    directTargetBW_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/relayguard1-node.dat.gz")
    directSniperRam_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/sniper-ram.dat.gz")
    directSniperBW_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/sniper-node.dat.gz")
    tunnelTargetRam_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/relayguard1-ram.dat.gz")
    tunnelTargetBW_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/relayguard1-node.dat.gz")
    tunnelSniperRam_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/sniper-ram.dat.gz")
    tunnelSniperBW_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/sniper-node.dat.gz")

    noattack = getContents("large-64GB-m2.4xlarge-relayguard1-vanilla/relayguard1-ram.dat.gz")

    ## target memory consumed over time ##

    pylab.figure(figsize=(4,3))

    pylab.plot(getTime(directTargetRam_10set10), getRam(directTargetRam_10set10), lineformat_10set10, label=label_10set10)
    pylab.plot(getTime(directTargetRam_5set10), getRam(directTargetRam_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(directTargetRam_1set10), getRam(directTargetRam_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(directTargetRam_1set5), getRam(directTargetRam_1set5), lineformat_1set5, label=label_1set5, alpha=0.4)

    pylab.plot(getTime(noattack), getRam(noattack), 'k-', label='no attack')

    pylab.axvspan(30, 60, facecolor='k', alpha=0.3)
    #pylab.title("Target RAM Usage, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM Consumed (MiB)")
    pylab.xlim(xmin=20)
    pylab.ylim(ymax=2600)
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/direct.target.ram.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(tunnelTargetRam_10set10), getRam(tunnelTargetRam_10set10), lineformat_10set10, label=label_10set10)
    pylab.plot(getTime(tunnelTargetRam_5set10), getRam(tunnelTargetRam_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(tunnelTargetRam_1set10), getRam(tunnelTargetRam_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(tunnelTargetRam_1set5), getRam(tunnelTargetRam_1set5), lineformat_1set5, label=label_1set5, alpha=0.4)

    pylab.plot(getTime(noattack), getRam(noattack), 'k-', label='no attack')

    pylab.axvspan(30, 60, facecolor='k', alpha=0.3)
    #pylab.title("Target RAM Usage, N sets of M sybils, Tunneled", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM Consumed (MiB)")
    pylab.xlim(xmin=20)
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/tunnel.target.ram.time.pdf".format(OUTDIR))

    ## subplot combined version ##

    pylab.rcParams.update({'figure.subplot.hspace' : 0.13})

    fig = pylab.figure()
    ax = fig.add_subplot(111) # the big subplot
    ax2 = fig.add_subplot(212)
    ax1 = fig.add_subplot(211, sharex=ax2, sharey=ax2)

    # turn off axis lines and ticks on big subplot
    ax.spines['top'].set_color('none')
    ax.spines['bottom'].set_color('none')
    ax.spines['left'].set_color('none')
    ax.spines['right'].set_color('none')
    ax.grid(b=False)
    ax.tick_params(labelcolor='w', top='off', bottom='off', left='off', right='off')

    # direct data subplot
    ax1.plot(getTime(directTargetRam_10set10), getRam(directTargetRam_10set10), lineformat_10set10, label=label_10set10)
    ax1.plot(getTime(directTargetRam_5set10), getRam(directTargetRam_5set10), lineformat_5set10, label=label_5set10)
    ax1.plot(getTime(directTargetRam_1set10), getRam(directTargetRam_1set10), lineformat_1set10, label=label_1set10)
    ax1.plot(getTime(directTargetRam_1set5), getRam(directTargetRam_1set5), lineformat_1set5, label=label_1set5, alpha=0.4)
    ax1.plot(getTime(noattack), getRam(noattack), 'k-', label='no attack')

    ax1.axvspan(30, 60, facecolor='k', alpha=0.3)
    #ax1.set_title("direct", x='1.0', fontsize="x-small", ha='right')#, fontsize="x-small", x='1.0', ha='right')
    ax1.text(61.25, 1250, 'direct', rotation=270, fontsize="x-small", ha='center', va='center')

    pylab.setp(ax1.get_xticklabels(), visible=False)
    ax1.legend(loc='upper left')

    # tunnel data subplot
    ax2.plot(getTime(tunnelTargetRam_10set10), getRam(tunnelTargetRam_10set10), lineformat_10set10, label=label_10set10)
    ax2.plot(getTime(tunnelTargetRam_5set10), getRam(tunnelTargetRam_5set10), lineformat_5set10, label=label_5set10)
    ax2.plot(getTime(tunnelTargetRam_1set10), getRam(tunnelTargetRam_1set10), lineformat_1set10, label=label_1set10)
    ax2.plot(getTime(tunnelTargetRam_1set5), getRam(tunnelTargetRam_1set5), lineformat_1set5, label=label_1set5, alpha=0.4)
    ax2.plot(getTime(noattack), getRam(noattack), 'k-', label='no attack')

    ax2.axvspan(30, 60, facecolor='k', alpha=0.3)
    ax2.set_xlim(xmin=20)
    ax2.text(60.875, 1250, 'anonymous', rotation=270, fontsize="x-small", ha='center', va='center')

    # common labels
    #pylab.title("Target RAM Usage, N sets of M sybils, Tunneled", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    ax.set_xlabel("Time (m)")
    ax.set_ylabel("RAM Consumed (MiB)")
    ax.yaxis.labelpad = 15

    pylab.savefig("{0}/combined.target.ram.time.pdf".format(OUTDIR))

# KiB/s
def getNewRam(data):
    period = data[1:][0][1] # 60
    ram = [(d[4]/1024.0) for d in data[1:]] # skip header
    newram = []
    last = ram[0]
    for r in ram:
        val = r - last
        newram.append(val/period if val > 0 else 0)
        last = r
    return newram

# in seconds
def getTime(data): return [ceil(d[0]/60.0) for d in data[1:]] # skip header

# in KiB/s
def getTotalBW(data):
    rx, tx = getRxBW(data), getTxBW(data)
    return [rx[i]+tx[i] for i in xrange(len(rx))]

# in KiB/s
def getRxBW(data): return [(d[2]/1024.0)/d[1] for d in data[1:]] # skip header

# in KiB/s
def getTxBW(data): return [(d[3]/1024.0)/d[1] for d in data[1:]] # skip header

# in MiB
def getRam(data): return [d[4]/(1024.0*1024.0) for d in data[1:]] # skip header

def getContents(filename):
    data = None
    with gzip.open(filename, 'r') as f: data = json.load(f)
    return data

# helper - set axis in scientific format
def setsciformat(setx=True, sety=True):
    # Scientific notation is used for data < 10^-n or data >= 10^m, 
    # where n and m are the power limits set using set_powerlimits((n,m)).
    sciform = pylab.ScalarFormatter(useMathText=True)
    sciform.set_scientific(True)
    sciform.set_powerlimits((-3, 3))
    if setx: pylab.gca().xaxis.set_major_formatter(sciform)
    if sety: pylab.gca().xaxis.set_major_formatter(sciform)

if __name__ == '__main__':
    main()
