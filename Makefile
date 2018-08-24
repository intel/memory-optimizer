CC = gcc
CFLAGS = -Wall

page-refs: page-refs.o
	$(CC) $< -o $@ $(CFLAGS)

page-refs.o: page-refs.c lib/memparse.c lib/iomem_parse.c lib/iomem_parse.c
	$(CC) -c $< -o $@ $(CFLAGS)
