#include <stdio.h>
#include <stdlib.h>
#include <expat.h>
#include <unistd.h>
#include <string.h>


//XMLHEAP is licensed for reuse under Version 3 of the GNU GPL
//Copyright (c) Adrian McMenamin, 2014

//globals - this is C after all
#define BUFFSZ 512
#define smBUFFSZ 64

static char buffer[BUFFSZ];
FILE *inFile;
FILE *outFile;
char *inFileStr = NULL;
char *outFileStr = NULL;
char *threadFileStr = "lru2thread.csv";
long boundaryCount = 1000000;
long nextBoundary;
int pageShift = 12;
int ticksPerLoad = 100;
int excludeLoadTime = 0;
long currentCount = 0;
char *strTick;
int tickIP = 0;
unsigned long pageMask = 0;
static XML_Parser *pp_ctrl;

struct ThreadMap {
	long count;
	int threads;
	struct ThreadMap *next;
};

struct BitArray {
	long pageNumber;
	char *bits;
	long accessCount;
	long started;
	long ended;
	long lastTick;
	long idleTicks;
	struct BitArray *nextBitArray;
};

struct BitArray *headBitArray = NULL;
struct ThreadMap *headThreadMap = NULL;
struct ThreadMap *currentThreadMap = NULL;

struct ThreadMap *lastThreadMap(struct ThreadMap *head)
{
	if (head == NULL) {
		return NULL;
	}
	if (head->next == NULL) {
		return head;
	}
	return lastThreadMap(head->next);
}

void cleanThreadMap(struct ThreadMap *head)
{
	if (head == NULL) {
		return;
	}
	struct ThreadMap *nextMap = head->next;
	free(head);
	cleanThreadMap(nextMap);
}

struct BitArray *CreateBitArray(long pageNumber)
{
	struct BitArray *bArray = (struct BitArray *)
		calloc(1, sizeof(struct BitArray));
	if (!bArray) {
		fprintf(stderr, "Failed to create BitArray structure.\n");
		return NULL;
	}
	bArray->bits = (char*)calloc(1, 1 << pageShift);
	if (!bArray->bits) {
		fprintf(stderr, "Failed to initialise bits in BitArray.\n");
		free(bArray);
		return NULL;
	}
	bArray->pageNumber = pageNumber;
	return bArray;
}

void CleanBitArrayChain(struct BitArray *nextBA)
{
	if (nextBA == NULL) {
		return;
	}
	struct BitArray *nextOne = nextBA->nextBitArray;
	free(nextBA->bits);
	free(nextBA);
	CleanBitArrayChain(nextOne);
}

void MarkBit(int offset, int length, struct BitArray *ourBA)
{
	int i;
	ourBA->accessCount += length;
	for (i = offset; i < offset + length; i++) {
		ourBA->bits[i] = 1;
	}
}

struct ChainDetails {
	long pages;
	long time;
	long idletime;
	long range;
	long accesses;
};

struct ChainDetails*
	GetChainDetails(struct ChainDetails *details, struct BitArray *nextBA)
{
	if (nextBA == NULL) {
		if (!excludeLoadTime) {
			long pageLoadFactor = (1 << pageShift) * ticksPerLoad *
				details->pages;
			details->time += pageLoadFactor;
			details->idletime += pageLoadFactor;
		}
		return details;
	} else {
		int i;
		details->pages++;
		details->time += nextBA->ended - nextBA->started;
		details->idletime += nextBA->idleTicks;
		details->accesses += nextBA->accessCount;
		for (i = 0; i < 1 << pageShift; i++) {
			details->range += nextBA->bits[i];
		}
		return GetChainDetails(details, nextBA->nextBitArray);
	}
}

static void XMLCALL TickHandler(void *data, const XML_Char *s, int len)
{
	strncpy((char*)&(strTick[tickIP]), s, len);
	tickIP += len;
}

static void writeOutChain()
{
	char strEnd[smBUFFSZ];
	char strPages[smBUFFSZ];
	char strIdle[smBUFFSZ];
	char strRange[smBUFFSZ];
	char strAccess[smBUFFSZ];
	struct ChainDetails *nextChain =
		(struct ChainDetails *)calloc(1, sizeof(
		struct ChainDetails));
	GetChainDetails(nextChain, headBitArray);
	sprintf(strEnd, "%ld", headBitArray->ended);
	sprintf(strPages, "%ld", nextChain->pages);
	sprintf(strIdle, "%ld", nextChain->idletime);
	sprintf(strRange, "%ld", nextChain->range);
	sprintf(strAccess, "%ld", nextChain->accesses);
	fprintf(outFile, "%s, %s, ", strEnd, strPages);
	fprintf(outFile, "%s, %s, ", strIdle, strRange);
	fprintf(outFile, "%s, %i\n", strAccess, currentThreadMap->threads);
	free(nextChain);
	nextChain = NULL;
	CleanBitArrayChain(headBitArray);
	headBitArray = NULL;
	nextBoundary += boundaryCount;
}

static void XMLCALL EndHandler(void *data, const XML_Char *name)
{
	if (strcmp(name, "code") == 0 || strcmp(name, "rw") == 0) {
		long tick = atol(strTick);
		free(strTick);
		strTick = NULL;
		tickIP = 0;
		XML_SetCharacterDataHandler(*pp_ctrl, NULL);
		if (headBitArray->idleTicks == 0) {
			headBitArray->lastTick = tick;
		} else {
			headBitArray->idleTicks += tick - headBitArray->lastTick;
			headBitArray->lastTick = tick;
		}
		return;
	}
	if (strcmp(name, "page") == 0) {
		headBitArray->idleTicks +=
			headBitArray->ended - headBitArray->lastTick;
		if (currentThreadMap->next) {
			if (currentThreadMap->next->count
				< headBitArray->ended) {
				currentThreadMap = currentThreadMap->next;
				writeOutChain();
				return;
			}
		}
		if (headBitArray->ended >= nextBoundary) {
			writeOutChain();
		}
	}
}

static void XMLCALL
	StartHandler(void *data, const XML_Char *name, const XML_Char **attr)
{
	if (strcmp(name, "page") == 0) {
		long pageN = 0;
		long startTime = 0;
		long endTime = 0;
		int i;
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "frame") == 0) {
				pageN = atol(attr[i + 1]);
				continue;
			}
			if (strcmp(attr[i], "in") == 0) {
				startTime = atol(attr[i + 1]);
				continue;
			}
			if (strcmp(attr[i], "out") == 0) {
				endTime = atol(attr[i + 1]);
			}
		}
		struct BitArray *newBA = CreateBitArray(pageN);
		if (!newBA) {
			fprintf(stderr,
				"Failed to create BitArray structure.\n");
			return;
		}
		newBA->started = startTime;
		newBA->ended = endTime;
		newBA->nextBitArray = headBitArray;
		headBitArray = newBA;
	} else {
		long offset = 0;
		long size = 0;
		int i;
		if (strcmp(name, "rw") == 0 || strcmp(name, "code") == 0){
			for (i = 0; attr[i]; i += 2) {
				if (strcmp(attr[i], "address") == 0) {
					offset = atol(attr[i + 1]) & pageMask;
				}
				else {
					if (strcmp(attr[i], "size") == 0) {
						size = atol(attr[i + 1]);
					}
				}
			}
			//TODO: Fix this ugly hack
			if (offset + size > 1 << pageShift) {
				size = (1 << pageShift) - offset;
			}
			MarkBit(offset, size, headBitArray);
			strTick = (char *)calloc(1, smBUFFSZ);
			XML_SetCharacterDataHandler(*pp_ctrl, TickHandler);
		}
	}
}

int main(int argc, char* argv[])
{
	int i, done;
	inFile = stdin;
	outFile = stdout;
	//parse command line
	while ((i = getopt(argc, argv, "i:o:b:p:t:f:x")) != -1){
		switch(i) {
		case 'i':
			inFileStr = optarg;
			break;
		case 'o':
			outFileStr = optarg;
			break;
		case 'b':
			boundaryCount = atol(optarg);
			break;
		case 'p':
			pageShift = atoi(optarg);
			break;
		case 't':
			ticksPerLoad = atoi(optarg);
			break;
		case 'x':
			excludeLoadTime = 1;
			break;
		case 'f':
			threadFileStr = optarg;
			break;
		}
	}

	for (i = 0; i < pageShift; i++) {
		pageMask |= 1 << i;
	}
	nextBoundary = boundaryCount;

	//get thread stuff
	FILE *threadFile = fopen(threadFileStr, "r");
	if (!threadFile) {
		fprintf(stderr, "Could not open %s\n", threadFileStr);
		exit(-1);
	}
	
	char *threadBuff = (char *)calloc(1, BUFFSZ);
	if (!threadBuff) {
		fprintf(stderr, "Could not create threadBuff\n");
		exit(-1);
	}
	struct ThreadMap *threadValues = (struct ThreadMap *) calloc(1, 
		sizeof(struct ThreadMap));
	if (!threadValues) {
		fprintf(stderr, "Could not create threadValues.\n");
		free(threadBuff);
		exit(-1);
	}
	while (fgets(threadBuff, BUFFSZ, threadFile) != NULL) {
		struct ThreadMap *workingMap;
		if (headThreadMap == NULL) {
			headThreadMap = (struct ThreadMap *)
				calloc(1, sizeof(struct ThreadMap));
			workingMap = headThreadMap;
		} else {
			struct ThreadMap *tail = lastThreadMap(headThreadMap);
			tail->next = (struct ThreadMap *)
				calloc(1, sizeof(struct ThreadMap));
			workingMap = tail->next;
		}
		if (!workingMap) {
			fprintf(stderr, "Failed on thread file reading.\n");
			free(threadBuff);
			free(threadValues);
			exit(-1);
		}
		char *fragment = strtok(threadBuff, ",");
		workingMap->count = atol(fragment);
		fragment = strtok(NULL, ",");
		workingMap->threads = atoi(fragment);
	}
	currentThreadMap = headThreadMap;

	//load XML parser
	XML_Parser p_ctrl = XML_ParserCreate("UTF-8");
	if (!p_ctrl) {
		fprintf(stderr, "Could not create XML parser\n");
		cleanThreadMap(headThreadMap);
		free(threadBuff);
		free(threadValues);
		exit(-1);
	}
	pp_ctrl = &p_ctrl;

	XML_SetStartElementHandler(p_ctrl, StartHandler);
	XML_SetEndElementHandler(p_ctrl, EndHandler);

	if (inFileStr) {
		inFile = fopen(inFileStr, "r");
		if (inFile == NULL) {
			fprintf(stderr, "Could not open %s\n", inFileStr);
			cleanThreadMap(headThreadMap);
			free(threadBuff);
			free(threadValues);
			XML_ParserFree(p_ctrl);
			exit(-1);
		}
	}
	if (outFileStr) {
		outFile = fopen(outFileStr, "w");
		if (outFile == NULL) {
			fprintf(stderr, "Could not open %s\n", outFileStr);
			cleanThreadMap(headThreadMap);
			free(threadBuff);
			free(threadValues);
			XML_ParserFree(p_ctrl);
			exit(-1);
		}
	}
	fprintf(outFile, "Ticks, Pages, Idle, Range, Access, Threads\n");

	do {
		long len = fread(buffer, 1, sizeof(buffer), inFile);
		done = len < sizeof(buffer);

		if (XML_Parse(p_ctrl, buffer, len, 0) == 0) {
			enum XML_Error errcode = XML_GetErrorCode(p_ctrl);
			fprintf(stderr,
				"ERROR: %s\n", XML_ErrorString(errcode));
			fprintf(stderr, "Line number: %lu ", 
				XML_GetCurrentLineNumber(p_ctrl));
			fprintf(stderr, "column number: %lu\n",
				XML_GetCurrentColumnNumber(p_ctrl));
			break;		}
	} while(!done);

	writeOutChain();
	XML_ParserFree(p_ctrl);
	if (inFileStr) {
		fclose(inFile);
	}
	if (outFileStr) {
		fclose(outFile);
	}
	cleanThreadMap(headThreadMap);
	free(threadBuff);
	free(threadValues);

	return 0;
}
