#!/bin/sh

grep -a -i mlvl player/* | awk '{print $2, $1}' | sort -rh | head -20
