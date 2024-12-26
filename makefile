main:
	gcc -DMAIN preflow.c -o preflow -pthread -O3
	time sh check-solution.sh ./preflow
	@echo PASS all tests
