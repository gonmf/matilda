[![Build Status](https://travis-ci.org/gonmf/matilda.svg?branch=master)](https://travis-ci.org/gonmf/matilda)

Matilda - Go/Igo/Wéiqí/Baduk playing software
===

Matilda is a competitive computer Go playing engine and accompanying software.
Implementation-wise it is a MCTS Mogo-like Go program. It is aimed at 64 bit computers in shared memory, playing with Chinese rules via the Go Text Protocol.
It is versatile and optimized for speed in a lot of areas, though some changes require a recompilation.

In terms of strength Matilda is pretty strong in small boards, but still needs a lot of work in 19x19.

Before using read the INSTALL file carefully, and at least modify the file src/inc/matilda.h to your taste.

*System Requirements*
  - Linux or other BSD and POSIX compliant system, like Mac OSX
  - C99 compiler suite with support for OpenMP 3.0 (GCC preferred)

Matilda uses external files. You can generate them yourself with game records and Matilda, or use the default files that are present in the src/data directory. They are available for 9x9, 13x13 and 19x19 boards.

All parts of Matilda are licensed as permissive free software, as described in the file LICENSE that should accompany this document, except for the following files. src/crc32.c, which was derived from another file, is distributed with the same license as the original (public domain). The files contained in the src/data/ directory may also be based on game records or other foreign files, and may be in dubious licensing circumstances. For legal enquiries contact the author of this software.

This project started as the practical component of a dissertation for the obtention of a Masters Degree on Computer Science and Computer Engineering, from the High Institute of Engineering of Lisbon, titled "Guiding Monte Carlo tree searches with neural networks in the game of Go" (2016) by Gonçalo Mendes Ferreira.
