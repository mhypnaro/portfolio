# Portfolio
A collection of coursework which demonstrates my knowledge of network programming, C++, and linux internals.

### Coding Projects
Each project is split into three directories. A bin directory for storing binaries, a doc directory which contains a README and design document, and a src directory which contains code and a make file.

**Concurrent File Downloader:** The purpose of this program is to create a client which can spawn multiple threads to  speed up the rate at which it gathers files from a set of servers. All of the servers have the same  files, so when the client wants to download a file, it divides the work equally among all of its  threads (one for each server). The threads then download a chunk of the specified file and if any  one of them fails, another threads takes over provided that there is one available.  

**Simple HTTPS Client:** This program is designed to serve as a basic HTTP or HTTPS client capable of making GET or HEAD requests to a functional web server.

**Simple Remote Shell:** This work is split into two programs: a client and a server. The client program is used to  connect to a specified server created by the server program. The client then acts as a limited  remote shell, allowing the user to run some commands, such as “pwd”, “ls”, etc. The server is  concurrent, meaning that one server can run commands for multiple clients simultaneously.