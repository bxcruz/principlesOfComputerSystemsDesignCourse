# **Asgn2 README.md - Design for httpserver.c**

## **Introduction**

httpserver.c emulates behavior of a single threaded
HTTP server, handling a subset of the HTTP 1.1 
request methods. A user, on the command line, sends
a HTTP 1.1 version-complying request to one of three
methods: GET, PUT, or APPEND. The server parses the 
request and transmits a response to the client based on
how the request was processed. This program handles
ASCII and non-printable data such as binary.

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

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; "Oper","URI","Status-Code","RequestID header value"\n


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

###### Errors returned

200 OK on successful method execution

201 Created on successful file creation (PUT)

404 Not Found on instances where a file does not exist (GET, APPEND)

500 Internal Server Error on instances of failure in the server out of our scope

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

## **References**

Piazza discussion board questions

Check if a File Exists in C: https://www.delftstack.com/howto/c/c-check-if-file-exists/#:~:text=file's%20full%20path.-,stat()%20Function%20to%20Check%20if%20a%20File%20Exists%20in,the%20file%20does%20not%20exist.

errno manpage: https://man7.org/linux/man-pages/man3/errno.3.html

open() manpage: https://man7.org/linux/man-pages/man2/open.2.html

read() manpage: https://man7.org/linux/man-pages/man2/read.2.html

write() manpage: https://man7.org/linux/man-pages/man2/write.2.html

manpages for recv(), send(), HTTP 1.1 protocols, char pointer manipulation

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