#!/usr/bin/env python
#############################################################################
"""
 GoguiScript.py

 Run a game of Go via gogui tools.
 Parameters of the program to be optimized are set via GTP commands.
 This script will create a sub-directory for each game.
"""
#############################################################################
import sys
import os
import shutil
import platform

#############################################################################
# Preliminaries
#############################################################################

#
# Processor and seed are passed on the command line
#
processor = sys.argv[1]
seed = int(sys.argv[2])

#
# Create empty directory for this seed, and chdir to it
#
path = "game-%07d" % seed
shutil.rmtree(path, ignore_errors = True)
os.makedirs(path)
os.chdir(path)

#############################################################################
#
# System options
#
# This script needs gogui-adapter to set parameters via gtp commands.
# gogui-adapter version 1.2.1 does not understand "resign".
# This problem was fixed:
# http://sf.net/tracker/?func=detail&atid=489964&aid=3016960&group_id=59117
# If you wish to play games with programs that resign, use a fixed version.
#

# program to be optimized
optimized_program = "/home/user/go/matilda/github/src/matilda -d /home/user/go/matilda/github/src/data/ -m gtp --disable_opening_books -l"

# (fixed) opponent program
opponent_program = 'gnugo --mode gtp --chinese-rules --positional-superko --level 0'


i = 4
params = []
while i < len(sys.argv):
    name = sys.argv[i - 1]
    value = sys.argv[i]
    optimized_program += ' --set ' + name + ' ' + value
    i += 2


#
# Protect program names with quotes
#
optimized_program = '\"' + optimized_program + '\"'
opponent_program = '\"' + opponent_program + '\"'

#
# Run one game with gogui-twogtp
#
command = 'gogui-twogtp -size 9 -komi 7.5 -white ' + optimized_program + ' -black ' + opponent_program + ' -sgffile twogtp.sgf -games 1 -auto' # -time 3s'

#print "command = ", command

os.system(command)

#
# Return game result
#
try:
    dat_file = open('twogtp.sgf.dat', 'r')

    for i in range(16):
        dat_file.readline()
    line = dat_file.readline()

    dat_file.close()

    if 'unexpectedly' in line: # problem
        print "Error: " + line
    elif 'W+' in line:
        print "W"
    elif 'B+' in line:
        print "L"
    else:
        print "Error: could not determine game result"
except IOError:
    print "Error: IOError"
