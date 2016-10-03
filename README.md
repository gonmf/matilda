[![Build Status](https://travis-ci.org/gonmf/matilda.svg?branch=master)](https://travis-ci.org/gonmf/matilda)

Matilda - Go/Igo/Wéiqí/Baduk playing software
===

Matilda is a competitive computer Go playing engine and accompanying software. Go is an ancient and beautiful strategy board game, you can read more about it [here](http://senseis.xmp.net/?WhatIsGo).

Implementation-wise it is a MCTS Mogo-like program. It is aimed at 64 bit computers in shared memory, playing with Chinese rules via the Go Text Protocol. It is versatile and optimized for speed in a lot of areas, though some changes require a recompilation.

The relative strength of Matilda can be seen from playing on the [CGOS](http://www.yss-aya.com/cgos/9x9/standings.html).

**System Requirements**

  - Linux, BSD, macOS or other POSIX 2004 compliant system
  - C99 compiler suite with support for OpenMP 3.0 (GCC preferred)

Before using read the INSTALL file carefully, and at least modify the file src/inc/matilda.h to your taste.

Matilda uses external files. You can generate them yourself with game records and Matilda, or use the default files that are present in the src/data directory. They are available for 9x9, 13x13 and 19x19 boards.

**How to**

You can play with Matilda out of the box using a text interface. For a graphical interface you can connect it with any GTP-speaking program that supports Chinese rules, like [GoGui](http://gogui.sourceforge.net/).

--

All parts of Matilda are licensed as permissive free software, as described in the file LICENSE that should accompany this document, except for the following files. src/crc32.c, which was derived from another file, is distributed with the same license as the original (public domain). The files contained in the src/data/ directory may also be based on game records or other foreign files, and may be in dubious licensing circumstances. For legal enquiries contact the author of this software.

This project started as the practical component of a dissertation for the obtention of a Masters Degree on Computer Science and Computer Engineering, from the High Institute of Engineering of Lisbon, titled "Guiding Monte Carlo tree searches with neural networks in the game of Go" (2016) by Gonçalo Mendes Ferreira.
