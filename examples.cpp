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

class MyQueueClass : public CommandQueue						//	basic extended object, this object will have its own dedicated thread for messages/events etc. very convenient to run some things in parallel, like an input queue, windows messages, network data queues (advanced), gameplay logic etc.
{
	int messages = 0;
private:
	static void addMessage_cmd( MyQueueClass *_this )
	{
		printf( "Receiving new message from another thread ...\n" );
		_this->messages++;
	}
public:
	void addMessage()
	{
		//execute( MyQueueClass::cmdPrintf, this );				//	alternative syntax
		(*this)( MyQueueClass::addMessage_cmd, this );
	}
};

void cmdPrintf( const char* str )								//	This is very tricky because sometimes you will get a random message from the template/compiler about "Conversion loses qualifiers", I had to put `const` before the char (try compile without the const)! Because the input strings "Hello World" are `const` strings! It's a very confusing error message sometimes! You will think there is something wrong with my code, but it's actually that your input values don't match the function input parameters that you want to call! You can also cast your input in the execute() calls ...
{
	printf( str );
}

CommandQueue commandQ;											//	Thread 1 - Easy to use! Created at application startup! You can run any functions you want on this thread!

int main()
{
	CommandQueue* pCommandQ = new CommandQueue();				//	Thread 2 - Example of using the class as a pointer
	MyQueueClass* myQueue   = new MyQueueClass();				//	Thread 3 - Example of extended class

	commandQ( cmdPrintf, "Hello " );							//	Method 1	(functor)  =	uses overloaded function call operator () ... can also be chained: eg. commandQ( ... )( ... )( ... )( ... )
	commandQ.execute( cmdPrintf, "World 1\n" );					//	Method 2

	(*pCommandQ)( cmdPrintf, "Hello " );						//	Method 1	(functor)
	pCommandQ->execute( cmdPrintf, "World 2\n" );				//	Method 2

	commandQ( cmdPrintf, "Chained" )( cmdPrintf, " - link 1" )( cmdPrintf, " - link 2\n" );		//	This will NEVER execute out-of-order, and it will NEVER execute before "Hello World 1" because they are on the same object/thread/queue, executed sequentially!

	myQueue->addMessage();

	commandQ.join();											//	NOTE: Run this a few times, you should see the messages appear in different orders! Except the `Chained` calls ... anything on a single object is executed sequentially ... but we are using 3 threads here, so they can execute in different orders on the 3 threads ... but anything added to the queue of a single object will execute sequentially!
	pCommandQ->join();
	myQueue->join();

	delete pCommandQ;
	delete myQueue;

	printf( "\nRun me again to see the messages appear in a different order,\nbecause they are executed on different threads!\n\n" );
	printf( "Shutdown Complete\n" );							//	Thread 1 (commandQ) will shutdown with the application
	getchar();
	return 0;
}
