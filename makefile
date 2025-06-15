compRun:
	gcc main.c -o r.out -lcurl -lpthread


clean:
	rm -f *.out

clear:
	clear

run: clear clean compRun; ./r.out url.txt 4 100