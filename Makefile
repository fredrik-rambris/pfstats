pfstats: pfstats.c
	gcc `pcre-config --libs --cflags` -pthread -o pfstats pfstats.c
clean:
	rm -f pfstats
