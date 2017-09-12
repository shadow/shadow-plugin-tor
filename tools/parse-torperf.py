#! /usr/bin/python

## Parse TorPerf output into CDF formated data files that can be plotted much easier with pylab
## This script assumes the newer .tpf torperf file format (v1.0)

## See:
## http://metrics.torproject.org/formats.html#torperf
## http://www.onlineconversion.com/unix_time.htm

import os, json
from subprocess import Popen, PIPE

##### Edit the following parameters for the month you are trying to model #####
INDIR="torperf-2017-01"
# only count measurments that occurred during this unix time range
# begintime is the first of the month, endtime is the last of the month, or 0 for no end time
begintime = 1483228800 # jan 1 2017, 00:00:00
endtime =  1485907199 # jan 31 2017. 23:59:59
##### end parameter section #####

# the data we extract
db = {"nodes": {"torperf": {"errors":{}, "firstbyte":{}, "lastbyte":{}}}}

# parse the files
for root, dirs, files in os.walk(INDIR):
    for file in files:
        fullpath = os.path.join(root, file)
        print fullpath
        with open(fullpath, 'r') as f:
            for line in f:
                if '=' not in line: continue
                d = {}
                parts = line.strip().split()
                for part in parts:
                    if '=' not in part: continue
                    p = part.split('=')
                    assert len(p) > 1
                    key, value = p[0], p[1]
                    d[key] = value

                start = float(d["START"])
                if start < begintime or (endtime > 0 and start > endtime): continue
                if int(d["DIDTIMEOUT"]) == 1: continue

                totalbytes = int(d["READBYTES"])
                if totalbytes < 1: continue
                if totalbytes == 51493: totalbytes = 51200
                if totalbytes == 1048873: totalbytes = 1048576
                if totalbytes == 5243177: totalbytes = 5242880

                if totalbytes not in db["nodes"]["torperf"]["firstbyte"]:
                    db["nodes"]["torperf"]["firstbyte"][totalbytes] = {1800:[]}
                    db["nodes"]["torperf"]["lastbyte"][totalbytes] = {1800:[]}

                first = float(d["DATARESPONSE"])
                if first >= start:
                    db["nodes"]["torperf"]["firstbyte"][totalbytes][1800].append(first-start)

                last = float(d["DATACOMPLETE"])
                if last >= start:
                    db["nodes"]["torperf"]["lastbyte"][totalbytes][1800].append(last-start)

# this filename is used so that plot-shadow.py will automatically work!
filename = 'stats.tgen.json'
with open("/dev/null", 'a') as nullf:
    path = "{}.xz".format(filename)
    xzp = Popen(["xz", "-"], stdin=PIPE, stdout=PIPE)
    ddp = Popen(["dd", "of={0}".format(path)], stdin=xzp.stdout, stdout=nullf, stderr=nullf)
    json.dump(db, xzp.stdin, sort_keys=True, separators=(',', ': '), indent=2)
    xzp.stdin.close()
    xzp.wait()
    ddp.wait()
#with open(filename, 'w') as outf:
#    json.dump(db, outf, sort_keys=True, separators=(',', ': '), indent=2)
