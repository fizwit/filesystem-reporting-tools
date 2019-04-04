#!/usr/bin/env python3
#
import sys
import csv
from pprint import pprint

"""2018.10.28 John Dey
consume CSV output from pwalk and reassemble directory data.
File system data from pwalk is flattened and out of order.
Rewalk the tree data and create two new fields for each directory.
Create a tree sum of file count and bytes at each directory (node)
that represents the child nodes.  Sums for the root will 
become the total file count and sum size for every file.

Notes: I wrote this in Python as a proof of concept.
"""

def usage():
    """how to use and exit"""
    print("usage: % inputfile.csv" % sys.argv[0])
    sys.exit(1)

if len(sys.argv ) != 2:
    usage()

dd = {}
with open(sys.argv[1], newline='') as csvfile:
     pwalk = csv.reader(csvfile, delimiter=',', quotechar='"')
     for row in pwalk:
         if int(row[15]) >= 0:   # only store directories
             dd[int(row[0])] = {'parent': int(row[1]),
                           'depth': int(row[2]),
                           'dircnt': int(row[15]),
                           'sumcnt': int(row[15]),  # or Zero?
                           'dirsiz': int(row[16]),
                           'sumsiz': int(row[16])}
             if int(row[1]) == 0:
                 root = int(row[0])
                 dd[int(row[0])]['sumcnt'] += 1
                 dd[int(row[0])]['sumsiz'] += int(row[7]) 

print("Total directories: %d" % len(dd.keys()))

"""reassemble the tree"""
for inode in dd.keys():
    parent = dd[inode]['parent']
    while parent != 0: 
       dd[parent]['sumcnt'] += dd[inode]['dircnt']
       dd[parent]['sumsiz'] += dd[inode]['dirsiz']
       parent = dd[parent]['parent']
pprint(dd[root])

