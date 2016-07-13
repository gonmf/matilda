#!/bin/bash
find . \( -name "*.h" -or -name "*.c" -or -name "Makefile" \) \
    -exec grep -l $1 {} \;

