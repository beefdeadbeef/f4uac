set print pretty
set mem inaccessible-by-default off
target extended-remote /dev/ttygdb
mon swdp_scan
attach 1
