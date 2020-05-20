all:
	clang++ -std=c++2a -I../include test_update.cpp ../build/libleveldb.a -o test_update
	clang++ -std=c++2a -I../include -I../ read_manifest.cpp ../build/libleveldb.a -o read_manifest

clean:
	rm -f test_update read_manifest

