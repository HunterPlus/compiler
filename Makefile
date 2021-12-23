CFLAGS=-std=c11 -g -static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

mycc: 	$(OBJS)
		$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): c.h

test:	mycc
			./test.sh
			./test-driver.sh
clean:
		rm -f mycc *.o *~ tmp*
.PHONY:	test clean