default: combine-aiger

OBJECTS = main.o combine.o aiger.o
CFLAGS = -std=c11 -Wall -Wshadow -pedantic

%.o: %.c %.h
	cc $(CFLAGS) -c $< -o $@

aiger.o: aiger.c aiger.h
	cc -c $< -o $@

combine-aiger: $(OBJECTS)
	cc $(OBJECTS) -o $@

clean:
	-rm $(OBJECTS)
	-rm combine-aiger