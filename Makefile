

sbench: sbench.cpp
	g++ -O3 -std=c++1z -W -Wall -o sbench sbench.cpp

clean:
	rm -f sbench

