#says "heres how to compile everything"

allocator: main.c allocator.c allocator.h
	gcc main.c allocator.c -o allocator

clean:
	rm -f allocator
