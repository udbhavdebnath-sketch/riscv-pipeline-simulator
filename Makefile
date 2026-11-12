# Builds all four labs and runs every check.
#   make          build lab2..lab5
#   make test     golden figures, oracle diffs, demo programs, independent
#                 integer/FP references and a short random-program run
#   make clean
LABS := lab2 lab3 lab4 lab5

.PHONY: all test clean
all:
	@for d in $(LABS); do $(MAKE) -s -C $$d || exit 1; done

test: all
	./golden.sh
	@for d in $(LABS); do echo "== $$d make test"; $(MAKE) -s -C $$d test > /dev/null && echo ok || exit 1; done
	tests/demo/run.sh
	@for d in $(LABS); do python3 tests/intref.py $$d 500 1 $$( [ $$d = lab5 ] && echo --m ) || exit 1; done
	python3 tests/fpref.py lab5 2000 1
	@for d in lab2 lab3 lab4; do python3 tests/fuzz.py $$d 40 || exit 1; done
	python3 tests/fuzz.py lab5 40 1 --fp

clean:
	@for d in $(LABS); do $(MAKE) -s -C $$d clean; done
	rm -rf tests/fuzz_fail tests/__pycache__
