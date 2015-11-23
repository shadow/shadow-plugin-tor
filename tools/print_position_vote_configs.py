
from lxml import etree

parser = etree.XMLParser(remove_blank_text=True)
tree = etree.parse("shadow.config.xml", parser)
root = tree.getroot()

g, e = [], []

for n in root.iterchildren("node"):
    for a in n.iterchildren("application"):
        if 'tor' == a.get("plugin"):
            name = n.get('id')
            if 'relay' in name and 'guard' in name: g.append(name)
            if 'relay' in name and 'exit' in name: e.append(name)

print "TestingDirAuthVoteGuard {0}".format(','.join(g))
print "TestingDirAuthVoteExit {0}".format(','.join(e))
