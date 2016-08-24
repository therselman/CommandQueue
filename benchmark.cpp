
/*
MIT License

Copyright (c) 2016 Trevor Herselman

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Written by Trevor Herselman

The Command Queue is a C++11 header-only object, designed specifically for
high speed function calls on a separate thread.

Featuring a unique custom built low level, lock-free double buffered queue.

Executes the queue with a specially designed protocol,
dedicated to high speed function calls in just 6 CPU instructions!
*/

#include <stdio.h>
#include <stdarg.h>

#include <thread>
#include <chrono>

#include "CommandQueue.hpp"

uint32_t calls = 0;

void doWork()
{
	calls++;																				//	Just giving it something to do
}

int main()
{
	printf( "WARNING: To be fair, don't run this from inside the Visual Studio IDE!\nstd::thread will run about 10x slower! (hooks?)\nCompile and run from executable to be fair!\n\n" );

	printf( "Press any key to begin benchmarks" );
	getchar();

	//
	//		Command Queue Benchmark
	//
	printf( "\n... running Command Queue benchmark, please wait ...\n" );

	auto start = std::chrono::steady_clock::now();											//	I've included the FULL startup AND shutdown of the Command Queue Object, double-buffers AND shutdown sequence in the benchmark! In a normal production environment, you can keep the Command Queue running for the duration of your Application!
	CommandQueue* commandQ = new CommandQueue();											//	Optional buffer sizes (doesn't have to be power-of-two!): 256K = 262144 / 512K = 524288 / 1MB = 1048576 / 2MB = 2097152 / 4MB = 4194304	-	You can try different buffer sizes, but I recommend just leave it for your production code, the buffers will expand as needed, you can see even if you make the buffers 10MB it doesn't affect the outcome! I didn't see ANY difference in speed! So just leave them and let them do their shit!
	for ( int i = 0; i < 100000000; i++ )													//	100000000 iterations == 100,000,000 == 100 million ... takes 10.56s on my Core i5 ... so 10 million per second
		commandQ->execute( doWork );
	commandQ->join();
	commandQ->printBufferSizes();
	delete commandQ;																		//	I've also included the `delete` inside the benchmark ... to be as fair as possible!
	auto end = std::chrono::steady_clock::now();

	std::chrono::steady_clock::duration time_span = end - start;
	double diff = double(time_span.count()) * std::chrono::steady_clock::period::num / std::chrono::steady_clock::period::den;
	printf( "time taken: %f sec\n", diff );
	printf( "Function calls: %d\n", calls );


	calls = 0;	//	reset calls counter


	//
	//		std::thread Benchmark															//	This is a bit unfair! Because the Command Queue doesn't constantly create new threads ... but that's the whole point!!! Why create and destroy threads!?!? They are SUPER costly! And that's what I want to show you! In the time it takes you to create a new std::thread, I can execute 500+ function calls! std::thread isn't a silver bullet for parallelism! You need to consider what you are doing carefully! And BENCHMARK!
	//
	printf( "\n... now running std::thread benchmark, please wait ...\n" );

	start = std::chrono::steady_clock::now();
	for ( int i = 0; i < 200000; i++ )														//	10.884914s (200000) vs. 10.56s (100000000) == 500x faster! ... don't test this in the VS IDE! Even on Release build! Because Visual Studio will hook into the std::thread, slowing it down! Test directly from compiled executable!
		std::thread( doWork ).join();
	end = std::chrono::steady_clock::now();

	time_span = end - start;
	diff = double(time_span.count()) * std::chrono::steady_clock::period::num / std::chrono::steady_clock::period::den;
	printf( "time taken: %f sec\n", diff );
	printf( "Function calls: %d\n", calls );


	//
	//		The End
	//
	printf( "\nThe End\npress any key" );
	getchar();
	return 0;
}
