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
#include <string>
#include <cstring>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include "http-request.h"
#include "http-response.h"

using namespace std;

const int LISTEN_PORT = 14805;
const int MAX_CONNECTIONS = 10;
const int BUFFER_SIZE = 512;

pthread_mutex_t num_connections_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t num_connections_cond = PTHREAD_COND_INITIALIZER;

int num_connections = 0;

//TODO: for Sherri
//uses recv() on the given port to obtain
//the text of the request, returns a string
//containing the request. Should return when
//the request is over (i.e. two '\r\n's in a row)
//SHERRI testing git
//Sherri testing git
string getRequest(int port)
{
  char buffer[BUFFER_SIZE]; // buffer
  string request = ""; // request string
  ssize_t recv_len; // recv return value
  size_t end;
  // get the request from port
  while(0 < (recv_len = recv(port, buffer, BUFFER_SIZE, 0)))
  {
    request.append(buffer, recv_len);
    //look for the end
    if(std::string::npos != (end = request.find("\r\n\r\\n")))
      break;
  }
  if(0 == recv_len)
    cout << "Port: " << port << " shut down (getRequest)" << endl;
  if(0 > recv_len)
    perror("Error occurred in getRequest");
    
  request = request.substr(0, (end + 4));
  
  return request;
}
    
  

//TODO: for Jonathan
//Given an HttpRequest object, contacts the
//server and returns the response in a string.
//This function should use req.FormatRequest() to
//convert the request into "relative URL + Host 
//header" format (see the spec) before sending
//it to the server.
string getResponse(HttpRequest* req){

  // format the request
  size_t reqLen = req->GetTotalLength();
  char* req_buffer = new char[reqLen];
  req->FormatRequest(req_buffer);

  // get the server ip
  //const string hostname = req->GetHost(); // get the host name
  struct hostent* host = gethostbyname((req->GetHost()).c_str()); // get host struct

  // create the socket
  int server_socket = socket(AF_INET, SOCK_STREAM, 0);

  // create the address
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr = *((struct in_addr*)host->h_addr);
  server_address.sin_port = req->GetPort();
  socklen_t length = sizeof(server_address);

  // try connecting
  if(connect(server_socket,(struct sockaddr*)&server_address, length)){
    cout << "Unable to connect to the server!" << endl;
    delete [] req_buffer;
    return NULL;
    //exit(EXIT_FAILURE);
  }

  // try sending
  if(send(server_socket, req_buffer, reqLen, 0) < 0){
    cout << "Failed to send request to the server!" << endl;
    delete [] req_buffer;
    return NULL;
    //exit(EXIT_FAILURE);
  }

  delete [] req_buffer;

  char* res_buffer = new char[2000]; // generous default response size 2KB
  // right now blocking code
  int response_size = recv(server_socket, res_buffer, 2000, 0);
  
  string response(res_buffer, response_size); // create the return string

  // free stuff
  delete [] res_buffer;
  close(server_socket);

  return response;

}



//TODO: for Jeremy
class HttpProxyCache 
{
public:
  HttpProxyCache();

  //if the req exists in the cache and is not expired
  //then return the data string, else return "0"
  //then return a pointer to the data, else return NULL
  string Query(HttpRequest* req);

  //try to cache the response. Will not cache it
  //if the object is not cacheable, (i.e. its private).
  //This function should be thread safe.
  bool AttemptAdd(HttpResponse* resp);

private:
  //some hash table here
  //possibly a heap to manage expiration times
};

HttpProxyCache http_cache;

void* servePeer(void* arg_sock)
{
  int peer_sock = *((int*) arg_sock);
     
  char buffer[BUFFER_SIZE]; // buffer
  string request = ""; // request string
  string req_to_be_parsed; // string ready to be parsed by HttpRequest
  ssize_t recv_len; // recv return value
  size_t end;
  /* Loop forever:
     recv() from peer, put result onto the end of buffer b. */
  while(1)
  {
    recv_len = recv(peer_sock, buffer, BUFFER_SIZE, 0);
    /*if recv() returns 0: (i.e. the connection has been closed)
       subtract 1 from num_connections (it needs to do this thread safe)
       thread should exit*/
    if(0 == recv_len)
    {
      close(peer_sock);
      pthread_mutex_lock(&num_connections_mutex);
      num_connections--;
      pthread_cond_signal(&num_connections_cond);
      pthread_mutex_unlock(&num_connections_mutex);
      pthread_exit(NULL);
    }
    else if(0 > recv_len)
      perror("Error occurred in recv");
    else
      request.append(buffer, recv_len);

    /*if newly recv()d stuff in b contains '\r\n\r\n' (two carriage returns in a row) then:
      split b into two parts (before the '\r\n\r\n' and after the '\r\n\r\n'*/
    if(std::string::npos != (end = request.find("\r\n\r\\n")))
    {
      req_to_be_parsed = request.substr(0, (end + 4));
      // the second part becomes the new buffer b
      request = request.substr((end + 4));
      // the first part is parsed by HttpRequest,
      HttpRequest req;
      req.ParseRequest(req_to_be_parsed.c_str(), req_to_be_parsed.length() + 1);
      // check the cache for the parsed url
      string cached_data = http_cache.Query(&req);
      // if it's in the cache
      if(cached_data != "0")
      {
        // then send() the data to the user
        send(peer_sock, &cached_data, sizeof(cached_data), 0);
      } 
      else
      {
        // the HttpRequest is given to the function getResponse()
        string response = getResponse(&req);
        // the result of the getResponse() is parsed by a HttpResponse()
        HttpResponse resp;
        resp.ParseResponse(response.c_str(), response.length() + 1);
        // the HttpResponse is given to the cache to be added
        http_cache.AttemptAdd(&resp);
        // the HttpResponse is formatted and sent to the user using send()
        size_t response_len = resp.GetTotalLength();
        char formatted_response[response_len];
        resp.FormatResponse(formatted_response);
        send(peer_sock, formatted_response, response_len, 0);
      }
    }
  }
}


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


  pthread_t threads[MAX_CONNECTIONS];

  //begin accepting connections
  while(true)
  {

      // check if we have too many connections
      if(num_connections >= MAX_CONNECTIONS)
      {
        pthread_mutex_lock(&num_connections_mutex);
        // block until a thread signals (when it exits)
        pthread_cond_wait(&num_connections_cond, &num_connections_mutex);
        pthread_mutex_unlock(&num_connections_mutex);
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
      int i;
      for(i = 0; i < MAX_CONNECTIONS; i++)
      {
        if(ESRCH == pthread_kill(threads[i], 0))
        {
          pthread_create(&threads[i], NULL, &servePeer, &peer_sock);
          break;
        }
      }
      pthread_mutex_lock(&num_connections_mutex);
      num_connections++;
      pthread_mutex_unlock(&num_connections_mutex);
  }

  // join all threads
  int i;
  for(i = 0; i < MAX_CONNECTIONS; i++)
    pthread_join(threads[i], NULL);

  return 0;
}
