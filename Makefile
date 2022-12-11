.PHONY: build
build: libscheduler.so

scheduler.o: scheduler.c
	gcc -fPIC -c scheduler.c

libscheduler.so: scheduler.o
	gcc -shared scheduler.o -o libscheduler.so

.PHONY: clean
clean:
	-rm -f libscheduler.so scheduler.o
