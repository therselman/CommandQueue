# CommandQueue
A C++11 header-only object to execute functions on a separate thread. Featuring a unique custom built low level, lock-free double buffered queue. Executes the queue with a specially designed protocol, dedicated to high speed function calls in just 6 CPU instructions; lea,call,mov,add,cmp,jb
