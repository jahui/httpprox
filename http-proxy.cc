/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>

using namespace std;

const int LISTEN_PORT = 14805;
const int MAX_CONNECTIONS = 10;

int main (int argc, char *argv[])
{

  //create a new socket
  int listen_sock = socket(AF_INET, SOCK_STREAM, 0);

  //create an ipv4 address object
  struct sockaddr_in listen_addr;
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_addr.s_addr = INADDR_ANY;
  listen_addr.sin_port = htons(LISTEN_PORT);

  //bind the port to the address
  if(bind(listen_sock, (struct sockaddr*)  &listen_addr, sizeof(listen_addr)))
    {
      //error binding
      cout << "Failure to bind port" << endl;
      exit(EXIT_FAILURE);
    }

  //start listening
  if(listen(listen_sock, MAX_CONNECTIONS))
    {
      //error listening
      cout << "Failure to listen" << endl;
      exit(EXIT_FAILURE);
    }


  int num_connections = 0;

  //begin accepting connections
  while(true)
    {

      // check if we have too many connections
      if(num_connections >= MAX_CONNECTIONS)
        {
          int status;

          // loop until a child finishes
          while(wait(&status) <= 0)
            ;

          num_connections--;
        }

      //accept the connection from the peer
      struct sockaddr_in peer_addr;
      socklen_t addr_len = sizeof(peer_addr);
      int peer_sock = accept(listen_sock, 
                             (struct sockaddr*) &peer_addr, 
                             &addr_len);

      if(peer_sock <= 0)
        {
          //error accepting
          cout << "Failure to accept" << endl;
          exit(EXIT_FAILURE);
        }

      //fork a child to serve the peer
      if(fork() == 0)
        {
          //this is the child process

          //child can stop listening
          close(listen_sock);


          // TODO:
          // 1. Parse http request from peer
          // 2. Check cache for the resource
          // 3. If resource is not in cache or it expired, send query to correct server.
          // 4. Parse response from server
          // 5. If it should be cached, then cache it
          // 6. Send response to peer


          close(peer_sock);
          exit(EXIT_SUCCESS);
        }
      else
        {
          //this is the parent process

          num_connections++;

          //parent doesn't need the peer connection
          close(peer_sock);
        }
    }


  return 0;
}
