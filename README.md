# Ruth

'Ruth' is a distributed key value database with end-to-end encryption. 

This project uses OpenSSL for cryptographic functions.
OpenSSL is licensed under the Apache License 2.0.

01/04/26 - 23:48 This is the beginning of the project. In case I actually stick with this and follow it through, I thought it would be nice to journal my thoughts and document the process. Currently, the name of the project has not been determined. The premise is to have a key value database, written in C with end-to-end encryption. When finished, the project can hopefully be exported as an API that I can use in my other projects. 

02/04/26 - 17:06 Phase 2 has been mostly completed, although I don't really understand the codebase that well. Before more code is written, I want to go through and properly understand why things work the way that they do. Currently, the encryption is all server side, and the hash map is still 3rd party. All of this will be changed later. Most of the codebase right now is temporary. The server in one terminal can be hosted with: ./build/ruth-server, and the client runs in a different terminal with: ./build/ruth-client. 