#!/usr/bin/env python3
"""Partial updates whose values are a function of the document id and lie outside the values r6load docs puts
(price >= 1000, status = 7, boost >= 2), cycling over ids [START, START+N) · spec: fast-restart/PREREG.md H4
After at least one full pass, filters on these values count exactly N in every arm, so a restart that lost an
attribute delta checkpoint shows up as a different count.
usage: updates.py START N   (pipe through throttle.py)"""
import sys

start, n = int(sys.argv[1]), int(sys.argv[2])
out = sys.stdout
while True:
    for d in range(start, start + n):
        out.write('{"update":"id:r6:webpage::%d","fields":{"price":{"assign":%.2f},"status":{"assign":7},'
                  '"boost":{"assign":%.4f}}}\n' % (d, 1000 + (d % 9973) / 10, 2 + (d % 997) / 1000))
