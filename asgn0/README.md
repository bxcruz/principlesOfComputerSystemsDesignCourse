
# **Asgn0 README.md - Design for split.c**

## **Introduction**

split.c emulates behavior of the Unix utility split. The command takes one 
or more input files (text or binary) and a character delimiter, "splitting" 
the file by replacing any occurrence of the delimiter in the file(s) with 
newlines ('\n'). 

## **Design**

One of the main goals in implementing split.c was to ensure modularization was 
properly utilized to maximize debugging potential. Optimization was also achieved
with modularity, as key processes were readily available through single function 
calls that reduced the overall code size. Key functions such as:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**checkForASCII(), processStdin(), processFile()**

allowed for immense reusability within the codebase and allowed for curbing of 
propagation of effects depending on the input provided. ASCII-style text files and
binary files produced two different flows the program could follow, making it much 
easier to track a bug compared to utilizing the same function to handle both types
of input. Checking for ASCII characters also provided security in knowing the code
was robust, taking in any sort of input while being strict on the output. 

Error checking was also a large design decision due to the many pitfalls the program
could fall into when making system calls to **open()**, **read()**, **write()**,
and processing command argument parameters. the decision to utilize **fprintf()**
in lieu of **warn()** came from comfort with the function and current misunderstanding
of how **warn()** works. A design decision for future assignments will most probably
include it for the sake of preventing compiler optimizations leading to CI testing
failures. **errno** was utiilized instead, proving incredibly useful for identifying
error codes returned by the program at the beginning, during execution, and
at the very end of the program. 

In order to avoid memory problems, a simple character buffer with a size of 1024 
bytes was declared in each process requiring reading in bytes from files. Although
it was simpler to use in this assignment, future assignments may require a change of
memory model in order to account for variable sizing.


## **Testing**

###### Commands used:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**./split < delimiter > < file1 > < file2 > ... < file N > > a.txt**

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;**../../resources/asgn0/./split < delimiter > < file1 > < file2 > ... < file N > > b.txt**

Testing was done utilizing the redirection operator **">"** on the command line,
calling **./split** from both the resources directory and my directory and outputting
the modified files into files named **a.txt** and **b.txt**, respectively. Then,
I utilized the **diff** command to compare both files, either outputting nothing if
the files were identical, or a message reporting both files differ. 

A very primitive bash script was also used, but scrapped due to a preference toward
using **diff** and **">"**. 

###### Errors returned 

errno 2: File wasn't found: No such file or directory

errno 21: Attempted to access a directory and not a file

errno 22: Can't handle multi-character splits of given multichar delimiter

errno 28: Out of memory. Tried to split a very large set of bytes 

errno also returns error values for invalid opens, closes, reads, and writes, but I left
the program to handle those errors, so I don't have the error numbers off the top
of my head. They do successfully return error messages since the test cases did in fact
pass.


## **Issues encountered**

At first, no major issues were found while writing up split.c. The decision to tackle
processing a call to **stdin** first made it much easier to transition to processing 
actual files. However, once both functions for stdin and text/binary files were written,
issues came up whenever handling non-ASCII input/files. This was due to the manner in
which after a chunk of bytes from a file was read, the for loop for iterating through
the buffer used **strlen(buffer)** instead of the variable that had the read() byte
number assigned, **bytesRead**.

###### Portion of code in stdin/file process functions using wrong length parameter

```
 for (int i = 0; i < (int) strlen(buffer); i++) {
            if (stdinBuffer[i] == delim) {
                stdinBuffer[i] = '\n';
            }
        }

```

 ###### Rectified portion of code, replaced **strlen(buffer)** with **bytesRead**

 ```
for (int i = 0; i < bytesRead; i++) {
            if (stdinBuffer[i] == delim) {
                stdinBuffer[i] = '\n';
            }
        }
 ```

This fix addressed the issue in which binary files weren't being split properly,
even though regular text files were. Since binary files aren't the same as ASCII
text-based files, the for loop would immediately break and write the file to stdout
unchanged. This problem was originally thought to have been caused by the character 
comparisons not quite working because I hadn't converted the character to its
binary representation, yet the problem was still there even *after* creating a
function that converted the delimiter to binary. 

## **References**

Dr. Quinn's office hour code examples

My own codebase from fall 2021 CSE 130 

split utility manpage: https://man7.org/linux/man-pages/man1/split.1.html

errno manpage: https://man7.org/linux/man-pages/man3/errno.3.html

open() manpage: https://man7.org/linux/man-pages/man2/open.2.html

read() manpage: https://man7.org/linux/man-pages/man2/read.2.html

write() manpage: https://man7.org/linux/man-pages/man2/write.2.html

reference pages for isprint(), iscntrl(), isblank(), isspace() to handle ASCII
checking

## Efficiency

This implementation of split is reasonably efficient to very efficient due to the
simplicity of the codebase. There are three major functions that do all the the 
work beyond **main()**, which (at a high level) checks for a valid ASCII-type 
character, and either processes an stdin parameter or file paramter as input. 
The code can take in any sort of text-based file and reasonably process it and 
split it within an acceptable amount of time. In the initial debugging stage, 
two comparisons were made to check for a splittable delimiter, which was then
rectified because it compared the same character twice, failing a few test cases.
This was due to the assumption that my binary processing portion of the code wasn't
detecting a valid delimiter because it wasn't converted into binary, yet the real
root of the issue was due to the fact that the iteration through the buffer storing 
chunks of bytes of the file for every read call was given an incorrect length
parameter. This ended the for loop prematurely and caused some bugs to where binary
files would spit out an unsplit output. 

The choice to keep the buffer size at 1064 was to have a nice clean power of 2 as
its magnitude. Any smaller buffer size would increase the iteration portion of
searching though the buffer for any splittable delimiter characters by a factor of 2.
Any larger could potentially work but that could again increase the search through the 
buffer by a factor of 2. 