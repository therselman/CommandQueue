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
#include <queue>

#include "CommandQueue.hpp"

int ret()
{
	return 1;
}

int inc( int a )
{
	return ++a;
}

int add2( int a, int b )
{
	return a + b;
}

int add3( int a, int b, int c )
{
	return a + b + c;
}

int add4( int a, int b, int c, int d )
{
	return a + b + c + d;
}

int add5( int a, int b, int c, int d, int e )
{
	return a + b + c + d + e;
}

int add6( int a, int b, int c, int d, int e, int f )
{
	return a + b + c + d + e + f;
}

CommandQueue commandQ;



int main()
{
	const char* hw = "";

	const char*( *test )( ) = [] { return "Hello World\n"; };		//	can create a `function pointer` variable ... it's easier when working with lambdas!
	commandQ.returns( test, &hw );
	commandQ.join();
	printf( hw );

	commandQ.returns( (const char*(*)()) [] { return "Hello World\n"; }, &hw ); // Or you can do the old-school cast directly ... a lot harder because the compiler will confust the shit out of you!
	commandQ.join();
	printf( hw );


	//
	//		Opening a file (or calling API commands)
	//
	FILE* f;

	commandQ.returns( (FILE*(*)(const char*)) [](const char* str) { return fopen( str, "r" ); }, &f, "examples.cpp" );	// I recommend first putting it in a variable first, like above, then you can move it directly here!
	//	do other work
	commandQ.join();
	if (f) fclose(f);

	commandQ.returns( fopen, &f, "examples.cpp", "r" );
	//	do other work
	commandQ.join();
	if (f) fclose(f);


	int r = 0;

	commandQ.returns( ret, &r );									//	if you get: error C2100: illegal indirection ... remember to use `&my_return_value`
	//	do other work
	commandQ.join();
	printf( "%d\n", r );

	commandQ.returns( inc, &r, 1 );									//	if you get: error C2198: 'int (__cdecl *const )(int)': too few arguments for call ... you forgot to add one or more parameters!
	//	do other work
	commandQ.join();
	printf( "%d\n", r );

	commandQ.returns( add2, &r, 1, 2 );
	//	do other work
	commandQ.join();
	printf( "%d\n", r );

	commandQ.returns( add3, &r, 1, 2, 3 );
	//	do other work
	commandQ.join();
	printf( "%d\n", r );

	commandQ.returns( add4, &r, 1, 2, 3, 4 );
	//	do other work
	commandQ.join();
	printf( "%d\n", r );

	commandQ.returns( add5, &r, 1, 2, 3, 4, 5 );
	//	do other work
	commandQ.join();
	printf( "%d\n", r );

	commandQ.returns( add6, &r, 1, 2, 3, 4, 5, 6 );
	//	do other work
	commandQ.join();
	printf( "%d\n", r );

	getchar();
	return 0;
}
