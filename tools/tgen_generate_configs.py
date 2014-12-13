#!/usr/bin/python

import sys, networkx as nx

NUMSERVERS=30

def main():
    servers = []
    for i in xrange(NUMSERVERS): servers.append("server{0}:80".format(i+1))
    s = ','.join(servers)

    generate_tgen_server()
    generate_tgen_filetransfer_clients(servers=s)
    generate_tgen_perf_clients(servers=s, size="50 KiB", name="tgen.torperf50kclient.graphml.xml")
    generate_tgen_perf_clients(servers=s, size="1 MiB", name="tgen.torperf1mclient.graphml.xml")
    generate_tgen_perf_clients(servers=s, size="5 MiB", name="tgen.torperf5mclient.graphml.xml")

def generate_tgen_server():
    G = nx.DiGraph()
    G.add_node("start", serverport="80")
    nx.write_graphml(G, "tgen.server.graphml.xml")

def generate_tgen_filetransfer_clients(servers):
    # webclients
    G = nx.DiGraph()

    G.add_node("start", socksproxy="localhost:9000", serverport="8888", peers=servers)
    G.add_node("transfer", type="get", protocol="tcp", size="320 KiB")
    G.add_node("pause", time="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60")

    G.add_edge("start", "transfer")
    G.add_edge("transfer", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, "tgen.torwebclient.graphml.xml")

    # bulkclients
    G = nx.DiGraph()

    G.add_node("start", socksproxy="localhost:9000", serverport="8888", peers=servers)
    G.add_node("transfer", type="get", protocol="tcp", size="5 MiB")

    G.add_edge("start", "transfer")
    G.add_edge("transfer", "start")

    nx.write_graphml(G, "tgen.torbulkclient.graphml.xml")

def generate_tgen_perf_clients(servers="server1:8888,server2:8888", size="50 KiB", name="tgen.perf50kclient.graphml.xml"):
    G = nx.DiGraph()

    G.add_node("start", socksproxy="localhost:9000", serverport="8888", peers=servers)
    G.add_node("transfer", type="get", protocol="tcp", size=size)
    G.add_node("pause", time="60")

    G.add_edge("start", "transfer")
    G.add_edge("transfer", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, name)

if __name__ == '__main__': sys.exit(main())
