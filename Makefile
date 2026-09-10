all:
	clang -O0 -g -rdynamic -o main main.c -ldl 
