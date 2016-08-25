#ifndef __COMMAND_QUEUE_HPP__
#define __COMMAND_QUEUE_HPP__

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

#include <stdlib.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

typedef void ( *PFNCommandHandler ) ( void* data );

class CommandQueue
{
protected:																								//	protected - incase you want to extend it, so your derived object can access any functions it needs! You are welcome to extend or modify it!

	template< uint32_t count = 0 >
	struct command_t																					//	For reference only! I write the values directly with pointers! This is just the structure template!
	{
		PFNCommandHandler	function;
		uint32_t			size;
	//	char*				data[ count ];																//	`optional` member of the structure! Not all commands/function calls require data!
	};

	struct queue_buffer_t
	{
		char*				commands;
		uint32_t			size;
		uint32_t			used;
	};
	queue_buffer_t			buffer[ 2 ];

	std::atomic< queue_buffer_t* > primary	 = &buffer[ 0 ];
	std::atomic< queue_buffer_t* > secondary = &buffer[ 1 ];

	std::mutex				mtxDequeue;
	std::condition_variable cvDequeue;

	std::mutex				mtxJoin;
	std::condition_variable cvJoin;

	std::thread*			hThread;
	bool		volatile	shutdown = false;


	//
	//		thread()
	//
	void thread()
	{
		queue_buffer_t* buffer = secondary.exchange( nullptr );

		while ( true )
		{
			buffer = primary.exchange( buffer );

			while ( buffer == nullptr )
				buffer = secondary.exchange( nullptr );

			if ( buffer->used )
			{
				char* base_addr = buffer->commands;
				const char* end = buffer->commands + buffer->used;
				do																												//	The inner loop - 6 CPU instructions (VS2015 Release build) for the do..while()! This is the loop that actually makes the function calls! Each `command` (function pointer + data) is VARIABLE in length, depending on the number of parameters! So I don't used a fixed structure or std::queue, I do everything the old-school way, with direct pointers!
				{
					( *( PFNCommandHandler* ) base_addr )( base_addr + sizeof( PFNCommandHandler* ) + sizeof( uint32_t ) );		//	I know this might look like a train-wreck, but it's actually the heart and soul of this class! The inner loop! You know we always say, you should just optimize the inner-loops! The code that requires the maximum speed! Well, this is it! 6 CPU instructions in total to execute an entire queue of function calls! You don't get much faster than that! You cannot do this faster with ANY STL container! This is what low level C/C++ and Assembler knowledge gets you! Incredible speed!
					base_addr += ( *( uint32_t* ) ( base_addr + sizeof( PFNCommandHandler* ) ) );								//	Calculate address of next function ... I guess this would be the equivalent of a queue `pop`. What we are doing is accessing the `size` value directly with a pointer. After the initial function pointer address (stored at the beginning of the `base_address`), there is a 32-bit offset number to the next function call. We just add this number to base_address to jump ahead to the next function call! There is no real `popping` of the data, that would be too slow and completely unecessary! We just make the function calls and recycle the buffer!
				}
				while ( base_addr < end );																						//	do while we haven't reached the end!
				buffer->used = 0;																								//	This essentially allows the buffer to be recycled! After this, this current buffer is exchanged with the `front-facing` / active buffer. So the `front-facing` / active is essentially a reset buffer with this. `used` is just an offset, and we just basically reset it to the beginning!
			}
			else if ( this->shutdown )
				break;
			else
			{
				std::unique_lock<std::mutex> lock( mtxDequeue );
				cvDequeue.wait( lock );
				lock.unlock();
			}
		}
	}


	//
	//		init()
	//
	void init( const uint32_t size )
	{
		//
		//		Initialize Double Buffers
		//
		this->buffer[ 0 ].commands = ( char* ) ::malloc( size );
		this->buffer[ 1 ].commands = ( char* ) ::malloc( size );

		this->buffer[ 0 ].size = size;
		this->buffer[ 1 ].size = size;

		this->buffer[ 0 ].used = 0;
		this->buffer[ 1 ].used = 0;

		//
		//		Start thread
		//
		this->hThread = new std::thread( &CommandQueue::thread, this );
	}


	//
	//		acquireBuffer()
	//
	queue_buffer_t* acquireBuffer()
	{
		queue_buffer_t* result;
		while ( ( result = primary.exchange( nullptr ) ) == nullptr )
			//	::Sleep( 0 );																			//	optional ... there are 2 producers fighting for the buffer, but they acquire and release very quickly, within a few clock cycles, it's less efficient to sleep!
			;
		return result;
	}
	//
	//		releaseBuffer()
	//
	void releaseBuffer( queue_buffer_t* buffer )
	{
		queue_buffer_t* exp = nullptr;
		if ( !primary.compare_exchange_strong( exp, buffer ) )
			secondary = buffer;																			//	Because we use Double Buffers, one is in primary, so put the other in secondary! Actually, there is a very important reason why we do this, if you are clever enough you will realise it! The thread is actually waiting for us to write this in a special while loop, look carefully! This is the second `edge` case of swopping the buffers! It's brilliant!
		this->cvDequeue.notify_one();
	}


	//
	//		allocCommand()
	//
	template< typename TCB >
	char* allocCommand( queue_buffer_t* buffer, const TCB function, const uint32_t size )				//	appends a new command to the buffer, sets the function pointer and allocates space (malloc-style) for a data buffer, returns the address to the data buffer like malloc()!
	{
		const uint32_t base = buffer->used;																//	store base address of this command, it's an array index into a char* buffer
		const uint32_t reserved = sizeof( TCB* ) + sizeof( uint32_t ) + size;							//	calculate the total size of this command, including function pointer, + sizeof( UINT ) + data
		buffer->used += reserved;
		if ( buffer->used > buffer->size )																//	check if we need to resize the buffer
		{
			do buffer->size *= 2;																		//	multiply size by *= 2, keep checking to make sure we have enough space for everything!
			while ( buffer->used > buffer->size );
			buffer->commands = (char*) ::realloc( buffer->commands, buffer->size );						//	extend the buffer, we will re-use this buffer, I feel if you needed a buffer this big before, it's likely you'll need it again! So I NEVER reduce the size of the buffer! This is up to you!
		}

		char* command = &buffer->commands[ base ];														//	Get the base address of the command
		*( ( TCB* ) command ) = function;																//	Write the function pointer address
		*( ( uint32_t* ) ( command + sizeof( TCB* ) ) ) = reserved;										//	Write the total size of the command

		return command + sizeof( TCB* ) + sizeof( uint32_t );											//	return the address to the `data` section
	}


	//
	//		execute() Stub functions																	//	These function essentially `extract` the function call parameters (data) from the Command Queue buffer and call your function with them!
	//
	template< typename TCB >
	static void executeStubV0( char* data )
	{
		( *( ( TCB* ) data ) )();
	}
	template< typename TCB, typename T1 >
	static void executeStubV1( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) ) );
		function( v1 );
	}
	template< typename TCB, typename T1, typename T2 >
	static void executeStubV2( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) );
		function( v1, v2 );
	}
	template< typename TCB, typename T1, typename T2, typename T3 >
	static void executeStubV3( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) );
		function( v1, v2, v3 );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4 >
	static void executeStubV4( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) );
		const T4 v4 = *( ( T4* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) );
		function( v1, v2, v3, v4 );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5 >
	static void executeStubV5( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) );
		const T4 v4 = *( ( T4* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) );
		const T5 v5 = *( ( T5* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) );
		function( v1, v2, v3, v4, v5 );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6 >
	static void executeStubV6( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) );
		const T4 v4 = *( ( T4* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) );
		const T5 v5 = *( ( T5* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) );
		const T6 v6 = *( ( T6* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) ) );
		function( v1, v2, v3, v4, v5, v6 );
	}


	//
	//		returns() Stub functions																	//	These are the `stub` functions that actually CALL YOUR function and returns the value directly to the address you specified! These are the functions that are actually called on another thread! They are called directly by the thread inner-loop!
	//
	template< typename TCB, typename R >
	static void returnStubV0( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		**( ( R* ) ( data + sizeof( TCB* ) ) ) = function();
	}
	template< typename TCB, typename R, typename T1 >
	static void returnStubV1( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) );
		**( ( R* ) ( data + sizeof( TCB* ) ) ) = function( v1 );
	}
	template< typename TCB, typename R, typename T1, typename T2 >
	static void returnStubV2( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) );
		**( ( R* ) ( data + sizeof( TCB* ) ) ) = function( v1, v2 );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3 >
	static void returnStubV3( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) );
		**( ( R* ) ( data + sizeof( TCB* ) ) ) = function( v1, v2, v3 );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3, typename T4 >
	static void returnStubV4( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) );
		const T4 v4 = *( ( T4* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) );
		**( ( R* ) ( data + sizeof( TCB* ) ) ) = function( v1, v2, v3, v4 );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3, typename T4, typename T5 >
	static void returnStubV5( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) );
		const T4 v4 = *( ( T4* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) );
		const T5 v5 = *( ( T5* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) );
		**( ( R* ) ( data + sizeof( TCB* ) ) ) = function( v1, v2, v3, v4, v5 );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6 >
	static void returnStubV6( char* data )
	{
		const TCB function = *( ( TCB* ) data );
		const T1 v1 = *( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) );
		const T2 v2 = *( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) );
		const T3 v3 = *( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) );
		const T4 v4 = *( ( T4* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) );
		const T5 v5 = *( ( T5* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) );
		const T6 v6 = *( ( T6* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) ) );
		**( ( R* ) ( data + sizeof( TCB* ) ) ) = function( v1, v2, v3, v4, v5, v6 );
	}


public:
	//
	//		constructors
	//
	CommandQueue() { this->init( 256 ); }
	CommandQueue( const uint32_t size ) { this->init( size ); }
	~CommandQueue()																						//	Shutdown thread
	{
		this->shutdown = true;
		this->cvDequeue.notify_one();
		this->hThread->join();
		free( this->buffer[ 0 ].commands );
		free( this->buffer[ 1 ].commands );
	}


	//
	//		execute()																					//	Includes a `parameter` stub function which extracts the parameters for you from the buffer! There is an advanced access directly to the data buffer with rawExecute, it's slightly faster because your data doesn't pass through the stub function, but it's a bit harder to work with! This is more convenient!
	//
	template< typename TCB >																			//	TCB = Type(name)/Template Callback
	void execute( const TCB function )
	{
		queue_buffer_t* buffer = acquireBuffer();

		*( ( TCB* ) allocCommand( buffer, executeStubV0< TCB >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) ) ) = function;					//	`function` pointer address is written to the queue buffer, allocCommand() returns a memory address for use to write the `function` address/pointer

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1 >
	void execute( const TCB function, const T1 v1 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, executeStubV1< TCB, T1 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( T1 ) );			//	`function` pointer address AND T1 parameter is written to the queue buffer!
		*( ( TCB* ) data ) = function;																											//	Here we actually WRITE the function pointer, the line above just allocates/reserves space on the queue, like malloc() it returns a pointer to the `data` section in the queue, of `size` bytes!
		*( ( T1* ) ( data + sizeof( TCB* ) ) ) = v1;																							//	This is where we actually write the parameter to the queue buffer, We do some pointer addition, to move to the next parameter

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2 >
	void execute( const TCB function, const T1 v1, const T2 v2 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, executeStubV2< TCB, T1, T2 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) );
		*( ( TCB* ) data ) = function;
		*( ( T1* ) ( data + sizeof( TCB* ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) ) = v2;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3 >
	void execute( const TCB function, const T1 v1, const T2 v2, const T3 v3 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, executeStubV3< TCB, T1, T2, T3 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) );
		*( ( TCB* ) data ) = function;
		*( ( T1* ) ( data + sizeof( TCB* ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4 >
	void execute( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, executeStubV4< TCB, T1, T2, T3, T4 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) );
		*( ( TCB* ) data ) = function;
		*( ( T1* ) ( data + sizeof( TCB* ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;
		*( ( T4* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) ) = v4;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5 >
	void execute( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, executeStubV5< TCB, T1, T2, T3, T4, T5 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) );
		*( ( TCB* ) data ) = function;
		*( ( T1* ) ( data + sizeof( TCB* ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;
		*( ( T4* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) ) = v4;
		*( ( T5* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) ) = v5;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6 >
	void execute( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5, const T6 v6 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, executeStubV6< TCB, T1, T2, T3, T4, T5, T6 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) + sizeof( T6 ) );
		*( ( TCB* ) data ) = function;
		*( ( T1* ) ( data + sizeof( TCB* ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;
		*( ( T4* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) ) = v4;
		*( ( T5* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) ) = v5;
		*( ( T6* ) ( data + sizeof( TCB* ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) ) ) = v6;

		releaseBuffer( buffer );
	}


	//
	//		returns()																					//	We store the return address directly after the function pointer address, the `stub` functions are what actually call your function, they are the ones that are actually executed on another thread!
	//
	template< typename TCB, typename R >
	void returns( const TCB function, const R ret )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, returnStubV0< TCB, R >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( R ) );
		*( ( TCB* ) data ) = function;
		*( ( R* ) ( data + sizeof( TCB* ) ) ) = ret;													//	We store the return address on our internal data buffer, directly after the function call address

		releaseBuffer( buffer );
	}
	template< typename TCB, typename R, typename T1 >
	void returns( const TCB function, const R ret, const T1 v1 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, returnStubV1< TCB, R, T1 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) );
		*( ( TCB* ) data ) = function;
		*( ( R* ) ( data + sizeof( TCB* ) ) ) = ret;
		*( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) ) = v1;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename R, typename T1, typename T2 >
	void returns( const TCB function, const R ret, const T1 v1, const T2 v2 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, returnStubV2< TCB, R, T1, T2 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) );
		*( ( TCB* ) data ) = function;
		*( ( R* ) ( data + sizeof( TCB* ) ) ) = ret;
		*( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) ) = v2;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3 >
	void returns( const TCB function, const R ret, const T1 v1, const T2 v2, const T3 v3 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, returnStubV3< TCB, R, T1, T2, T3 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) );
		*( ( TCB* ) data ) = function;
		*( ( R* ) ( data + sizeof( TCB* ) ) ) = ret;
		*( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3, typename T4 >
	void returns( const TCB function, const R ret, const T1 v1, const T2 v2, const T3 v3, const T4 v4 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, returnStubV4< TCB, R, T1, T2, T3, T4 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) );
		*( ( TCB* ) data ) = function;
		*( ( R* ) ( data + sizeof( TCB* ) ) ) = ret;
		*( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;
		*( ( T4* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) ) = v4;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3, typename T4, typename T5 >
	void returns( const TCB function, const R ret, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, returnStubV5< TCB, R, T1, T2, T3, T4, T5 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) );
		*( ( TCB* ) data ) = function;
		*( ( R* ) ( data + sizeof( TCB* ) ) ) = ret;
		*( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;
		*( ( T4* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) ) = v4;
		*( ( T5* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) ) = v5;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename R, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6 >
	void returns( const TCB function, const R ret, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5, const T6 v6 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, returnStubV6< TCB, R, T1, T2, T3, T4, T5, T6 >, sizeof( PFNCommandHandler* ) + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) + sizeof( T6 ) );
		*( ( TCB* ) data ) = function;
		*( ( R* ) ( data + sizeof( TCB* ) ) ) = ret;
		*( ( T1* ) ( data + sizeof( TCB* ) + sizeof( R ) ) ) = v1;
		*( ( T2* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) ) ) = v3;
		*( ( T4* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) ) = v4;
		*( ( T5* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) ) = v5;
		*( ( T6* ) ( data + sizeof( TCB* ) + sizeof( R ) + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) ) ) = v6;

		releaseBuffer( buffer );
	}


	//
	//		rawExecute()																				//	These functions are slightly faster than execute(), but they don't extract the parameters from the queue. You get a `raw` pointer directly to data! This is for advanced use!
	//
	template< typename TCB >
	void rawExecute( const TCB function )
	{
		queue_buffer_t* buffer = acquireBuffer();

		allocCommand( buffer, function, 0 );

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1 >
	void rawExecute( const TCB function, const T1 v1 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		*( ( T1* ) allocCommand( buffer, function, sizeof( T1 ) ) ) = v1;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2 >
	void rawExecute( const TCB function, const T1 v1, const T2 v2 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, function, sizeof( T1 ) + sizeof( T2 ) );
		*( ( T1* ) data ) = v1;
		*( ( T2* ) ( data + sizeof( T1 ) ) ) = v2;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3 >
	void rawExecute( const TCB function, const T1 v1, const T2 v2, const T3 v3 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, function, sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) );
		*( ( T1* ) data ) = v1;
		*( ( T2* ) ( data + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( T1 ) + sizeof( T2 ) ) )= v3;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4 >
	void rawExecute( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, function, sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) );
		*( ( T1* ) data ) = v1;
		*( ( T2* ) ( data + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( T1 ) + sizeof( T2 ) ) )= v3;
		*( ( T4* ) ( data + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) )= v4;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5 >
	void rawExecute( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, function, sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) );
		*( ( T1* ) data ) = v1;
		*( ( T2* ) ( data + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( T1 ) + sizeof( T2 ) ) )= v3;
		*( ( T4* ) ( data + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) )= v4;
		*( ( T5* ) ( data + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) )= v5;

		releaseBuffer( buffer );
	}
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6 >
	void rawExecute( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5, const T6 v6 )
	{
		queue_buffer_t* buffer = acquireBuffer();

		char* data = allocCommand( buffer, function, sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) + sizeof( T6 ) );
		*( ( T1* ) data ) = v1;
		*( ( T2* ) ( data + sizeof( T1 ) ) ) = v2;
		*( ( T3* ) ( data + sizeof( T1 ) + sizeof( T2 ) ) )= v3;
		*( ( T4* ) ( data + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) ) )= v4;
		*( ( T5* ) ( data + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) ) )= v5;
		*( ( T6* ) ( data + sizeof( T1 ) + sizeof( T2 ) + sizeof( T3 ) + sizeof( T4 ) + sizeof( T5 ) ) )= v6;

		releaseBuffer( buffer );
	}


	//
	//		executeWithCopy()																			//	advanced! Copies the raw data directly to the buffer! You probably won't ever need it! It allows me to write raw data to the Command Queue buffers, for example, raw TCP/UDP data packets from the network!
	//
	template< typename TCB >
	void executeWithCopy( const TCB function, const void* data, const uint32_t size )
	{
		queue_buffer_t* buffer = acquireBuffer();

		::memcpy( allocCommand( buffer, function, size ), data, size );

		releaseBuffer( buffer );
	}


	//
	//		::SetEvent()
	//
	#if defined(_WIN32) && defined(HANDLE)
protected:
	void setEvent_cb( HANDLE** ev )																		//	Very useful for Windows development! Because you can use it for general purpose event notification! But join() is probably enough ... this could however send a notification to another thread to begin execution etc. join() locks the current thread, but maybe you want the current thread to continue and notify another thread somewhere else that a task is complete! It has come in handy before!
	{
		::SetEvent( *ev );
	}
public:
	void setEvent( HANDLE ev )
	{
		this->execute( setEvent_cb, ev );
	}
	#endif


	//
	//		join
	//
private:																								//	They are both here together for reference!
	static void join_cb( CommandQueue* commandQ, bool* done )
	{
		*done = true;																					//	This sets the `done` bool below, via dereferenced pointer, which is read by the lambda function in the cvJoin.wait() statement below!
		commandQ->cvJoin.notify_one();
	}
public:
	void join()																							//	Man, I really don't want to have to explain how this works ... just too technical! Read about condition variables and lambdas.
	{
		bool done = false;
		this->execute( join_cb, this, &done );
		std::unique_lock<std::mutex> lock( this->mtxDequeue );
		cvJoin.wait( lock, [&] { return done; } );														//	Condition variables can be signaled by the operating system and return randomly, so we need a way to `signal` them that they must return from OUR `done` message only, that's what the lambda function does!
		lock.unlock();
	}


	//
	//		operator ()		functors!																	//	NOTE: If you create an object pointer out of this (with `new`), then you need to use (*objname)(function_to_call) ... note the object/pointer dereference ... it sucks I know!
	//
	template< typename TCB >
	CommandQueue & operator ()( const TCB function ) { this->execute( function ); return *this; }
	template< typename TCB, typename T1 >
	CommandQueue & operator ()( const TCB function, const T1 v1 ) { this->execute( function, v1 ); return *this; }
	template< typename TCB, typename T1, typename T2 >
	CommandQueue & operator ()( const TCB function, const T1 v1, const T2 v2 ) { this->execute( function, v1, v2 ); return *this; }
	template< typename TCB, typename T1, typename T2, typename T3 >
	CommandQueue & operator ()( const TCB function, const T1 v1, const T2 v2, const T3 v3 ) { this->execute( function, v1, v2, v3 ); return *this; }
	template< typename TCB, typename T1, typename T2, typename T3, typename T4 >
	CommandQueue & operator ()( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4 ) { this->execute( function, v1, v2, v3, v4 ); return *this; }
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5 >
	CommandQueue & operator ()( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5 ) { this->execute( function, v1, v2, v3, v4, v5 ); return *this; }
	template< typename TCB, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6 >
	CommandQueue & operator ()( const TCB function, const T1 v1, const T2 v2, const T3 v3, const T4 v4, const T5 v5, const T6 v6 ) { this->execute( function, v1, v2, v3, v4, v5, v6 ); return *this; }


	//
	//		printBufferSizes()																			//	Just for statistical purposes, used during testing and benchmarking!
	//
	void printBufferSizes()
	{
		printf( "Double Buffer sizes: %d KB + %d KB\n", this->buffer[ 0 ].size / 1024, this->buffer[ 1 ].size / 1024 );
	}
};

#endif // __COMMAND_QUEUE_HPP__
