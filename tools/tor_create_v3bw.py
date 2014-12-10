#! /usr/bin/python

import sys, os, subprocess, shlex
from lxml import etree

TORBIN="/home/rob/shadow-plugin-tor/build/tor/src/or/tor"

def main():
    parser = etree.XMLParser(remove_blank_text=True)
    tree = etree.parse("shadow.config.xml", parser)
    root = tree.getroot()

    os.chdir("initdata")
    os.makedirs("torflowauthority")
    v3bwfile = open("torflowauthority/v3bw.init.consensus", "wb")
    os.symlink("v3bw.init.consensus", "torflowauthority/v3bw")
    v3bwfile.write("1\n")

    for n in root.iterchildren("node"):
        runsTor = False
        for a in n.iterchildren("application"):
            if 'tor' in a.get("plugin"): runsTor = True
        if runsTor:
            name = n.get('id')
            if 'relay' in name: # authority already has dir and fp
                os.makedirs(name)
                os.chdir(name)
                rc, fp = getfp('../authgen.torrc', name)
                os.chdir("..")

                assert rc == 0
                fp = fp.replace(" ", "")
                bw = n.get('bandwidthup')

                bwline = "node_id=${0}\tbw={1}\tnick={2}\n".format(fp, bw, name)
                print bwline
                v3bwfile.write(bwline)

def getfp(torrc, name, datadir="."):
    """Run Tor with --list-fingerprint to get its fingerprint, read
    the fingerprint file and return the fingerprint. Uses current
    directory for DataDir. Returns a two-element list where the first
    element is an integer return code from running tor (0 for success)
    and the second is a string with the fingerprint, or None on
    failure."""
    with open('/dev/null', 'wb') as dn:
        listfp = "{0} --list-fingerprint --DataDirectory {2} --Nickname {3} -f {1}".format(TORBIN, torrc, datadir, name)
        retcode = subprocess.call(shlex.split(listfp), stdout=dn, stderr=subprocess.STDOUT)
        if retcode !=0: return retcode, None
        fp = None
        with open("{0}/fingerprint".format(datadir), 'r') as f:
            fp = f.readline().strip().split()[1]
            fp = " ".join(fp[i:i+4] for i in range(0, len(fp), 4))
        return 0, fp

if __name__ == '__main__': sys.exit(main())
