# **Asgn4 README.md - Design for httpserver.c - Multithreadded httpserver with Atomicity**

## **Introduction**

httpserver.c emulates behavior of a multithreaded
HTTP server, handling a subset of the HTTP 1.1 
request methods. A user, on the command line, sends
a HTTP 1.1 version-complying request to one of three
methods: GET, PUT, or APPEND. The server accepts connections,
adds them to a work queue via a dispatcher thread, and 
executes connections via pulling of jobs by worker
threads. Each thread parses the request and transmits a 
response to the client based on how the request was processed. 
This program handles ASCII and non-printable data such as binary.

## **Design**

Part 1 worked on so far: LOGGING

In the refactored version of asgn1, the three processes for
GET, PUT, and APPEND are still existent in the code, though
the manner in which subsequent messages are to be received 
has been improved such that no assumptions on when a certain
header is to be received is made. The program parses the 
request up until the end of the request, then sends all 
computation to either method's processing function, using
buffers with a fixed chunk size to iteratively send the body's
data to a filePath. Edge cases seem to have been remedied,
as 94% of the asgn1 pipeline tests have been accounted for. 

Logging has been implemented such that each request, with an
existent Request-Id, will be properly logged in the format:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; **"Oper","URI","Status-Code","RequestID header value"\n**

Part 2 & 3 worked on so far: MULTITHREADING AND ATOMICITY

The httpserver is run with multiple threads handling requests concurrently.
Jobs are enqueued onto the queue data structure using a FIFO system to 
ensure worker threads dequeue jobs from the front while the dispatch thread
enqueues from the back. 

The worker queue implemented is as follows:

#### **Dispatcher:**

```
for (;;) {
        int connfd = accept(listenfd, NULL, NULL);
        pthread_mutex_lock(&myMutex);
        if (connfd < 0) {
            warn("accept error");
            continue;
        }

        if (getQueueSize() > MAX_QUEUE_SIZE) {
            warn("Queue Full <%d>", getQueueSize());
            pthread_cond_wait(&queueFull, &myMutex);
        }
        enqueue(connfd);
        //QueueCapacityCount++;
        warn("connfd [%d] to be input...", connfd);
        pthread_cond_signal(&canWork);
        pthread_mutex_unlock(&myMutex);
    }
```

#### **Workers:**

```
for (;;) {
        pthread_mutex_lock(&myMutex);

        while ((threadConnfd = dequeue()) < 1) {
            pthread_cond_wait(&canWork, &myMutex);
        }
        //QueueCapacityCount--;
        pthread_cond_signal(&queueFull);

        printf("Thread: %p about to unlock...", arg);
        pthread_mutex_unlock(&myMutex);
        if (threadConnfd > 0) {

            handle_connection(threadConnfd);

            pthread_mutex_lock(&closeConnMutex);
            (void) threadConnfd;
            close(threadConnfd);
            warn("Thread finished work");
            pthread_mutex_unlock(&closeConnMutex);
}  
```

Proper atomicity is employed to ensure no worker thread accesses or modifies 
a resource being written to by another worker thread. A resource can be shared
if only reading is being done, otherwise it is locked from being written to.
A resource that is being written to may only be written to by the worker thread
currently modifying it. These are exclusive rights given to a thread to edit
the resource as needed, unlocking it once all modifications are completed.

#### **Locking a file:**

```
{
    ...
    flock(fd, LOCK_EX);
    ...
    reads() or writes(), or logs
    ...
    close(fd);
}
```

All producing and consuming of jobs are done via a loop in main() (dispatcher 
creates jobs when a connection is accepted) and another loop in threadFunction()
(N worker threads remove jobs from the front of the queue and go off to execute
them).

## **Testing**

###### Commands used:

#### GET:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**echo -e "GET /httpserver HTTP/1.1\r\nRequest-Id: 2\r\n\r\n" | nc -q 0 localhost 8080**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**curl http://localhost:8081/httpserver -v --output -**

#### PUT:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**echo -e "PUT /foo.txt HTTP/1.1\r\nRequest-Id: 1\r\nContent-Length: 12\r\n\r\nHello world!!" | nc -q 0 localhost 8083**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**curl -d "Hi Hi Hi" -X PUT http://localhost:8083/foo.txt**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**curl -X PUT "localhost:8081/foo11.txt" -F "file=@httpserver"**

#### APPEND:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**echo -e "APPEND /foo.txt HTTP/1.1\r\nRequest-Id: 1\r\nContent-Length: 11\r\n\r\nHello World!" | nc -q 0 localhost 8080**

#### PERFORMANCE CHECK:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**check performance: time ./httpserver -t <threads> -l <logfile> <port>**

#### VALGRIND COMMAND:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**valgrind: valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./httpserver -t 4 -l logging 8081**

#### GET_SPEEDUP:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**./get_speedup.sh 5012 README.md 64 64 1000**

#### OLIVERTWIST USING TOMLS (output results to a file):

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**./olivertwist.py -d responses -p 8081 requests.toml > resultss**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**t1.toml, t2.toml, wozniak.toml, etc**

###### Errors returned

200 OK on successful method execution

201 Created on successful file creation (PUT)

404 Not Found on instances where a file does not exist (GET, APPEND)

500 Internal Server Error on instances of failure in the server out of our scope

-1 in cases of invalid reads/writes. This was done mostly whenever bytes were needed
to be read off a connection. This would make a request fail where applicable.

program stoppage (exit(EXIT_FAILURE)) in the event where the threads encountered some 
error during creation.

errno codes for invalid reads/recvs, writes/sends, or attempted access to a 
resource that does not exist 

## **Issues encountered**

After spending quite a bit of time refactoring code from asgn1, I was able to
finally pass 94% of the functional tests (save for 24 and 26), which I
hypothesized was a request formatting issue. Knowing that my asgn1 code was
refactored into a state that could handle virtually any request of either
text-based or binary content, I decided to abandon effort in solving the
last two remaining test cases. It just wasn't worth it to continue to
problem-solve two cases that may not be accounted for in asgn2; all requests
are assume to be valid, so checking for formatting would be an issue that
could hold me back in passing the asgn2 pipeline. 

Figuring out how to create and send information to the logfile was probably
the biggest hurdle, as the macro was intially confusing. Then came the issue 
in figuring out why my calls to LOG() weren't showing up in the logfile.
This was then discovered to be a lack of calls to fflush() right after calling
LOG(). 

Getting the program to compile was also a bit of a hassle. Some bits of code
from the starter code was needed for asgn2's program arguments to be accepted.
After getting the right portions of the code transferred to my asgn2 project,
everything started to work fine and the gitlab pipeline was eventually passed.

Multithreading was pretty difficult when it came to handling race conditions,
partial requests, and knowing when to lock, unlock, and have threads wait
on a condition variable. 

The queue data structure wasn't difficult to implement, but knowing how to handle
size issues was a little tricky. We don't want the program to allocate more 
memory than we have available, so a max size for the queue is utilized so that
in the event that many requests come in at once, we can have the dispatcher thread
wait before adding more jobs to the queue.

Mutexes were also difficult to get a hang of because I needed to ensure that I
was handling mutual exclusion in all the correct places of my code.

Non-blocking sockets proved to be very challenging. To circumvent blocking (or at
least try to), careful thread scheduling was implemented to ensure most cases the
httpserver would handle could be successfully handled. all but one case in asgn3
passed, so non-blocking wasn't totally accounted for: a big vulnerability my
server possesses. 

The worker queue presented some issues with handling lots of requests, with most
of the threads doing something most of the time. I spent probably 5+ days trying
to track my bug, to which I finally discovered that the way I was dequeuing jobs
allowed for some threads to break through an if() conditional. This was remedied
(in my worker thread function) to a while() loop such that if the queue is empty,
wait for a job then test to see if the queue is empty again (just in case).

As for the dispatcher thread, The manner in which I set up a variable to keep track
of the queue size was via a global variable in the httpserver.c file. This was 
dangerous because a set of threads could decrement it more than it needed to be 
decremented, leading to waiting in cases where the worker threads believed the
queue was empty when it wasn't. This was remedied by adding the size variable
into my threadPool.c file, so that the size of the queue remained solely inside
the queue data structure. This allowed me to pass test cases 1, 3, and 9 in 
asgn3: 3 cases I had struggled to get working and unsuccessfully used all my 
grace days in search of the bugs.

To ensure the webserver adhered to the concept of atomicity, I had to pinpoint
where exactly reading and writing from files could yield partial or incorrect
results. It was a bit difficult at first, but once I read up on flock(), I
could handle a few more cases in which the httpserver needed to maintiain
a consistent sequence of events in modifying or reading from files. 

the placement of mutex locks and unlocks were pivotal in ensuring the correct
sequence of requests were made. having a lock too early or too late would yield
incorrect or partial writes to a file, cascading further whenever a thread was
sent to fulfill a GET request.

There is still work to be done for the asgn4 pipeline, yet currently most of the
test cases are handled by my httpserver.

## **References**

Thread Pool Implementation (Jacob Sorber): https://www.youtube.com/watch?v=FMNnusHqjpw

Condition variables (Jacob Sorber): https://www.youtube.com/watch?v=P6Z5K8zmEmc

Piazza discussion board questions

Check if a File Exists in C: https://www.delftstack.com/howto/c/c-check-if-file-exists/#:~:text=file's%20full%20path.-,stat()%20Function%20to%20Check%20if%20a%20File%20Exists%20in,the%20file%20does%20not%20exist.

multithreaded web server concepts: https://users.cs.duke.edu/~chase/cps196/slides/serversx6.pdf

toml intro: https://npf.io/2014/08/intro-to-toml/

errno manpage: https://man7.org/linux/man-pages/man3/errno.3.html

open() manpage: https://man7.org/linux/man-pages/man2/open.2.html

read() manpage: https://man7.org/linux/man-pages/man2/read.2.html

write() manpage: https://man7.org/linux/man-pages/man2/write.2.html

recv() manpage: https://man7.org/linux/man-pages/man2/recv.2.html

send() manpage: https://man7.org/linux/man-pages/man2/send.2.html

flock() manpage: https://linux.die.net/man/2/flock

pthread manpage: https://man7.org/linux/man-pages/man7/pthreads.7.html

signal handling: https://man7.org/linux/man-pages/man2/signal.2.html

manpages for HTTP 1.1 protocols, char pointer manipulation

## Efficiency

The efficiency of processing GET, PUT, and APPEND requests have been
sped up greatly. The decision to refactor the message body processing
into multiple recv() calls was one made later in the design stage
instead of earlier, so had I decided to take that efficiency route I
would have definitely gotten 100%+ on asgn1.

Multithreading could be an issue with flag variables within my
handle_connection() function, so eventually that code will be
refactored and migrated outside as its own functions, so different
processes/threads do not conflict with the flag variables.

Based on preliminary testing, we observed up to a 2x speed up in most cases,
although more work is needed to ensure a faster process time. 

