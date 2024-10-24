TIME = $(shell date +%H%M_%d-%m-%y)
RESULTS_FILE_NAME = results_${TIME}.csv

$(info Results file: ${RESULTS_FILE_NAME})


.PHONY: ${RESULTS_FILE_NAME}

all: ${RESULTS_FILE_NAME}

${RESULTS_FILE_NAME}: main
	./main 2> /dev/null > ${RESULTS_FILE_NAME}
	@echo "\n----- RESULTS ${RESULTS_FILE_NAME} ------"
	@cat ${RESULTS_FILE_NAME}

main: main.cpp
	clang++ -g -O0 -Wall -std=c++11 main.cpp -o main

