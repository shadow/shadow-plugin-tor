#!/usr/bin/python

# takes a xml topology file and adds a torctl application for each tor node.
# torctl logs information from tor via the controller events specified herein.

import sys
from lxml import etree

if len(sys.argv) != 3: print >>sys.stderr, "{0} input.xml output.xml".format(sys.argv[0]);exit()

infname = sys.argv[1]
outfname = sys.argv[2]

parser = etree.XMLParser(remove_blank_text=True)
tree = etree.parse(infname, parser)
root = tree.getroot()

p = etree.SubElement(root, "plugin")
p.set("id", "torctl")
p.set("path", "~/.shadow/plugins/libshadow-plugin-torctl.so")

for n in root.iterchildren("node"):
    nodeid = n.get("id")
    if 'client' in nodeid or 'relay' in nodeid or 'thority' in nodeid:
        # get the time scallion starts
        starttime = None
        for a in n.iterchildren("application"):
            if 'scallion' in a.get("plugin"):
                starttime = a.get("starttime")
                if starttime is None: starttime = a.get("time")

        # create our torctl app 10 seconds after scallion
        if starttime is not None:
            starttime = str(int(starttime) + 10)
            a = etree.SubElement(n, "application")
            a.set("plugin", "torctl")
            a.set("starttime", starttime)
            # available events: STREAM,CIRC,ORCONN,BW,STREAM_BW,CIRC_BW,CONN_BW,TB_EMPTY,CELL_STATS
            a.set("arguments", "localhost 9051 STREAM,CIRC,ORCONN,BW,STREAM_BW,CIRC_BW,CONN_BW,TB_EMPTY,CELL_STATS")

with open(outfname, 'wb') as outf:
    print >>outf, (etree.tostring(root, pretty_print=True, xml_declaration=False))

