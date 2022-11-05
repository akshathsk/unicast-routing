#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "monitor_neighbors.h"

int main(int argc, char **argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "Usage: %s nodeId initialCostFile logFileName\n", argv[0]);
		exit(1);
	}

	nodeId = atoi(argv[1]);

	setUpGlobalNetwork();

	initCosts();
	updateLinkCosts(argv[2], cost);

	setUpSendRecvSocket();

	pthread_t announcerThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void *)0);

	pthread_t broadcastThread;
	pthread_create(&broadcastThread, 0, broadcastToNeighbors, (void *)0);

	listenForNeighbors(argv[3]);
}
