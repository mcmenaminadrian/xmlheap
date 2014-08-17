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
int pageShift = 12;
int ticksPerLoad = 100;
int excludeLoadTime = 0;

struct BitArray {
	long pageNumber;
	char *bits;
	long accessCount;
	struct BitArray *nextBitArray;
};

struct BitArray *headBitArray = NULL;

struct BitArray *CreateBitArray(long pageNumber)
{
	struct BitArray *bArray = (struct BitArray *)
		malloc(sizeof(struct BitArray));
	if (!bArray) {
		fprintf(stderr, "Failed to create BitArray structure\n");
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
	long range;
	long accesses;
};

struct ChainDetails*
GetChainDetails(struct ChainDetails *details, struct BitArray *nextBA)
{
	if (nextBA == NULL) {
		return details;
	} else {
		details->pages++;
		details->accesses += nextBA->accessCount;
		for (int i = 0; i < 1 << pageShift; i++) {
			details->range += nextBA->bits[i];
		}
		return GetChainDetails(details, nextBA->nextBitArray);
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

	//load XML parser
	XML_Parser p_ctrl = XML_ParserCreate("UTF-8");
	if (!p_ctrl) {
		fprintf(stderr, "Could not create XML parser\n");
		exit(-1);
	}

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
