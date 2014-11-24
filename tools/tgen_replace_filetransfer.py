#!/usr/bin/python

"""
takes a shadow.config.xml file and replaces all uses of the filetransfer
plugin with the tgen plugin. the configs used here can be generated with
'tgen_generate_configs.py'.
"""

import sys
from lxml import etree

if len(sys.argv) != 3: print >>sys.stderr, "{0} input.xml output.xml".format(sys.argv[0]);exit()

infname = sys.argv[1]
outfname = sys.argv[2]

parser = etree.XMLParser(remove_blank_text=True)
tree = etree.parse(infname, parser)
root = tree.getroot()

for p in root.iterchildren("plugin"):
    if 'filetransfer' in p.get("id"):
        p.set("id", "tgen")
        p.set("path", "~/.shadow/plugins/libshadow-plugin-tgen.so")

hastgen = False        
for p in root.iterchildren("plugin"):
    if 'tgen' in p.get("id"): hastgen = True
assert hastgen == True

for n in root.iterchildren("node"):
    for a in n.iterchildren("application"):
        if 'filetransfer' in a.get("plugin"):
            starttime = a.get("starttime")
            if starttime is None: starttime = a.get("time")
            n.remove(a)
            a = etree.SubElement(n, "application")
            a.set("plugin", "tgen")
            a.set("starttime", "{0}".format(starttime))
            if 'server' in n.get("id"):
                a.set("arguments", "tgen.server.graphml.xml")
            elif 'webclient' in n.get("id"):
                a.set("arguments", "tgen.torwebclient.graphml.xml")
            elif 'bulkclient' in n.get("id"):
                a.set("arguments", "tgen.torbulkclient.graphml.xml")
            elif 'perfclient50k' in n.get("id"):
                a.set("arguments", "tgen.torperf50kclient.graphml.xml")
            elif 'perfclient1m' in n.get("id"):
                a.set("arguments", "tgen.torperf1mclient.graphml.xml")
            elif 'perfclient5m' in n.get("id"):
                a.set("arguments", "tgen.torperf5mclient.graphml.xml")
            else: assert False

with open(outfname, 'wb') as outf:
    print >>outf, (etree.tostring(root, pretty_print=True, xml_declaration=False))

