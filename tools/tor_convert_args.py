#!/usr/bin/python

"""
takes a shadow.config.xml file and replaces args with new format used in shadow v1.10.0.
this is to say the first 2 args of every tor node args line are removed.
"""

import sys
from lxml import etree

if len(sys.argv) != 3: print >>sys.stderr, "{0} input.xml output.xml".format(sys.argv[0]);exit()

infname = sys.argv[1]
outfname = sys.argv[2]

parser = etree.XMLParser(remove_blank_text=True)
tree = etree.parse(infname, parser)
root = tree.getroot()

for n in root.iterchildren("node"):
    for a in n.iterchildren("application"):
        if 'tor' in a.get("plugin") or 'scallion' in a.get("plugin"):
            args = a.get('arguments').strip().split()
            assert 'client' in args[0] or 'dirauth' in args[0] or 'relay' in args[0] or 'exitrelay' in args[0]
            a.set('arguments', ' '.join(args[2:]))

with open(outfname, 'wb') as outf:
    print >>outf, (etree.tostring(root, pretty_print=True, xml_declaration=False))

