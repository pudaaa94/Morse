Raspberry Pi was used as Embedded system platform. Main purpose of this project was to implement:

- multithreaded user space console application (main thread. UI thread and worker thread), which will handle user's input, change working regime based on user's input and catch device driver's output (this required knowledge and usage of mutexes and semaphores)
- character device driver in kernel space, which will perform encoding. Main focus here was proper implementation of init, exit, read, write and ioctl functions. Also, additional task here was to implement power LED blinking in syncrhonism with Morse-encoded word (which required knowledge of Raspberry Pi's address space and concept of address virtualization)
- automatization of build and execution process with shell scripts