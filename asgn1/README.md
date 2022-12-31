# **Asgn1 README.md - Design for httpserver.c**

## **Introduction**

httpserver.c emulates behavior of a single threaded
HTTP server, handling a subset of the HTTP 1.1 
request methods. A user, on the command line, sends
a HTTP 1.1 version-complying request to one of three
methods: GET, PUT, or APPEND. The server parses the 
request, throws out any requests that are not formatted
correctly, and transmits a response to the client based on
how the request was processed. This program handles
ASCII and non-printable data such as binary.

## **Design**

Three big modules were designed with the program in mind:
processGet(), processPut(), and processAppend(). These each
are executed based on the type of request given. GET requests
are fairly simple, so many short requests made to the server 
are most likely GET requests, as there is nothing after the 
"\r\n\r\n" string at the end of each request. 

When it came to processing PUT and APPEND, those were more 
difficult, as the message body posed the greatest difficulty in
assessing now long it may be in the request. This design tried
the best it could to adapt to different length requests in order
to effectively capture a request with a body that was sent within
the 2048 request length message. If any other information was 
transmitted after the inital read, an additional call to read/recv
was made to catch the rest of the data. 

One big flaw that seems to still affect the program is the presence
of binary data sent to the server. it's still a bit difficult to
fully process binary messages in full, so as of this writing of
the document, not all cases for binaries have been accounted for.

Most of the hard work is done within the validateRequest() function,
as the design parses each important piece of information needed to
fully process, or reject, a request. pattern matching (for the most
part) does a good job with this design, with all attempted poorly
formed requests are filtered out and rejected in this server.
Edge cases are a bit difficult to manage because of their tendency
to be difficult to diagnose, but a lot of error checking has gone
into this design to ensure the program works as closely to a single-
threaded server as possible. ASCII-text checking for key:value 
pairs was also implemented to ensure each key and value are properly
typed in the request. and non-ASCII data should be thrown out along
with the entire request. 

Responses by the server to the client have for the most part been
covered, with maybe a few edge cases not accounted for. Each response
in the event of good requests or bad requests is always given a 
proper response, with the connection being closed in the event of no
data being transmitted

## **Testing**

Lots of permutations of requests were made and attempted on the server
and compared to the working binary. Despite some differences between
the working binary and the desired output, most of this server's
outputs do match its counterpart. Mismatched content-lengths to message
bodies were put into requests to study how the program decides how many
bytes to send back to the client, with mostly favorable results. 

Sending files as PUT arguments to the server also yielded good results
as well, although binary data had mixed success. 

Testing for non-ASCII characters was also dutifully employed to ensure
the requests matched the specifications of the assignment document.

Many features outside of the scope of the assignment have not been worked
on, however. If given more time this effort could have been done.

###### Commands used:

#### GET:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**echo -e "GET /httpserver HTTP/1.1\r\nHost: localhost:8080\r\nUser-Agent:curl/7.68.0\r\nAccept: */*\r\n\r\n" | nc -q 0 localhost 8080**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**curl http://localhost:8081/httpserver -v --output -**

#### PUT:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**echo -e "PUT /foo.txt HTTP/1.1\r\nContent-Length: 12\r\n\r\nHello world!" | nc -q 0 localhost 8080**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**echo -e "PUT /foo.txt HTTP/1.1\r\nContent-Length: 12\r\n\r\nHello world!!" | nc -q 0 localhost 8083**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**curl -d "Hi Hi Hi" -X PUT http://localhost:8083/foo.txt**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**curl -X PUT "localhost:8081/foo11.txt" -F "file=@httpserver"**


#### APPEND:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**echo -e "APPEND /foo.txt HTTP/1.1\r\nContent-Length: 11\r\n\r\nHello World!" | nc -q 0 localhost 8080**

###### Errors returned

200 OK on successful method execution

201 Created on successful file creation (PUT)

400 Bad Request on a poorly-formatted request

403 Forbidden on instances in which we don't have file access

404 Not Found on instances where a file does not exist (GET, APPEND)

500 Internal Server Error on instances of failure in the server out of our scope

501 Not Implemented on instances where a method that isn't supported is attempted

errno codes for invalid reads/recvs, writes/sends, or attempted access to a resource
that does not exist or don't have access to

## **Issues encountered**

There were lot's of instances where output of my httpserver matched that of the binary,
yet the pipeline would register it as a fail. There was also extensive failures in the
gitlab pipeline, making this assignment even more difficult to bug, as unit tests looked
like they passed and matched the binary output, yet did not end up passing test cases
via the script.

String parsing was also especially difficult in the early stages of the assignment.
It was difficult at first to formulate an effective strategy to grab a message body
that did not agree with the content length value. I tried really hard to get it as
close as possible, though more work is to be done.

curl was also difficult to work with, as sometimes the connection would not close.
This affected the flow of the program as sometimes input was not processed correctly.
I ended up switching to mostly echo and netcat commands to better diagnose the program,
but there still exist bugs that will take time to fix. 

## **References**

Dr. Quinn's Piazza discussion over tests (private)

Eugene's office hours regex matching literals

(Youtube) Jacob Sorber - Program your own web server in C. (sockets): https://www.youtube.com/watch?v=esXw4bdaZkc&t=626s

Check if a File Exists in C: https://www.delftstack.com/howto/c/c-check-if-file-exists/#:~:text=file's%20full%20path.-,stat()%20Function%20to%20Check%20if%20a%20File%20Exists%20in,the%20file%20does%20not%20exist.

errno manpage: https://man7.org/linux/man-pages/man3/errno.3.html

open() manpage: https://man7.org/linux/man-pages/man2/open.2.html

read() manpage: https://man7.org/linux/man-pages/man2/read.2.html

write() manpage: https://man7.org/linux/man-pages/man2/write.2.html

manpages for recv(), send(), HTTP 1.1 protocols, char pointer manipulation

## Efficiency

The httpserver is somewhat efficient, with two real program computation
forks for different types of requests. If the request includes content
to be PUT or APPENDed that is a file, we can successfully read that 
information with a test call to read(). Otherwise, if no other pertinent
information is received from a read()/recv(), the server will assume 
that the request has been fully sent and will operate the method on 
the partially filled buffer containing a request that is < 2048 bytes 
long. This is for PUT and APPEND methods that have a short request,
typically on information that is readily available. 

This server may experience difficulties when it comes time to implement
multithreading, but works reasonably quickly for all general requests.

In the early stages of the program, there were instances of it hanging
on bad data within requests, yet by refactoring and redesigning, a non-
hanging program has for the most part been successfully completed.