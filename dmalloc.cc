#define M61_DISABLE 1
#include "dmalloc.hh"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cassert>

// You may write code here.
// (Helper functions, types, structs, macros, globals, etc.)

//Okay, here is our profiler data
unsigned long long nactive; // number of active allocations [#malloc − #free]
unsigned long long active_size; // number of bytes in active allocations
unsigned long long ntotal; // number of allocations, total
unsigned long long total_size; // number of bytes in allocations, total
unsigned long long nfail; // number of failed allocation attempts
unsigned long long fail_size; // number of bytes in failed allocation attempts
unsigned int* heap_min; // smallest address in any region ever allocated
unsigned int* heap_max; // largest address in any region ever allocated

struct Chunk
{
	unsigned int Size;
	const char* File;
	unsigned short Line;
	unsigned int Hits;
	struct Chunk* Next;
	struct Chunk* Prev;
} *Chunks, *Last;


/// dmalloc_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then dmalloc_malloc must
///    return a unique, newly-allocated pointer value. The allocation
///    request was at location `file`:`line`.

void* dmalloc_malloc(size_t sz, const char* file, long line) 
{
	(void) file, (void) line;   // avoid uninitialized variable warnings
	// Your code here.
	
	
	
	
	//Account for our memory cookies! The cookie size can be larger too. 16 is good enough
	size_t RealSize = sizeof(struct Chunk) + 32 + sz;
	void* Data = base_malloc(RealSize);
	if (!Data)
	{
		++nfail;
		fail_size += sz;
		fprintf(stderr, "Error: malloc failed to allocate %u bytes in %s:%lu", sz, file, line);
		return Data;
	}
	//Special case for heap_min, we don't want it to remain 0!
	if (!heap_min)
		heap_min = (unsigned int*)Data;

	//Region statistics
	if ( ((unsigned int*)Data) < heap_min)
		heap_min = (unsigned int*)Data;
	else if ( ((unsigned int*)Data) > heap_max)
		heap_max = (unsigned int*)Data;

	//Register profiler data
	++nactive;
	active_size += sz;
	++ntotal;
	++total_size;
	
	//This is how we would align memory in case malloc wouldn't already do it for us.
	/*
	if(Data & 15)
	{
		Data += 16;	//Increment by 16 to make sure we don't move outside our memory region. Of course the sz allocated needs to add 16 bytes too
		Data &= ~(15);	Chop off the last 4 bits
	}
	*/

	//This is great because we can use the file and line to hint exactly which memory chunk that got written to out of bounds
	//And instead of allocating new memory for our chunk. We embed it inside the allocated memory's meta data
	struct Chunk* Chk = (struct Chunk*)Data;
	if (!Chunks)
	{
		Chunks = Chk;
		Last = Chk;
	}
	else
	{
		Last->Next = Chk;
		Chk->Prev = Last;
		Last = Chk;
	}
	//I will not be using a hash map of any sort here because I do not want any additional allocations to take place. In production code, malloc, realloc and free are swapped at the linker level in any real debugger. Not using macros
	//Then, calling malloc inside this function will by nature cause an infinite loop. Instead we use a meta header in the block we allocate from base_malloc. 
	Chk->File = file;	//File name passed to this must always be a constant string litteral. This shouldn't be done in production code unless we can assert specific usage, but should be copied instead. The document however allows it with these specific tests.
	Chk->Line = line;
	Chk->Size = sz;

	char* Cookie = (char*)Data;
	Cookie += sizeof(struct Chunk);
	memset(Cookie, 0xDEADDEAD, 16);	//-- Assign cookies
	Cookie += sz + 16;
	memset(Cookie, 0xDEADDEAD, 16);
	Cookie -= sz;

	//Done?
	return Cookie;
}


/// dmalloc_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`, which must have been
///    returned by a previous call to dmalloc_malloc. If `ptr == NULL`,
///    does nothing. The free was called at location `file`:`line`.

void dmalloc_free(void* ptr, const char* file, long line) {
	(void) file, (void) line;   // avoid uninitialized variable warnings
	if (!ptr)
		return;

	//The easiest, and most efficient thing is to check for sanity, and error on anything else
	struct Chunk* chk = Chunks;
	while(chk)
	{
		unsigned char* Block = (unsigned char*)chk;	//Avoid doing messy and lengthy casts in the if expression
		if(ptr == ( Block + 16 + sizeof(struct Chunk) ))
		{
			chk->Prev->Next = chk->Next;
			chk->Next->Prev = chk->Prev;
			--nactive;
			active_size -= chk->Size;
			base_free(chk);	//It's valid. Free the entire block, including meta data. We don't want to pass an invalid pointer here!
			return;
		}
		chk = chk->Next;
	}
	//At this point, we know for sure, that this pointer is invalid.
	//Figure out how bad it is, and where the issue might be
	//error check crazy pointers
	if(ptr < heap_min || ptr > heap_max)
	{
		fprintf(stderr, "Error %s:%lu : attempted to free memory outside heap region, 0x%p\n",file, line, ptr);
		return;	//Should we exit or just move on?
	}
	chk = Chunks;
	while(chk)
	{
		unsigned char* Block = (unsigned char*)chk;
		Block += 16 + sizeof(struct Chunk);
		if(ptr > Block && ptr < (Block + chk->Size))	//Somewhere within this block
		{
			fprintf(stderr, "Error %s:%lu : invalid memory pointer is %u bytes inside a %u byte block allocated at %s:%u\n", file, line, (unsigned int)((unsigned char*)ptr - Block), chk->Size, chk->File, chk->Line);
			return;
		}
	}
	fprintf(stderr, "Error %s:%lu : invalid pointer passed to free %p is either a double free, or points to an unknown region within the heap.\n", file, line, ptr);
}
	

/// dmalloc_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. If `sz == 0`,
///    then must return a unique, newly-allocated pointer value. Returned
///    memory should be initialized to zero. The allocation request was at
///    location `file`:`line`.

void* dmalloc_calloc(size_t nmemb, size_t sz, const char* file, long line) {
	// Your code here (to fix test014).
	void* ptr = dmalloc_malloc(nmemb * sz, file, line);
	if (ptr) {
		memset(ptr, 0, nmemb * sz);
	}
	return ptr;
}


/// dmalloc_get_statistics(stats)
///    Store the current memory statistics in `*stats`.

void dmalloc_get_statistics(dmalloc_statistics* stats) {
	// Stub: set all statistics to enormous numbers
	memset(stats, 255, sizeof(dmalloc_statistics));
	// Your code here.
	stats->active_size = active_size;
	stats->fail_size = fail_size;
	stats->heap_max = (unsigned int)heap_max;
	stats->heap_min = (unsigned int)heap_min;
	stats->nactive = nactive;
	stats->nfail = nfail;
	stats->ntotal = ntotal;
	stats->total_size = total_size;
}


/// dmalloc_print_statistics()
///    Print the current memory statistics.

void dmalloc_print_statistics() {
	dmalloc_statistics stats;
	dmalloc_get_statistics(&stats);

	printf("alloc count: active %10llu   total %10llu   fail %10llu\n",
		   stats.nactive, stats.ntotal, stats.nfail);
	printf("alloc size:  active %10llu   total %10llu   fail %10llu\n",
		   stats.active_size, stats.total_size, stats.fail_size);
}


/// dmalloc_print_leak_report()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.

void dmalloc_print_leak_report() {
	// Your code here.
	printf("Leak report:\n");

	struct Chunk* chk = Chunks;
	while(chk)
		printf("Leaked: %s:%u Allocated %u bytes at %p\n", chk->File, chk->Line, chk->Size, ((unsigned char*)chk) + 16 + sizeof(struct Chunk));
	
}


/// dmalloc_print_heavy_hitter_report()
///    Print a report of heavily-used allocation locations.

void dmalloc_print_heavy_hitter_report() {
	// Your heavy-hitters code here
	//!!We do not want to profile line hits in malloc!! It's expensive, so instead we do that here. Since this function is only called once, and only when performance is not a major concern. It'll also be more efficient
	//since we don't have to loop everytime we call malloc. Otherwise malloc will be exponentially slower the more often it's being called
	struct Record
	{
		const char* File;
		unsigned short Line;
		unsigned int Hits;
		unsigned int Size;
	} *Records = (struct Record*) base_malloc(sizeof(struct Record) * nactive), *RLast = Records;

	unsigned char bNew = 0;
	memset(Records, 0x0, sizeof(struct Record)* nactive);
	assert(Records);	//-- May never be null!

	struct Chunk* chk = Chunks;
	//This could be faster
	while(chk)
	{
		
		bNew = 1;
		//Already recorded?
		for(struct Record* Rec = Records; Rec->File; ++Rec)
		{
			if (chk->File == Rec->File && chk->Line == Rec->Line)
				bNew = 0;
		}
		if (bNew)	//Nope
		{
			struct Chunk* chk2 = Chunks;
			while(chk2)
			{
				if (chk->File == chk2->File && chk->Line == chk2->Line)
				{
					RLast->File = chk2->File;
					RLast->Line = chk2->Line;
					++RLast->Hits;
					RLast->Size += chk2->Size;
				}
				chk2 = chk2->Next;
			}
			if (((double)RLast->Hits) / ((double)nactive) >= 0.2)
				printf("HIT REPORT: Allocation in %s:%u allocated a total of %u bytes, %u times (%f%%)\n", RLast->File, RLast->Line, RLast->Size, RLast->Hits, ((double)RLast->Hits) / ((double)nactive) * 100);
			++RLast;
		}
	}
	base_free(Records);
}
