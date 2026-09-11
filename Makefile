all:
	clang -rdynamic -o main main.c -ldl
