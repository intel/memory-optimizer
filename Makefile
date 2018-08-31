CC = gcc
CFLAGS = -Wall
LIB_SOURCE_FILES = lib/memparse.c lib/iomem_parse.c lib/page-types.c

page-refs: page-refs.c $(LIB_SOURCE_FILES)
	$(CC) -g $< $(LIB_SOURCE_FILES) -o $@ $(CFLAGS)
