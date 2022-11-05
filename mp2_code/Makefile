all: ls

ls: main.cpp monitor_neighbors.h
	g++ -o ls_router main.cpp monitor_neighbors.h -g

.PHONY: clean
clean:
	rm *.o ls_router
