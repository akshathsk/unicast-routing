#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>

#define MAX_NODES 256
int MAX_INT = 9999;
const char space[2] = " ";

typedef struct sendPacket
{
	int destination;
	int nextDestination;
	char message[MAX_NODES];
} sendPacket;

typedef struct recvPacket
{
	int length;
	int seq;
	int from;
	int fromCost[MAX_NODES];
} recvPacket;

int nodeId;
int cost[MAX_NODES];
int currentSocket;
int broadcastSeq;
int broadcastSequenceFrom[MAX_NODES];
int neighbour[MAX_NODES];
int network[MAX_NODES][MAX_NODES];
int pred[MAX_NODES];
int broadcastFlag;
int networkInfoReceived = 0;

struct timeval heartBeatHistory[MAX_NODES];
struct sockaddr_in allNodeAddresses[MAX_NODES];

pthread_mutex_t m_forwardFlag = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_broadcastFlag = PTHREAD_MUTEX_INITIALIZER;

void setUpGlobalNetwork()
{
	for (int i = 0; i < MAX_NODES; i++)
	{
		gettimeofday(&heartBeatHistory[i], 0);
		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&allNodeAddresses[i], 0, sizeof(allNodeAddresses[i]));
		allNodeAddresses[i].sin_family = AF_INET;
		allNodeAddresses[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &allNodeAddresses[i].sin_addr);
	}
}

void initCosts()
{
	for (int i = 0; i < MAX_NODES; i++)
	{
		cost[i] = 1;
	}
}

void updateLinkCosts(char *costFile, int *cost)
{
	FILE *pointer;
	pointer = fopen(costFile, "r");

	if (pointer == NULL)
	{
		return;
	}

	char *line = NULL;
	size_t length = 0;
	char *token;
	ssize_t readLine;
	while ((readLine = getline(&line, &length, pointer)) != -1)
	{
		token = strtok(line, space);
		int nodeId = atoi(token);
		token = strtok(NULL, space);
		int costToNode = atoi(token);
		cost[nodeId] = costToNode;
	}
	fclose(pointer);
	if (line)
		free(line);
}

void setUpSendRecvSocket()
{
	if ((currentSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	char address[100];
	struct sockaddr_in bindAddress;
	sprintf(address, "10.1.1.%d", nodeId);
	memset(&bindAddress, 0, sizeof(bindAddress));
	bindAddress.sin_family = AF_INET;
	bindAddress.sin_port = htons(7777);
	inet_pton(AF_INET, address, &bindAddress.sin_addr);
	if (bind(currentSocket, (struct sockaddr *)&bindAddress, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(currentSocket);
		exit(1);
	}
}

void hackyBroadcast(const char *buf, int length)
{
	for (int i = 0; i < MAX_NODES; i++)
	{
		if (i != nodeId)
		{
			sendto(currentSocket, buf, length, 0,
				   (struct sockaddr *)&allNodeAddresses[i], sizeof(allNodeAddresses[i]));
		}
	}
}

void *announceToNeighbors(void *nan)
{
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 300 * 1000 * 1000;
	while (1)
	{
		hackyBroadcast("ANNOUNC", 7);
		nanosleep(&sleepFor, 0);
	}
}

int updateMessageWithMyNetworkDetails(char *message)
{
	int count = 0;
	strcpy(message, "NETW");
	for (int i = 0; i < MAX_NODES; i++)
	{
		for (int j = i + 1; j < MAX_NODES; j++)
		{
			short int msg;
			if (network[i][j])
			{
				msg = htons((short int)i);
				memcpy(message + 4 + count * sizeof(short int), &msg, sizeof(short int));
				count++;
				msg = htons((short int)j);
				memcpy(message + 4 + count * sizeof(short int), &msg, sizeof(short int));
				count++;
				msg = htons((short int)network[i][j]);
				memcpy(message + 4 + count * sizeof(short int), &msg, sizeof(short int));
				count++;
			}
		}
	}
	char end[2] = "*";
	memcpy(message + 4 + count * sizeof(short int), end, 1);
	return 4 + count * sizeof(short int) + 1;
}

void sendToSpecificDestination(char *bufferSend, int destId, int length)
{
	if (sendto(currentSocket, bufferSend, length, 0,
			   (struct sockaddr *)&allNodeAddresses[destId], sizeof(allNodeAddresses[destId])) < 0)
	{
		neighbour[destId] = 0;
		network[nodeId][destId] = 0;
		network[destId][nodeId] = 0;
		broadcastSeq++;
	}
}

void updateNetworkFromNeighborInfo(char *messageReceived)
{
	int i = 0;
	short int seq;
	short int fromIndex;
	short int toIndex;
	short int cost;

	while (1)
	{
		if (!strncmp(messageReceived + 4 + sizeof(short int) * i, "*", 1))
		{
			break;
		}
		else
		{
			memcpy(&seq, messageReceived + 4 + sizeof(short int) * i, sizeof(short int));
			fromIndex = ntohs(seq);
			i++;
			memcpy(&seq, messageReceived + 4 + sizeof(short int) * i, sizeof(short int));
			toIndex = ntohs(seq);
			i++;
			memcpy(&seq, messageReceived + 4 + sizeof(short int) * i, sizeof(short int));
			i++;
			cost = ntohs(seq);
			network[fromIndex][toIndex] = cost;
			network[toIndex][fromIndex] = cost;
		}
	}
}

int updateBroadcastMessage(char *message)
{
	strcpy(message, "BROA");
	short int msg = htons((short int)broadcastSeq);
	memcpy(message + 4, &msg, sizeof(short int));
	msg = htons((short int)nodeId);
	memcpy(message + 4 + sizeof(short int), &msg, sizeof(short int));
	int count = 2;
	for (short int i = 0; i < MAX_NODES; i++)
	{
		if (neighbour[i])
		{
			msg = htons((short int)i);
			memcpy(message + 4 + sizeof(short int) * count, &msg, sizeof(short int));
			count++;
			msg = htons((short int)cost[i]);
			memcpy(message + 4 + sizeof(short int) * count, &msg, sizeof(short int));
			count++;
		}
	}
	char end[2] = "*";
	memcpy(message + 4 + sizeof(short int) * count, end, 1);
	return 4 + count * sizeof(short int) + 1;
}

void *broadcastToNeighbors(void *nan)
{
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 300 * 1000 * 1000;
	char message[MAX_NODES] = {"\0"};
	int len;
	broadcastFlag = 0;

	while (1)
	{
		len = updateBroadcastMessage(message);
		for (int i = 0; i < MAX_NODES; i++)
		{
			if (i != nodeId && neighbour[i])
			{
				sendToSpecificDestination(message, i, len);
			}
		}
		nanosleep(&sleepFor, 0);
		pthread_mutex_lock(&m_broadcastFlag);
		broadcastSeq++;
		pthread_mutex_unlock(&m_broadcastFlag);
	}
}

void dijkstraAlgo()
{
	int start = nodeId;
	int dijGraph[MAX_NODES][MAX_NODES] = {0};
	int distance[MAX_NODES];
	int visitedNodes[MAX_NODES];
	int count, mindistance, nextnode;
	int firstHop[MAX_NODES] = {0};
	for (int i = 0; i < MAX_NODES; i++)
	{
		for (int j = 0; j < MAX_NODES; j++)
		{
			if (network[i][j])
			{
				dijGraph[i][j] = network[i][j];
			}
			else
			{
				dijGraph[i][j] = MAX_INT;
			}
		}
	}

	for (int i = 0; i < MAX_NODES; i++)
	{
		distance[i] = dijGraph[start][i];
		pred[i] = start;
		visitedNodes[i] = 0;
		if (dijGraph[start][i] < MAX_INT)
		{
			firstHop[i] = i;
		}
		else
		{
			firstHop[i] = MAX_INT;
		}
	}

	distance[start] = 0;
	visitedNodes[start] = 1;
	count = 1;

	while (count < MAX_NODES - 1)
	{
		mindistance = MAX_INT;
		for (int i = 0; i < MAX_NODES; i++)
		{
			if (distance[i] < mindistance && !visitedNodes[i])
			{
				mindistance = distance[i];
				nextnode = i;
			}
		}
		visitedNodes[nextnode] = 1;
		for (int i = 0; i < MAX_NODES; i++)
		{
			if (!visitedNodes[i])
			{
				if (mindistance + dijGraph[nextnode][i] < distance[i] || ((mindistance + dijGraph[nextnode][i] == distance[i]) && (firstHop[nextnode] < firstHop[i])))
				{
					distance[i] = mindistance + dijGraph[nextnode][i];
					pred[i] = nextnode;
					firstHop[i] = firstHop[nextnode];
				}
			}
		}
		count++;
	}
}

int nextHop(int dest)
{
	for (int i = 0; i < MAX_NODES; i++)
	{
		if (pred[dest] != nodeId)
		{
			dest = pred[dest];
		}
		else
		{
			break;
		}
	}
	if (network[nodeId][dest])
	{
		return dest;
	}
	else
	{
		return -1;
	}
}

sendPacket getSendMsgPacket(char *message)
{
	sendPacket packet = {0, 0, "\0"};
	short int destID;
	memcpy(&destID, message + 4, sizeof(short int));
	short int dest;
	dest = ntohs(destID);
	packet.destination = dest;
	char *token = message + 4 + sizeof(short int);
	strcpy(packet.message, token);
	packet.nextDestination = nextHop(packet.destination);
	return packet;
}

recvPacket getReceivedPacket(char *message)
{
	recvPacket pkt = {0, 0, {0}, 0};
	short int seq;
	memcpy(&seq, message + 4, sizeof(short int));
	pkt.seq = ntohs(seq);
	memcpy(&seq, message + 4 + sizeof(short int), sizeof(short int));
	pkt.from = ntohs(seq);
	int i = 2;
	while (1)
	{
		if (!strncmp(message + 4 + sizeof(short int) * i, "*", 1))
		{
			break;
		}
		else
		{
			memcpy(&seq, message + 4 + sizeof(short int) * i, sizeof(short int));
			short int to = ntohs(seq);
			i++;
			memcpy(&seq, message + 4 + sizeof(short int) * i, sizeof(short int));
			i++;
			pkt.fromCost[to] = ntohs(seq);
		}
	}
	pkt.length = 4 + sizeof(short int) * i + 1;
	return pkt;
}

void sendToOthers(char *buffer, int fromNode, int source, int length)
{

	for (int i = 0; i < MAX_NODES; i++)
	{
		if (neighbour[i] && i != fromNode && i != source)
		{
			sendToSpecificDestination(buffer, i, length);
		}
	}
}

void announceChanges(int n)
{
	char message[MAX_NODES] = {"\0"};
	if (!broadcastSequenceFrom[nodeId])
	{
		broadcastSequenceFrom[nodeId] = 1;
		broadcastSeq = 1;
	}
	int length = updateBroadcastMessage(message);
	hackyBroadcast(message, length);
}

void checkNeighborAlive()
{
	struct timeval current;
	int timeout = 2;
	gettimeofday(&current, NULL);
	for (int i = 0; i < MAX_NODES; i++)
	{
		if (!neighbour[i])
		{
			continue;
		}
		if (current.tv_sec - heartBeatHistory[i].tv_sec > timeout)
		{
			neighbour[i] = 0;
			network[nodeId][i] = 0;
			network[i][nodeId] = 0;
			broadcastSeq++;
			broadcastSequenceFrom[nodeId] = broadcastSeq;
		}
	}
}

void listenForNeighbors(char *log)
{
	FILE *fp;
	fp = fopen(log, "w");
	char fromAddress[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLength;
	char recvBuf[1000] = {'\0'};
	int bytesReceived;

	while (1)
	{
		memset(recvBuf, '\0', 1000);
		theirAddrLength = sizeof(theirAddr);
		if ((bytesReceived = recvfrom(currentSocket, recvBuf, 1000, 0,
									  (struct sockaddr *)&theirAddr, &theirAddrLength)) == -1)
		{
			perror("connectivity listener : recvfrom failed");
			exit(1);
		}
		inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddress, 100);
		short int heardFrom = -1;

		if (strstr(fromAddress, "10.1.1."))
		{
			heardFrom = atoi(
				strchr(strchr(strchr(fromAddress, '.') + 1, '.') + 1, '.') + 1);
			if (!neighbour[heardFrom])
			{
				neighbour[heardFrom] = 1;
				network[nodeId][heardFrom] = cost[heardFrom];
				network[heardFrom][nodeId] = cost[heardFrom];
				broadcastSeq++;
				broadcastSequenceFrom[nodeId] = broadcastSeq;
			}
			gettimeofday(&heartBeatHistory[heardFrom], 0);
		}
		if (!strncmp(recvBuf, "send", 4))
		{
			dijkstraAlgo();
			char logLine[512] = {"\0"};
			sendPacket msg = getSendMsgPacket(recvBuf);
			if (heardFrom < 0 && msg.nextDestination > 0)
			{
				sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", msg.destination, msg.nextDestination, msg.message);
				sendToSpecificDestination(recvBuf, msg.nextDestination, 4 + sizeof(short int) + strlen(msg.message));
			}
			else if (msg.destination == nodeId)
			{
				sprintf(logLine, "receive packet message %s\n", msg.message);
			}
			else if (msg.nextDestination == -1)
			{
				sprintf(logLine, "unreachable dest %d\n", msg.destination);
			}
			else
			{
				sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", msg.destination, msg.nextDestination, msg.message);

				sendToSpecificDestination(recvBuf, msg.nextDestination, 4 + sizeof(short int) + strlen(msg.message));
			}
			fprintf(fp, "%s", logLine);
			fflush(fp);
		}
		else if (!strncmp(recvBuf, "ANNOUNC", 7))
		{

			char networkDetails[512] = {"\0"};
			int length = updateMessageWithMyNetworkDetails(networkDetails);
			sendToSpecificDestination(networkDetails, heardFrom, length);
		}
		else if (!strncmp(recvBuf, "NETW", 4) && !networkInfoReceived)
		{
			updateNetworkFromNeighborInfo(recvBuf);
			networkInfoReceived = 1;
		}
		else if (!strncmp(recvBuf, "BROA", 4))
		{
			pthread_mutex_lock(&m_forwardFlag);
			struct recvPacket broadcastMessage = getReceivedPacket(recvBuf);
			if (broadcastMessage.seq > broadcastSequenceFrom[broadcastMessage.from])
			{
				broadcastSequenceFrom[broadcastMessage.from] = broadcastMessage.seq;

				for (int i = 0; i < MAX_NODES; i++)
				{
					int fromCost = broadcastMessage.fromCost[i];

					network[broadcastMessage.from][i] = fromCost;
					network[i][broadcastMessage.from] = fromCost;
				}
				sendToOthers(recvBuf, heardFrom, broadcastMessage.from, broadcastMessage.length);
			}
			pthread_mutex_unlock(&m_forwardFlag);
		}
		else if (!strncmp(recvBuf, "cost", 4))
		{
			// do nothing
		}
		checkNeighborAlive();
	}
	close(currentSocket);
}
