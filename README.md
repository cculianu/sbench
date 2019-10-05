# OSX Simple SSD Benchmark
(C) 2019 Calin Culianu <calin.culianu@gmail.com>

This is a command-line tool for a quick and dirty benchmark of your SSD drive for MacOS.

It is intended to be run from the console to benchmark your SSD drive. You must have Xcode and Xcode Command-Line Tools installed to use compile this utility.

### Compiling
```
    make 
```

### Running
```
    ./sbench dummyfile 20000 # second arg here is number of MB for test
```

### Example
```
    $ make
    g++ -O3 -std=c++1z -W -Wall -o sbench sbench.cpp
    
    $ ./sbench dummyfile 20000
    Generating random data...took 0.145 seconds
    Writing 20000 MB to dummyfile...took 25.453 seconds (785.77 MB/sec)
    Running /usr/sbin/purge with sudo (clearing read cache)...
    Reading back dummyfile...took 11.598 secs (1724.505 MB/sec)
    (Removed dummyfile)
```

### LICENSE

[GNU General Public License version 3](https://www.gnu.org/licenses/gpl-3.0.en.html)


