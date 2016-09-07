[![Build Status](https://travis-ci.org/gonmf/matilda.svg?branch=master)](https://travis-ci.org/gonmf/matilda)

Matilda - Go/Igo/Wéiqí/Baduk playing software
===

Matilda is a competitive computer Go playing engine and accompanying software.
Implementation-wise it is a MCTS Mogo-like Go program. It is aimed at 64 bit computers in shared memory, playing with Chinese rules via the Go Text Protocol.
It is versatile and optimized for speed in a lot of areas, though some changes require a recompilation.

**Playing strength (v0.19)**

Board | ELO | EGF Rank
:---: | :---: | :---:
 9x9  | 2242 | 2 dan
13x13 |  |

These values are estimates based on the results against a reference opponent, GNU Go 3.8. They used [CGOS](http://cgos.boardspace.net/) match settings and the hardware equivalent of a medium range consumer computer from 2014. The translation of ELO to Japanese ranks is made with the table from [SL](http://senseis.xmp.net/?GoR). In terms of strength Matilda is pretty strong in small boards, but still needs a lot of work in 19x19.

**System Requirements**

  - Linux, BSD, Mac OSX or other POSIX 2004 compliant system
  - C99 compiler suite with support for OpenMP 3.0 (GCC preferred)

Before using read the INSTALL file carefully, and at least modify the file src/inc/matilda.h to your taste.

Matilda uses external files. You can generate them yourself with game records and Matilda, or use the default files that are present in the src/data directory. They are available for 9x9, 13x13 and 19x19 boards.

All parts of Matilda are licensed as permissive free software, as described in the file LICENSE that should accompany this document, except for the following files. src/crc32.c, which was derived from another file, is distributed with the same license as the original (public domain). The files contained in the src/data/ directory may also be based on game records or other foreign files, and may be in dubious licensing circumstances. For legal enquiries contact the author of this software.

This project started as the practical component of a dissertation for the obtention of a Masters Degree on Computer Science and Computer Engineering, from the High Institute of Engineering of Lisbon, titled "Guiding Monte Carlo tree searches with neural networks in the game of Go" (2016) by Gonçalo Mendes Ferreira.
