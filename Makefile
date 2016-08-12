.PHONY: phony

hyperloop-debug: hyperloop.cpp 
	NDEBUG=1 g++ -g -O0 -fno-inline -std=c++14 hyperloop.cpp -o hyperloop-debug

start: hyperloop-debug
	./hyperloop-debug

lldb: hyperloop-debug phony
	lldb hyperloop-debug

%.d: hyperloop-debug phony
	sudo dtrace -s $@ -c ./hyperloop-debug

phony:
