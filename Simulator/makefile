# Makefile for Car Park System
CC = gcc
CFLAGS = -Wall -Wextra -Werror -g

all: manager simulator firealarm

manager: manager.c
	gcc -o manager manager.c -lrt -pthread

simulator: simulator.c
	gcc -o simulator simulator.c -lrt -pthread

firealarm: firealarm.c
	gcc -o firealarm firealarm.c -lrt -pthread

.PHONY: all clean