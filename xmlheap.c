#include <stdio.h>
#include <stdlib.h>
#include <expat.h>
#include <unistd.h>


//XMLHEAP is licensed for reuse under Version 3 of the GNU GPL
//Copyright (c) Adrian McMenamin, 2014

//globals - this is C after all
#define BUFFSZ 512

static char buffer[BUFFSZ];
//if file pointers are NULL (default) use std*
FILE *inFile = NULL;
FILE *outFile = NULL;
char *inFileStr = NULL;
char *outFileStr = NULL
long boundaryCount = 1000000;
int pageShift = 12;
int ticksPerLoad = 100;
int excludeloadTime = 0;

struct BitArray {
	long pageNumber;
	char *bits;
	long accessCount;
	struct bitArray *nextBitArray;
}

struct BitArray *headBitArray = NULL;

struct BitArray *CreateBitArray(long pageNumber)
{
	struct BitArray *bArray = (struct BitArray *)
		malloc(sizeof(struct BitArray));
	if (!bArray) {
		fprintf(stderr, "Failed to create BitArray structure\n");
		return NULL;
	}
	bArray->bits = (char*)calloc(1 << pageShift);
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
		struct BitArray *emptyBA = CreateBitArray(pageNumber)
		emptyBA->nextBitArray = headBitArray;
		headBitArray = emptyBA;
		return emptyBA;
	} else {
		if (nextBA->pageNumber == pageNumber) {
			return nextBA;
		}
		else {
			return TraverseBitArray(nextBA->nextBitArray);
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
	CleanBitArray(nextOne);
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
}


struct ChainDetails*
GetChainDetails(struct ChainDetails *details, struct BitArray *nextBA)
{
	if (nextBA == NULL) {
		return details;
	} else {
		details->pages++;
		details->accesses += accessCount;
		for (int i = 0; i < 1 << pageShift; i++) {
			details->range += nextBA->bits[i];
		}
	return GetChainDetails(details, nextBA->nextBitArray);
}


int main(int argc, char* argv[])
{

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
