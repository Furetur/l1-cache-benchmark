TIME = $(shell date +%H%M%S_%d-%m-%y)
RESULTS_FILE_NAME = results_${TIME}.csv

$(info Results file: ${RESULTS_FILE_NAME})


.PHONY: ${RESULTS_FILE_NAME}

all: ${RESULTS_FILE_NAME}

${RESULTS_FILE_NAME}: main
	-./main > ${RESULTS_FILE_NAME}
	@echo "\nFull results: ${RESULTS_FILE_NAME}"

main: main.cpp
	$(CXX) -g -O0 -Wall -std=c++20 main.cpp -o main

