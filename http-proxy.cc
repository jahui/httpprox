/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>

using namespace std;

const int LISTEN_PORT = 14805;
const int MAX_CONNECTIONS = 10;

//TODO: for Sherri
//uses recv() on the given port to obtain
//the text of the request, returns a string
//containing the request. Should return when
//the request is over (i.e. two '\r\n's in a row)
string getRequest(int port);

//TODO: for Jonathan
//Given an HttpRequest object, contacts the
//server and returns the response in a string.
//This function should use req.FormatRequest() to
//convert the request into "relative URL + Host 
//header" format (see the spec) before sending
//it to the server.
string getResponse(HttpRequest* req){

  // format the request
  size_t reqLen = req.GetTotalLength();
  char* request = new char[reqLen];
  req.FormatRequest(request);

  // get the server ip
  string* hostname = req.GetHost(); // get the host name
  struct hostent* host = gethostbyname(hostname->c_str()); // get host struct

  // create the socket
  int socket = socket(AF_INET, SOCK_STREAM, 0);

  // create the address
  struct sockaddr_in server_address;
  server_address.sa_family = AF_INET;
  server_address.sin_addr = *((struct in_addr*)host->h_addr);
  server_address.sin_port = req.GetPort();
  socklen_t length = sizeof(server_address);

  // try connecting
  if(connect(socket,(struct sockaddr*)&server_address, length)){
    cout << "Unable to connect to the server!" << endl;
    //exit(EXIT_FAILURE);
  }

  

}



//TODO: for Jeremy
class HttpProxyCache 
{
public:
  HttpProxyCache();

  //if the req exists in the cache and is not expired
  //then return a pointer to the data, else return NULL
  char* Query(HttpRequest* req);

  //try to cache the response. Will not cache it
  //if the object is not cacheable, (i.e. its private).
  //This function should be thread safe.
  bool AttemptAdd(HttpResponse* resp);

private:
  //some hash table here
  //possibly a heap to manage expiration times
};



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


      //TODO: For Sherri
      //Look into using Pthreads or Boost threads instead of fork(), since
      //fork() doesn't allow the child processes to share the cache.

      //fork a child to serve the peer
      if(fork() == 0)
        {
          //this is the child process

          //child can stop listening for new connections
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
