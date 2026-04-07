# Ruth

'Ruth' is a distributed key value database with end-to-end encryption. 


This project uses OpenSSL for cryptographic functions.
OpenSSL is licensed under the Apache License 2.0.


Entry log 1: 01/04/26 - 23:48 This is the beginning of the project. In case I actually stick with this and follow it through, I thought it would be nice to journal my thoughts and document the process. Currently, the name of the project has not been determined. The premise is to have a key value database, written in C with end-to-end encryption. When finished, the project can hopefully be exported as an API that I can use in my other projects. 


Entry log 2: 02/04/26 - 17:06 Phase 2 has been mostly completed, although I don't really understand the codebase that well. Before more code is written, I want to go through and properly understand why things work the way that they do. Currently, the encryption is all server side, and the hash map is still 3rd party. All of this will be changed later. Most of the codebase right now is temporary. 
        The server in one terminal can be hosted with:

        ./build/ruth-server
 
        and the client runs in a different terminal with: 
 
        ./build/ruth-client


 Entry log 3: 02/04/26 - 22:20 The project now has end-to-end distributed with multiple nodes. Currently needs debugging for the distributed system, but the encryption seems to work fine. I do want to have a feature that elects a leader that can be functional solo. I would like to research distributed systems more to find out how they should be implemented properly. This could be one of the best features when fully realised. To run the nodes:

        ./build/ruth-server 1 config/cluster.conf

        ./build/ruth-server 2 config/cluster.conf

        ./build/ruth-server 3 config/cluster.conf

        ./build/ruth-client 127.0.0.1 6379

        **Start order matters** — start all 3 server nodes before the client. The nodes need to find each other and elect a leader first, which takes about 300ms after all nodes are up.



Entry log 4: 06/04/26 - 20:54 I haven't changed any code since the last update, rather I have spent that time researching into industry standards for similar pieces of software. First of all, mostly use cloud computing; this could become an element I want to introduce for Ruth later, but is not currently a necessity as apart of the project. Secondly, a lot of similar NoSql systems are commonplace in major companies. Cassandra DB (not an inspiration for the name) is a very large wide column store NoSql DB. For key-value stores, the biggest I could find was 'Valkey', which will be infinitely better and more practical than anything I can make. It's open source and has had years for teams to optimise the C. The point of Ruth was to build these systems from the ground up. Also, Ruth has a bigger emphasis on security than Valkey. Before I review the code, I want to further define what Ruth actually is, which is a smaller, secure cousin of Redis. Ruth is different from all of these for offering end-to-end encryption. This would destroy the performance of something like Redis, as well as killing some of the functionality. E2E is only viable when the server does not need to interpret the data that is being transferred. 

(for safe keeping cd "/mnt/c/Users/harry/Documents/Ruth")


Entry log 5: 07/04/26 - 16:28 Since last log, I have added a lot more comments and improved personal readability for the code. As a result, the codebase makes a lot more sense, and I have a better sense of where the project is going. Firstly, I want the distributed features that are current to work. There are a couple of bugs to do with the election cycle, as well as the db.log file not existing in the data folder. The number of clients / servers is hard coded, and I want a way to make that more malleable. One idea for this, is a 'master client' (password protected) that is able to add and remove server nodes manually, either through cli or gui. To abstract, lets say cli for now (if I decide this is the optimal solution). The Khash function also needs to be replaced with my own custom hash function. I can see this being one of the most difficult aspects of the project, so I'm leaving it for later. 

Entry log 6: 07/04/26 - 17:07 The issue now is clear. After some debugging, it seems that all 3 server nodes (which are required) are stuck in an infinite loop of election cycles, where nobody wins. This means no server can handle a client's request. This is known as the split vote problem. 