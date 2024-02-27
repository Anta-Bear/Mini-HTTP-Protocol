bin=httpserver
cgi=test_cgi
cc=g++
LD_FLAGS=-std=c++11 -pthread
curr=$(shell pwd)
src=main.cc

ALL:$(bin) $(cgi) 
.PHONY:ALL 

$(bin):$(src)
	$(cc) -o $@ $^ $(LD_FLAGS)
$(cgi):$(curr)/cgi/test_cgi.cc
	$(cc) -o $@ $^ -std=c++11

.PHONY:clean
clean:
	rm $(bin) $(cgi)
	rm -rf output

.PHONY:output
output:
	mkdir -p output
	cp $(bin) output
	cp -rf wwwroot output
	cp $(cgi) output/wwwroot