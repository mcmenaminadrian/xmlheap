#include <stdio.h>
#include <stdlib.h>
#include <expat.h>
#include <unistd.h>


//XMLHEAP is licensed for reuse under Version 3 of the GNU GPL
//Copyright (c) Adrian McMenamin, 2014

//globals - this is C after all
#define BUFFSZ 512

static char buffer[BUFFSZ];
FILE *inFile;
FILE *outFile;
char *inFileStr = NULL;
char *outFileStr = NULL;
long boundaryCount = 1000000;
long nextBoundary;
int pageShift = 12;
int ticksPerLoad = 100;
int excludeLoadTime = 0;
long currentCount = 0;
char *tickStr = NULL;
static XML_Parser *pp_ctrl;

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

struct BitArray *CreateBitArray(long pageNumber)
{
	struct BitArray *bArray = (struct BitArray *)
		malloc(sizeof(struct BitArray));
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
	bArray->accessCount = 0;
	bArray->nextBitArray = NULL;
	bArray->started = 0;
	bArray->ended = 0;
	bArray->lastTick = 0;
	bArray->idleTicks = 0;
	bArray->pageNumber = pageNumber;
	return bArray;
}

struct BitArray *TraverseBitArray(long pageNumber, struct BitArray *nextBA)
{
	if (nextBA == NULL) {
		struct BitArray *emptyBA = CreateBitArray(pageNumber);
		emptyBA->nextBitArray = headBitArray;
		headBitArray = emptyBA;
		return emptyBA;
	} else {
		if (nextBA->pageNumber == pageNumber) {
			return nextBA;
		}
		else {
			return TraverseBitArray(pageNumber,
				nextBA->nextBitArray);
		}
	}
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

struct BitArray *GetBitArray(long pageNumber)
{
	return TraverseBitArray(pageNumber, headBitArray);
}

void MarkBit(int offset, int length, long pageNumber)
{
	struct BitArray *ourBA = TraverseBitArray(pageNumber, headBitArray);
	ourBA->accessCount += length;
	for (int i = offset; i < offset + length; i++) {
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
i	GetChainDetails(struct ChainDetails *details, struct BitArray *nextBA)
{
	if (nextBA == NULL) {
		if (!excludeLoadTime) {
			long pageLoadFactor = (1 << pageShift) * ticksPerLoad *
				details->pages;
			details->time += pageLoadFactor;
			details->idleTime += pageLoadFactor;
		}
		return details;
	} else {
		details->pages++;
		details->time += nextBA->ended - nextBA->started;
		details->idletime += nextBA->idleTicks;
		details->accesses += nextBA->accessCount;
		for (int i = 0; i < 1 << pageShift; i++) {
			details->range += nextBA->bits[i];
		}
		return GetChainDetails(details, nextBA->nextBitArray);
	}
}

static void XMLCALL EndHandler(void *data, const XML_Char *name)
{
	if (strcmp(name, "code") == 0 || strcmp(name, "rw") == 0) {
		long tick = atol(strTick);
		free(strTick);
		XML_SetCharcterDataHandler(*pp_ctrl, NULL);
		if (headBitArray->idleTime == 0) {
			headBitArray->lastTick = tick;
		} else {
			headBitArray->idleTicks += tick - headBitArray->lastTick;
			headBitArray->lastTick = tick;
		}
		return;
	}
	if (strcmp(name, "frame") == 0) {
		headBitArray->idleTicks += ended - headBitArray->lastTick;
		if (headBitArray->ended >= nextBoundary) {
			struct ChainDetails *nextChain =
				(struct ChainDetails *)malloc(sizeof(
				struct ChainDetails));
			GetChainDetails(nextChain, headBitArray);
			fprintf(outFile, ltoa(ended, 10));
			fprintf(outFile, ", ");
			fprintf(outFile, ltoa(nextChain->pages, 10));
			fprintf(outFile, ", "); 
			fprintf(outFiile, ltoa(nextChain->idletime, 10));
			fprintf(outFile, ", ");
			fprintf(outFile, ltoa(nextChain->range, 10));
			fprintf(outFile, ", ");
			fprintf(outFile, ltoa(nextChain->accesses, 10));
			fprintf(outFile, "\n");
			free(nextChain);
			CleanBitArrayChain(headBitArray);
			nextBoundary += boundaryCount;
		}
	}
}

static void XMLCALL
	StartHandler(void *data, const XML_Char *name, const XML_Char **attr)
{
	if (strcmp(name, "page") == 0) {
		long pageN;
		long startTime;
		long endTime;
		for (int i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "frame") == 0) {
				pageN = atol(attr[i + 1]);
				break;
			}
			if (strcmp(attr[i], "in") == 0) {
				startTime = atol(attr[i + 1]);
				break;
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
		long offset, size;
		if (strcmp(name, "rw") == 0 || strcmp(name, "code") == 0){
			for (int i = 0; attr[i]; i += 2) {
				if (strcmp(attr[i], "address") == 0) {
					offset = atol(attr[i + 1]) & pageMask;
					break;
				}
				if (strcmp(attr[i], "size") == 0) {
					size = atol(attr[i + 1]);
				}
			}
			MarkBit(offset, size, headBitArray->pageNumber);
			XML_SetCharcterDataHandler(*pp_ctrl, TickHandler);
			tickStr = (char *)calloc(1, BUFFSZ);
		}
	}
}



int main(int argc, char* argv[])
{
	int i, done;
	inFile = stdin;
	outFile = stdout;
	//parse command line
	while ((i = getopt(argc, argv, "i:o:b:p:t:x")) != -1)
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
		}

	for (i = 0; i < pageShift) {
		pageMask |= 1 << i;
	}
	nextBoundary = boundaryCount;

	//load XML parser
	XML_Parser p_ctrl = XML_ParserCreate("UTF-8");
	if (!p_ctrl) {
		fprintf(stderr, "Could not create XML parser\n");
		exit(-1);
	}
	pp_ctrl = &p_ctrl;

	XML_SetStartElementHandler(p_ctrl, StartHandler);
	XML_SetEndElementHandler(p_ctrl, EndHandler);

	if (inFileStr) {
		inFile = fopen(inFileStr, "r");
		if (inFile == NULL) {
			fprintf(stderr, "Could not open %s\n", inFileStr);
			XML_ParserFree(p_ctrl);
			exit(-1);
		}
	}
	if (outFileStr) {
		outFile = fopen(outFileStr, "w");
		if (outFile == NULL) {
			fprintf(stderr, "Could not open %s\n", outFileStr);
			XML_ParserFree(p_ctrl);
			exit(-1);
		}
	}
	fprintf(outFile, "Ticks, Pages, Idle, Range, Access\n");

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

	//TODO: flush data to outFile

	XML_ParserFree(p_ctrl);
	if (inFileStr) {
		fclose(inFile);
	}
	if (outFileStr) {
		fclose(outFile);
	}

	return 0;
}
