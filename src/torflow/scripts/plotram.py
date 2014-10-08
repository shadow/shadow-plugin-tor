#!/usr/bin/python

import sys, os, json, gzip, pylab

def main():
    if len(sys.argv) < 2:
        print 'Usage: {0} <nodename-ram.dat.gz> ...'.format(sys.argv[0])
        sys.exit(0)

    stats = {}
    for filename in sys.argv[1:]:
        starti = filename.rindex('/')+1 if '/' in filename else 0
        fn = filename[starti:]
        name = filename#fn.split('-')[0]
        with gzip.open(filename, 'r') as f: stats[name] = json.load(f)

    pylab.figure()
    for name in stats:
        time = [d[0]/60.0 for d in stats[name][1:]] # skip header
        total = [d[4]/(1024*1024) for d in stats[name][1:]] # skip header
        #x, y = getcdf(total)
        pylab.plot(time, total, label=name)
        print "{0} max ram usage = {1} MiB".format(name, max(total))
    #pylab.suptitle("RAM Usage", fontsize="small")
    pylab.title("RAM Usage, measured every 60 seconds", fontsize="x-small", x='1.0', ha='right')
    pylab.xlabel("Time (m)")
    pylab.ylabel("RAM (MiB)")
    pylab.legend(loc="upper left")
    pylab.savefig("ram.time.pdf")

if __name__ == '__main__':
    main()
