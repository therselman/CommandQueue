# CommandQueue
A C++11 header-only object to execute functions on a separate thread. Featuring a unique custom built low level, lock-free double buffered queue. Executes the queue with a specially designed protocol, dedicated to high speed function calls in just 6 CPU instructions; lea,call,mov,add,cmp,jb


    #include "CommandQueue.hpp"

    CommandQueue commandQ;                 // Creates a single thread, ready and waiting for commands
    
    void cmdPrintf( const char* str )
    {
        printf( str );
    }

    int main()
    {
        commandQ( cmdPrintf, "Hello " );                                            //	Method 1 - functor
        commandQ.execute( cmdPrintf, "World\n" );                                   //	Method 2 - function
        commandQ( cmdPrintf, "Chained" )( cmdPrintf, " - link 1" )( cmdPrintf, " - link 2\n" ); //  Method 3
        commandQ.join(); // The thread doesn't actually terminate here, you can issue more commands!
        return 0;
    }
