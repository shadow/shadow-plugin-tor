#!/usr/bin/python

import os,sys
from lxml import etree

def main():
    if len(sys.argv) < 3:
        print 'Usage: {0} <old hosts filename> <new hosts filename>'.format(sys.argv[0])
        sys.exit(0)

    tree = etree.parse(sys.argv[1])
    root = tree.getroot()

    for a in root.iter('application'):
        if a.get("plugin") != "tor": continue
        args = a.get("arguments")
        argv = args.split()
        i = 0
        while i < len(argv):
            if argv[i] == "--BandwidthRate" or argv[i] == "--BandwidthBurst":
                n = int(argv[i+1])
                if n < 76800: argv[i+1] = str(76800)
                i += 2
            else: i += 1
        a.set("arguments", " ".join(argv))

    fout = open(sys.argv[2], 'w')
    fout.write(etree.tostring(root, pretty_print=True, xml_declaration=False))
    fout.close()

if __name__ == '__main__':
    main()
