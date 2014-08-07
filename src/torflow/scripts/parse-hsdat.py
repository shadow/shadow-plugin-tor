#!/usr/bin/python

import sys, os, json, gzip

def main():
    hstimes = getContents("hs-times.dat.gz")

    ''' format:
    0<int>: unique id of a hidden server/client pair
    1<float>: time (s) that client app send .onion to Tor's socks port
    2<float>: time (s) that socks port says its connected to hidden service
    3<float>: time (s) that hidden service recieves the 'padding' cells
    4<list>: the rendevous path used by the hidden service
    '''
    print len(hstimes)
    print hstimes[0]

def getContents(filename):
    data = None
    with gzip.open(filename, 'r') as f: data = json.load(f)
    return data

if __name__ == '__main__':
    main()
