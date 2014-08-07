#!/usr/bin/python

import sys, os, json, gzip, pylab

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
    'lines.markersize' : 10,
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
    label_1set2, lineformat_1set2, = "N=1,M=2", "b-"
    directTargetRam_1set2 = getContents("large-64GB-snipetor-direct-relayguard1-1set2/stats/relayguard1-ram.dat.gz")
    directTargetBW_1set2 = getContents("large-64GB-snipetor-direct-relayguard1-1set2/stats/relayguard1-node.dat.gz")
    directSniperRam_1set2 = getContents("large-64GB-snipetor-direct-relayguard1-1set2/stats/sniper-ram.dat.gz")
    directSniperBW_1set2 = getContents("large-64GB-snipetor-direct-relayguard1-1set2/stats/sniper-node.dat.gz")
    tunnelTargetRam_1set2 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set2/stats/relayguard1-ram.dat.gz")
    tunnelTargetBW_1set2 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set2/stats/relayguard1-node.dat.gz")
    tunnelSniperRam_1set2 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set2/stats/sniper-ram.dat.gz")
    tunnelSniperBW_1set2 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set2/stats/sniper-node.dat.gz")

    label_1set5, lineformat_1set5, = "N=1,M=5", "k-"
    directTargetRam_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/stats/relayguard1-ram.dat.gz")
    directTargetBW_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/stats/relayguard1-node.dat.gz")
    directSniperRam_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/stats/sniper-ram.dat.gz")
    directSniperBW_1set5 = getContents("large-64GB-snipetor-direct-relayguard1-1set5/stats/sniper-node.dat.gz")
    tunnelTargetRam_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/stats/relayguard1-ram.dat.gz")
    tunnelTargetBW_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/stats/relayguard1-node.dat.gz")
    tunnelSniperRam_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/stats/sniper-ram.dat.gz")
    tunnelSniperBW_1set5 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set5/stats/sniper-node.dat.gz")

    label_1set10, lineformat_1set10, = "N=1,M=10", "g:"
    directTargetRam_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/stats/relayguard1-ram.dat.gz")
    directTargetBW_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/stats/relayguard1-node.dat.gz")
    directSniperRam_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/stats/sniper-ram.dat.gz")
    directSniperBW_1set10 = getContents("large-64GB-snipetor-direct-relayguard1-1set10/stats/sniper-node.dat.gz")
    tunnelTargetRam_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/stats/relayguard1-ram.dat.gz")
    tunnelTargetBW_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/stats/relayguard1-node.dat.gz")
    tunnelSniperRam_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/stats/sniper-ram.dat.gz")
    tunnelSniperBW_1set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set10/stats/sniper-node.dat.gz")

    label_1set20, lineformat_1set20, = "N=1,M=20", "g-."
    directTargetRam_1set20 = getContents("large-64GB-snipetor-direct-relayguard1-1set20/stats/relayguard1-ram.dat.gz")
    directTargetBW_1set20 = getContents("large-64GB-snipetor-direct-relayguard1-1set20/stats/relayguard1-node.dat.gz")
    directSniperRam_1set20 = getContents("large-64GB-snipetor-direct-relayguard1-1set20/stats/sniper-ram.dat.gz")
    directSniperBW_1set20 = getContents("large-64GB-snipetor-direct-relayguard1-1set20/stats/sniper-node.dat.gz")
    tunnelTargetRam_1set20 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set20/stats/relayguard1-ram.dat.gz")
    tunnelTargetBW_1set20 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set20/stats/relayguard1-node.dat.gz")
    tunnelSniperRam_1set20 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set20/stats/sniper-ram.dat.gz")
    tunnelSniperBW_1set20 = getContents("large-64GB-snipetor-tunnel-relayguard1-1set20/stats/sniper-node.dat.gz")

    label_2set10, lineformat_2set10, = "N=2,M=10", "k:"
    directTargetRam_2set10 = getContents("large-64GB-snipetor-direct-relayguard1-2set10/stats/relayguard1-ram.dat.gz")
    directTargetBW_2set10 = getContents("large-64GB-snipetor-direct-relayguard1-2set10/stats/relayguard1-node.dat.gz")
    directSniperRam_2set10 = getContents("large-64GB-snipetor-direct-relayguard1-2set10/stats/sniper-ram.dat.gz")
    directSniperBW_2set10 = getContents("large-64GB-snipetor-direct-relayguard1-2set10/stats/sniper-node.dat.gz")
    tunnelTargetRam_2set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-2set10/stats/relayguard1-ram.dat.gz")
    tunnelTargetBW_2set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-2set10/stats/relayguard1-node.dat.gz")
    tunnelSniperRam_2set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-2set10/stats/sniper-ram.dat.gz")
    tunnelSniperBW_2set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-2set10/stats/sniper-node.dat.gz")

    label_5set10, lineformat_5set10, = "N=5,M=10", "b-."
    directTargetRam_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/stats/relayguard1-ram.dat.gz")
    directTargetBW_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/stats/relayguard1-node.dat.gz")
    directSniperRam_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/stats/sniper-ram.dat.gz")
    directSniperBW_5set10 = getContents("large-64GB-snipetor-direct-relayguard1-5set10/stats/sniper-node.dat.gz")
    tunnelTargetRam_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/stats/relayguard1-ram.dat.gz")
    tunnelTargetBW_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/stats/relayguard1-node.dat.gz")
    tunnelSniperRam_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/stats/sniper-ram.dat.gz")
    tunnelSniperBW_5set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-5set10/stats/sniper-node.dat.gz")

    label_10set10, lineformat_10set10, = "N=10,M=10", "r--"
    directTargetRam_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/stats/relayguard1-ram.dat.gz")
    directTargetBW_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/stats/relayguard1-node.dat.gz")
    directSniperRam_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/stats/sniper-ram.dat.gz")
    directSniperBW_10set10 = getContents("large-64GB-snipetor-direct-relayguard1-10set10/stats/sniper-node.dat.gz")
    tunnelTargetRam_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/stats/relayguard1-ram.dat.gz")
    tunnelTargetBW_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/stats/relayguard1-node.dat.gz")
    tunnelSniperRam_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/stats/sniper-ram.dat.gz")
    tunnelSniperBW_10set10 = getContents("large-64GB-snipetor-tunnel-relayguard1-10set10/stats/sniper-node.dat.gz")


    ## target memory consumed over time ##

    pylab.figure(figsize=(4,3))

    pylab.plot(getTime(directTargetRam_1set2), getRam(directTargetRam_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(directTargetRam_1set5), getRam(directTargetRam_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(directTargetRam_1set10), getRam(directTargetRam_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(directTargetRam_1set20), getRam(directTargetRam_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(directTargetRam_2set10), getRam(directTargetRam_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(directTargetRam_5set10), getRam(directTargetRam_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(directTargetRam_10set10), getRam(directTargetRam_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Target RAM Usage, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM Consumed (MiB)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/direct.target.ram.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(tunnelTargetRam_1set2), getRam(tunnelTargetRam_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(tunnelTargetRam_1set5), getRam(tunnelTargetRam_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(tunnelTargetRam_1set10), getRam(tunnelTargetRam_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(tunnelTargetRam_1set20), getRam(tunnelTargetRam_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(tunnelTargetRam_2set10), getRam(tunnelTargetRam_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(tunnelTargetRam_5set10), getRam(tunnelTargetRam_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(tunnelTargetRam_10set10), getRam(tunnelTargetRam_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Target RAM Usage, N sets of M sybils, Tunneled", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM Consumed (MiB)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/tunnel.target.ram.time.pdf".format(OUTDIR))

    ## sniper memory consumed over time ##

    pylab.figure()

    pylab.plot(getTime(directSniperRam_1set2), getRam(directSniperRam_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(directSniperRam_1set5), getRam(directSniperRam_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(directSniperRam_1set10), getRam(directSniperRam_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(directSniperRam_1set20), getRam(directSniperRam_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(directSniperRam_2set10), getRam(directSniperRam_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(directSniperRam_5set10), getRam(directSniperRam_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(directSniperRam_10set10), getRam(directSniperRam_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper RAM Usage, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM Consumed (MiB)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/direct.sniper.ram.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(tunnelSniperRam_1set2), getRam(tunnelSniperRam_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(tunnelSniperRam_1set5), getRam(tunnelSniperRam_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(tunnelSniperRam_1set10), getRam(tunnelSniperRam_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(tunnelSniperRam_1set20), getRam(tunnelSniperRam_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(tunnelSniperRam_2set10), getRam(tunnelSniperRam_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(tunnelSniperRam_5set10), getRam(tunnelSniperRam_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(tunnelSniperRam_10set10), getRam(tunnelSniperRam_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper RAM Usage, N sets of M sybils, Tunneled", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM Consumed (MiB)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/tunnel.sniper.ram.time.pdf".format(OUTDIR))

    ## sniper bandwidth consumed over time ##

    pylab.figure()

    pylab.plot(getTime(directSniperBW_1set2), getTxBW(directSniperBW_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(directSniperBW_1set5), getTxBW(directSniperBW_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(directSniperBW_1set10), getTxBW(directSniperBW_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(directSniperBW_1set20), getTxBW(directSniperBW_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(directSniperBW_2set10), getTxBW(directSniperBW_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(directSniperBW_5set10), getTxBW(directSniperBW_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(directSniperBW_10set10), getTxBW(directSniperBW_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper BW Up, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("Transmit Bandwidth (KiB/s)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/direct.sniper.bwtx.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(tunnelSniperBW_1set2), getTxBW(tunnelSniperBW_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(tunnelSniperBW_1set5), getTxBW(tunnelSniperBW_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(tunnelSniperBW_1set10), getTxBW(tunnelSniperBW_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(tunnelSniperBW_1set20), getTxBW(tunnelSniperBW_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(tunnelSniperBW_2set10), getTxBW(tunnelSniperBW_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(tunnelSniperBW_5set10), getTxBW(tunnelSniperBW_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(tunnelSniperBW_10set10), getTxBW(tunnelSniperBW_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper BW Up, N sets of M sybils, Tunneled", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("Transmit Bandwidth (KiB/s)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/tunnel.sniper.bwtx.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(directSniperBW_1set2), getRxBW(directSniperBW_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(directSniperBW_1set5), getRxBW(directSniperBW_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(directSniperBW_1set10), getRxBW(directSniperBW_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(directSniperBW_1set20), getRxBW(directSniperBW_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(directSniperBW_2set10), getRxBW(directSniperBW_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(directSniperBW_5set10), getRxBW(directSniperBW_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(directSniperBW_10set10), getRxBW(directSniperBW_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper BW Down, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("Receive Bandwidth (KiB/s)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/direct.sniper.bwrx.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(tunnelSniperBW_1set2), getRxBW(tunnelSniperBW_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(tunnelSniperBW_1set5), getRxBW(tunnelSniperBW_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(tunnelSniperBW_1set10), getRxBW(tunnelSniperBW_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(tunnelSniperBW_1set20), getRxBW(tunnelSniperBW_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(tunnelSniperBW_2set10), getRxBW(tunnelSniperBW_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(tunnelSniperBW_5set10), getRxBW(tunnelSniperBW_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(tunnelSniperBW_10set10), getRxBW(tunnelSniperBW_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper BW Down, N sets of M sybils, Tunneled", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("Receive Bandwidth (KiB/s)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/tunnel.sniper.bwrx.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(directSniperBW_1set2), getTotalBW(directSniperBW_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(directSniperBW_1set5), getTotalBW(directSniperBW_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(directSniperBW_1set10), getTotalBW(directSniperBW_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(directSniperBW_1set20), getTotalBW(directSniperBW_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(directSniperBW_2set10), getTotalBW(directSniperBW_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(directSniperBW_5set10), getTotalBW(directSniperBW_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(directSniperBW_10set10), getTotalBW(directSniperBW_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper BW Down, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("Total Bandwidth (KiB/s)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/direct.sniper.bw.time.pdf".format(OUTDIR))

    pylab.figure()

    pylab.plot(getTime(tunnelSniperBW_1set2), getTotalBW(tunnelSniperBW_1set2), lineformat_1set2, label=label_1set2)
    pylab.plot(getTime(tunnelSniperBW_1set5), getTotalBW(tunnelSniperBW_1set5), lineformat_1set5, label=label_1set5)
    pylab.plot(getTime(tunnelSniperBW_1set10), getTotalBW(tunnelSniperBW_1set10), lineformat_1set10, label=label_1set10)
    pylab.plot(getTime(tunnelSniperBW_1set20), getTotalBW(tunnelSniperBW_1set20), lineformat_1set20, label=label_1set20)
    pylab.plot(getTime(tunnelSniperBW_2set10), getTotalBW(tunnelSniperBW_2set10), lineformat_2set10, label=label_2set10)
    pylab.plot(getTime(tunnelSniperBW_5set10), getTotalBW(tunnelSniperBW_5set10), lineformat_5set10, label=label_5set10)
    pylab.plot(getTime(tunnelSniperBW_10set10), getTotalBW(tunnelSniperBW_10set10), lineformat_10set10, label=label_10set10)

#    pylab.axvspan(20, 30, facecolor='b', alpha=0.2)
    pylab.axvspan(30, 60, facecolor='k', alpha=0.4)
    pylab.title("Sniper BW Down, N sets of M sybils, Tunneled", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("Total Bandwidth (KiB/s)")
    pylab.legend(loc="upper left")
    pylab.savefig("{0}/tunnel.sniper.bw.time.pdf".format(OUTDIR))

    ## sniper bw/s, target ram/s ##

    pylab.figure()

    pylab.scatter(getRxBW(directSniperBW_1set2), getNewRam(directTargetRam_1set2), marker='o', s=10, c='w', edgecolor=lineformat_1set2[0], label=label_1set2)
    pylab.scatter(getRxBW(directSniperBW_1set5), getNewRam(directTargetRam_1set5), marker='^', s=10, c='w', edgecolor=lineformat_1set5[0], label=label_1set5)
    pylab.scatter(getRxBW(directSniperBW_1set10), getNewRam(directTargetRam_1set10), marker='s', s=10, c='w', edgecolor=lineformat_1set10[0], label=label_1set10)
    pylab.scatter(getRxBW(directSniperBW_1set20), getNewRam(directTargetRam_1set20), marker='p', s=10, c='w', edgecolor=lineformat_1set20[0], label=label_1set20)
    pylab.scatter(getRxBW(directSniperBW_2set10), getNewRam(directTargetRam_2set10), marker='h', s=10, c='w', edgecolor=lineformat_2set10[0], label=label_2set10)
    pylab.scatter(getRxBW(directSniperBW_5set10), getNewRam(directTargetRam_5set10), marker='8', s=10, c='w', edgecolor=lineformat_5set10[0], label=label_5set10)
    pylab.scatter(getRxBW(directSniperBW_10set10), getNewRam(directTargetRam_10set10), marker='*', s=10, c='w', edgecolor=lineformat_10set10[0], label=label_10set10)

    pylab.title("BW vs RAM, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Sniper Receive Bandwidth (KiB/s)")
    pylab.ylabel("Target RAM Consumption Rate (KiB/s)")
    pylab.xlim(xmin=0.0, xmax=250.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="upper right")
    pylab.savefig("{0}/direct.bwrx.ram.pdf".format(OUTDIR))

    pylab.figure()

    pylab.scatter(getTxBW(directSniperBW_1set2), getNewRam(directTargetRam_1set2), marker='o', s=10, c='w', edgecolor=lineformat_1set2[0], label=label_1set2)
    pylab.scatter(getTxBW(directSniperBW_1set5), getNewRam(directTargetRam_1set5), marker='^', s=10, c='w', edgecolor=lineformat_1set5[0], label=label_1set5)
    pylab.scatter(getTxBW(directSniperBW_1set10), getNewRam(directTargetRam_1set10), marker='s', s=10, c='w', edgecolor=lineformat_1set10[0], label=label_1set10)
    pylab.scatter(getTxBW(directSniperBW_1set20), getNewRam(directTargetRam_1set20), marker='p', s=10, c='w', edgecolor=lineformat_1set20[0], label=label_1set20)
    pylab.scatter(getTxBW(directSniperBW_2set10), getNewRam(directTargetRam_2set10), marker='h', s=10, c='w', edgecolor=lineformat_2set10[0], label=label_2set10)
    pylab.scatter(getTxBW(directSniperBW_5set10), getNewRam(directTargetRam_5set10), marker='8', s=10, c='w', edgecolor=lineformat_5set10[0], label=label_5set10)
    pylab.scatter(getTxBW(directSniperBW_10set10), getNewRam(directTargetRam_10set10), marker='*', s=10, c='w', edgecolor=lineformat_10set10[0], label=label_10set10)

    pylab.title("BW vs RAM, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Sniper Transmit Bandwidth (KiB/s)")
    pylab.ylabel("Target RAM Consumption Rate (KiB/s)")
    pylab.xlim(xmin=0.0, xmax=250.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="upper right")
    pylab.savefig("{0}/direct.bwtx.ram.pdf".format(OUTDIR))

    pylab.figure()

    pylab.scatter(getTotalBW(directSniperBW_1set2), getNewRam(directTargetRam_1set2), marker='o', s=10, c='w', edgecolor=lineformat_1set2[0], label=label_1set2)
    pylab.scatter(getTotalBW(directSniperBW_1set5), getNewRam(directTargetRam_1set5), marker='^', s=10, c='w', edgecolor=lineformat_1set5[0], label=label_1set5)
    pylab.scatter(getTotalBW(directSniperBW_1set10), getNewRam(directTargetRam_1set10), marker='s', s=10, c='w', edgecolor=lineformat_1set10[0], label=label_1set10)
    pylab.scatter(getTotalBW(directSniperBW_1set20), getNewRam(directTargetRam_1set20), marker='p', s=10, c='w', edgecolor=lineformat_1set20[0], label=label_1set20)
    pylab.scatter(getTotalBW(directSniperBW_2set10), getNewRam(directTargetRam_2set10), marker='h', s=10, c='w', edgecolor=lineformat_2set10[0], label=label_2set10)
    pylab.scatter(getTotalBW(directSniperBW_5set10), getNewRam(directTargetRam_5set10), marker='8', s=10, c='w', edgecolor=lineformat_5set10[0], label=label_5set10)
    pylab.scatter(getTotalBW(directSniperBW_10set10), getNewRam(directTargetRam_10set10), marker='*', s=10, c='w', edgecolor=lineformat_10set10[0], label=label_10set10)

    pylab.title("BW vs RAM, N sets of M sybils, Direct", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Sniper Total Bandwidth (KiB/s)")
    pylab.ylabel("Target RAM Consumption Rate (KiB/s)")
    pylab.xlim(xmin=0.0, xmax=250.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="upper right")
    pylab.savefig("{0}/direct.bw.ram.pdf".format(OUTDIR))

    pylab.figure()

    pylab.scatter(getRxBW(tunnelSniperBW_1set2), getNewRam(tunnelTargetRam_1set2), marker='o', s=10, c='w', edgecolor=lineformat_1set2[0], label=label_1set2)
    pylab.scatter(getRxBW(tunnelSniperBW_1set5), getNewRam(tunnelTargetRam_1set5), marker='^', s=10, c='w', edgecolor=lineformat_1set5[0], label=label_1set5)
    pylab.scatter(getRxBW(tunnelSniperBW_1set10), getNewRam(tunnelTargetRam_1set10), marker='s', s=10, c='w', edgecolor=lineformat_1set10[0], label=label_1set10)
    pylab.scatter(getRxBW(tunnelSniperBW_1set20), getNewRam(tunnelTargetRam_1set20), marker='p', s=10, c='w', edgecolor=lineformat_1set20[0], label=label_1set20)
    pylab.scatter(getRxBW(tunnelSniperBW_2set10), getNewRam(tunnelTargetRam_2set10), marker='h', s=10, c='w', edgecolor=lineformat_2set10[0], label=label_2set10)
    pylab.scatter(getRxBW(tunnelSniperBW_5set10), getNewRam(tunnelTargetRam_5set10), marker='8', s=10, c='w', edgecolor=lineformat_5set10[0], label=label_5set10)
    pylab.scatter(getRxBW(tunnelSniperBW_10set10), getNewRam(tunnelTargetRam_10set10), marker='*', s=10, c='w', edgecolor=lineformat_10set10[0], label=label_10set10)

    pylab.title("BW vs RAM, N sets of M sybils, Tunnel", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Sniper Receive Bandwidth (KiB/s)")
    pylab.ylabel("Target RAM Consumption Rate (KiB/s)")
    pylab.xlim(xmin=0.0, xmax=250.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="upper right")
    pylab.savefig("{0}/tunnel.bwrx.ram.pdf".format(OUTDIR))

    pylab.figure()

    pylab.scatter(getTxBW(tunnelSniperBW_1set2), getNewRam(tunnelTargetRam_1set2), marker='o', s=10, c='w', edgecolor=lineformat_1set2[0], label=label_1set2)
    pylab.scatter(getTxBW(tunnelSniperBW_1set5), getNewRam(tunnelTargetRam_1set5), marker='^', s=10, c='w', edgecolor=lineformat_1set5[0], label=label_1set5)
    pylab.scatter(getTxBW(tunnelSniperBW_1set10), getNewRam(tunnelTargetRam_1set10), marker='s', s=10, c='w', edgecolor=lineformat_1set10[0], label=label_1set10)
    pylab.scatter(getTxBW(tunnelSniperBW_1set20), getNewRam(tunnelTargetRam_1set20), marker='p', s=10, c='w', edgecolor=lineformat_1set20[0], label=label_1set20)
    pylab.scatter(getTxBW(tunnelSniperBW_2set10), getNewRam(tunnelTargetRam_2set10), marker='h', s=10, c='w', edgecolor=lineformat_2set10[0], label=label_2set10)
    pylab.scatter(getTxBW(tunnelSniperBW_5set10), getNewRam(tunnelTargetRam_5set10), marker='8', s=10, c='w', edgecolor=lineformat_5set10[0], label=label_5set10)
    pylab.scatter(getTxBW(tunnelSniperBW_10set10), getNewRam(tunnelTargetRam_10set10), marker='*', s=10, c='w', edgecolor=lineformat_10set10[0], label=label_10set10)

    pylab.title("BW vs RAM, N sets of M sybils, Tunnel", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Sniper Transmit Bandwidth (KiB/s)")
    pylab.ylabel("Target RAM Consumption Rate (KiB/s)")
    pylab.xlim(xmin=0.0, xmax=250.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="upper right")
    pylab.savefig("{0}/tunnel.bwtx.ram.pdf".format(OUTDIR))

    pylab.figure()

    pylab.scatter(getTotalBW(tunnelSniperBW_1set2), getNewRam(tunnelTargetRam_1set2), marker='o', s=10, c='w', edgecolor=lineformat_1set2[0], label=label_1set2)
    pylab.scatter(getTotalBW(tunnelSniperBW_1set5), getNewRam(tunnelTargetRam_1set5), marker='^', s=10, c='w', edgecolor=lineformat_1set5[0], label=label_1set5)
    pylab.scatter(getTotalBW(tunnelSniperBW_1set10), getNewRam(tunnelTargetRam_1set10), marker='s', s=10, c='w', edgecolor=lineformat_1set10[0], label=label_1set10)
    pylab.scatter(getTotalBW(tunnelSniperBW_1set20), getNewRam(tunnelTargetRam_1set20), marker='p', s=10, c='w', edgecolor=lineformat_1set20[0], label=label_1set20)
    pylab.scatter(getTotalBW(tunnelSniperBW_2set10), getNewRam(tunnelTargetRam_2set10), marker='h', s=10, c='w', edgecolor=lineformat_2set10[0], label=label_2set10)
    pylab.scatter(getTotalBW(tunnelSniperBW_5set10), getNewRam(tunnelTargetRam_5set10), marker='8', s=10, c='w', edgecolor=lineformat_5set10[0], label=label_5set10)
    pylab.scatter(getTotalBW(tunnelSniperBW_10set10), getNewRam(tunnelTargetRam_10set10), marker='*', s=10, c='w', edgecolor=lineformat_10set10[0], label=label_10set10)

    pylab.title("BW vs RAM, N sets of M sybils, Tunnel", x='1.0', ha='right')#, fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Sniper Total Bandwidth (KiB/s)")
    pylab.ylabel("Target RAM Consumption Rate (KiB/s)")
    pylab.xlim(xmin=0.0, xmax=250.0)#, xmax=10.0)
    pylab.ylim(ymin=0.0)#, ymax=10.0)
    pylab.legend(loc="upper right")
    pylab.savefig("{0}/tunnel.bw.ram.pdf".format(OUTDIR))

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
def getTime(data): return [d[0]/60.0 for d in data[1:]] # skip header

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
