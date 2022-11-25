g++ -c cJSON.c
g++ -c cJSON.h
g++ -c monitor.cpp

g++ -o main cJSON.o cJSON.h monitor.o

./main
