#!python
from sys import argv
from re import split
from re import compile
inputFile = file(argv[1])

idMatcher = compile(".*\[([0-9]*)].*")

eof=0
print "<IGPROF>"
while eof == 0:
    line = inputFile.readline ()

    if line == "":
        eof = 1 
    elif line[0] == "[":
        (id, total, self, path, symbol, library, null) = split ("\s+",line)
        (null1, id, null2) = split ("\W+",id)
        print "<FUNC ID=\"" + id + "\" SYMBOL=\"" + symbol + "\" SELF=\"" + self + "\" TOTAL=\"" + total+"\"/>"

inputFile.close ()
inputFile2 = file(argv[1])
eof = 0
beginOfBlock = 0
started = 0

while eof == 0:
    line = inputFile2.readline ()
    if line == "":
        eof = 1
    elif line[0] == "-":
        if started == 1:
            print "</BLOCK>"
        print "<BLOCK>"
        beginOfBlock = 1
        started = 1
    elif line[0] == "[":
        beginOfBlock = 0
        id = idMatcher.match (line).group (1)
        print "<NODE ID=\"" + id + "\"/>"
    elif beginOfBlock == 1:
        id = idMatcher.match (line).group (1)
        print "<CALLER ID=\""+ id + "\"/>"
    elif beginOfBlock == 0 and started == 1:
        id = idMatcher.match (line).group (1)
        print "<CHILD ID=\""+ id + "\"/>"

print "</BLOCK></IGPROF>"
