/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <vector>
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
#include <time.h>
#include <boost/unordered_map.hpp>
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
    if(std::string::npos != (end = request.find("\r\n\r\n")))
      break;
  }
  if(0 == recv_len)
    cout << "Port: " << port << " shut down (getRequest)" << endl;
  if(0 > recv_len)
    perror("Error occurred in getRequest");
    
  request = request.substr(0, (end + 4));
  
  return request;
}


struct PeerRequest {

  PeerRequest();
  ~PeerRequest();

  //the request from the peer
  HttpRequest req;

  //the response to the peer
  HttpResponse resp;

  bool finished; //true if resp is ready to be sent to peer
  
  //the socket for the server
  int server_socket;
  
  //this buffer holds the server response in its text form
  string server_buffer;
  int buffer_capacity;
  int buffer_num_chars;

  bool stale; //true if the cache returned a stale resp
};

PeerRequest::PeerRequest() {
  finished = false;
  server_socket = -1;
  buffer_capacity = 100;
  buffer_num_chars = 0;
  server_buffer = "";
  stale = false;
}



//TODO: for Jonathan
//Given an HttpRequest object, contacts the
//server and returns the response in a string.
//This function should use req.FormatRequest() to
//convert the request into "relative URL + Host 
//header" format (see the spec) before sending
//it to the server.
void getResponse(PeerRequest* node){

  // format the request
  size_t reqLen = node->req.GetTotalLength();
  char* buffer = new char[reqLen];
  node->req.FormatRequest(buffer);

  // get the server ip
  struct hostent* host = gethostbyname((node->req.GetHost()).c_str()); // get host struct

  // create the socket
  if(node->server_socket < 0){
    node->server_socket = socket(AF_INET, SOCK_STREAM, 0);
  }
  // create the address
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr = *((struct in_addr*)host->h_addr);
  server_address.sin_port = node->req.GetPort();
  socklen_t length = sizeof(server_address);
  
  // try connecting
  if(connect(node->server_socket,(struct sockaddr*)&server_address, length)){
    cout << "Unable to connect to the server!" << endl;
    delete [] buffer;
    return;
    //exit(EXIT_FAILURE);
  }

  // try sending
  if(send(node->server_socket, buffer, reqLen, 0) < 0){
    cout << "Failed to send request to the server!" << endl;
    delete [] buffer;
    return;
    //exit(EXIT_FAILURE);
  }
  
  // done sending the request
  delete [] buffer;

  buffer = new char[BUFFER_SIZE];
  
  ssize_t recv_len;
  size_t end;
  while ( 0 < (recv_len = recv(node->server_socket, buffer, BUFFER_SIZE, MSG_DONTWAIT))){
    
    node->server_buffer.append(buffer, recv_len);
    node->buffer_num_chars += recv_len;
    //look for the end
    if(std::string::npos != (end = node->server_buffer.find("\r\n\r\n")))
      break;
  }
  // would have blocked
  if (recv_len < 0)
    return;
  
  // if value was stale before
  if(node->stale){
    HttpResponse check;
    check.ParseResponse(node->server_buffer.c_str(), node->buffer_num_chars);
    if(check.GetStatusCode() == "304"){
      node->finished = true;
      return;
    }
  }
  // setting the response
  node->resp.ParseResponse(node->server_buffer.c_str(), node->buffer_num_chars);
  node->finished = true;
  return;
}


//TODO: for Jeremy
class HttpProxyCache 
{
public:
  HttpProxyCache();

  //if the req exists in the cache and is not expired
  //then return the data string, else return "0"
  //then return a pointer to the data, else return NULL
  void Query(PeerRequest* pr);

  //try to cache the response. Will not cache it
  //if the object is not cacheable, (i.e. its private).
  //This function should be thread safe.
  void AttemptAdd(PeerRequest* pr);

private:

  struct CacheData {

    CacheData(char* data, int data_size, time_t expireTime) 
      : data(data), data_size(data_size), expireTime(expireTime) {}

    char* data;
    int data_size;
    time_t expireTime;
  };
  
  boost::unordered_map<string, CacheData> cache;

  pthread_mutex_t mutex;
};

HttpProxyCache::HttpProxyCache() 
{
  mutex = PTHREAD_MUTEX_INITIALIZER;
}


void HttpProxyCache::Query(PeerRequest* pr) 
{

  string url = pr->req.GetHost() + pr->req.GetPath();

  //find the data
  boost::unordered_map<string, CacheData>::const_iterator data = cache.find(url);
  
  //if the data was not found then return
  if(data == cache.end())
    {
      return;
    }


  //set the response to the cache data
  pr->resp.ParseResponse(data->second.data, data->second.data_size);


  //if the data is expired
  if(data->second.expireTime > time(NULL))
    {

      //the data is stale
      pr->stale = true;

      //find the last time the page was modified
      string lastModified = pr->resp.FindHeader("Last-Modified");

      //set "If-Modified-Since" header in order to perform conditional GET
      pr->req.ModifyHeader("If-Modified-Since", lastModified);
    }
  else
    {
      //the data is correct
      pr->finished = true;
    }
}

void HttpProxyCache::AttemptAdd(PeerRequest* pr)
{
  string expire_text = pr->resp.FindHeader("Expires");
  struct tm time_struct;
  time_t time;

  // parse the expire text
  if(strptime(expire_text.c_str(), 
              "%a, %d %b %Y %H:%M:%S GMT", 
              &time_struct) == NULL)
    {
      cout << "Expires header is in incorrect format." << endl <<
        "Assuming expired page." << endl;
      time = 0;
    }
  else
    {
      time = mktime(&time_struct);
    }

  //create the cache key

  string url = pr->req.GetHost() + pr->req.GetPath();
  
  //create the cache data
  int response_text_size = pr->resp.GetTotalLength();
  char* response_text = new char[response_text_size];
  pr->resp.FormatResponse(response_text);
  response_text[response_text_size] = '\0'; //terminate the string
  CacheData data(response_text, response_text_size, time);

  //obtain a lock on the cache
  if(pthread_mutex_lock(&mutex))
    {
      //report error but continue anyway and hope for the best
      cout << "Error locking cache mutex" << endl;
    }

  //insert the key and data into the cache
  
  cache.insert(pair<string, CacheData>(url, data));

  //unlock the cache
  if(pthread_mutex_unlock(&mutex))
    {
      cout << "Error unlocking cache mutex" << endl;
    }

  return;
}

HttpProxyCache http_cache;

void* servePeer(void* arg_sock)
{
  int peer_sock = *((int*) arg_sock);
 
  std::vector<struct PeerRequest> requests; // vector of PeerRequests
  char buffer[BUFFER_SIZE]; // buffer
  string request = ""; // request string
  string req_to_be_parsed; // string ready to be parsed by HttpRequest
  ssize_t recv_len; // recv return value
  size_t end; // position of the end of a request
  /* Loop forever:
     recv() from peer, put result onto the end of buffer b. */
  while(1)
  {
    recv_len = recv(peer_sock, buffer, BUFFER_SIZE, MSG_DONTWAIT);
    /*if recv() returns 0: (i.e. the connection has been closed)
       subtract 1 from num_connections (it needs to do this thread safe)
       thread should exit*/
    if(0 == recv_len)
    {
      // loop through the requests vector until all PeerRequests are done
      while(0 != requests.size())
      {
        unsigned int k = 0;
        // loop through the first ten
        while(10 > k && requests.size() > k)
        {
          // check the cache for the parsed url
          http_cache.Query(&requests[k]);
          // if it's in the cache
          if(requests[k].finished)
          {
            // make buffer to hold response
            size_t response_len = requests[k].resp.GetTotalLength();
            char formatted_response[response_len];
            // format response
            requests[k].resp.FormatResponse(formatted_response);
            // send data to user
            send(peer_sock, formatted_response, response_len, 0);
            // remove the PeerRequest from the list
            requests.erase(requests.begin() + k);
            // do not imcrement k because one has just been removed
          }  
          // response is not done yet
          else
          {
            // call getResponse on the PeerRequest
            getResponse(&requests[k]);
            // the response is now ready
            if(requests[k].finished)
            {
              // make buffer to hold response
              size_t response_len = requests[k].resp.GetTotalLength();
              char formatted_response[response_len];
              // format response
              requests[k].resp.FormatResponse(formatted_response);
              // send data to user
              send(peer_sock, formatted_response, response_len, 0);
              // remove the PeerRequest from the list
              requests.erase(requests.begin() + k);
              // do not imcrement k because one has just been removed
            }
            // the response is still not ready
            else
            {
              // increment k to move on to the next PeerRequest
              k++;
            }
          }
        }
      }
      close(peer_sock);
      pthread_mutex_lock(&num_connections_mutex);
      num_connections--;
      pthread_cond_signal(&num_connections_cond);
      pthread_mutex_unlock(&num_connections_mutex);
      pthread_exit(NULL);
    }
    else if(0 > recv_len && errno != EAGAIN && errno != EWOULDBLOCK)
      perror("Error occurred in recv");
    else
      request.append(buffer, recv_len);

    /*if newly recv()d stuff in b contains '\r\n\r\n' (two carriage returns in a row) then:
      split b into two parts (before the '\r\n\r\n' and after the '\r\n\r\n'*/
    if(std::string::npos != (end = request.find("\r\n\r\n")))
    {
      req_to_be_parsed = request.substr(0, (end + 4));
      // the second part becomes the new buffer b
      request = request.substr((end + 4));
      // the first part is parsed by HttpRequest,
      HttpRequest req;
      req.ParseRequest(req_to_be_parsed.c_str(), req_to_be_parsed.length() + 1);
      // put HttpRequest into a PeerRequest
      struct PeerRequest peer_req;
      peer_req.req = req;
      // put PeerRequest in requests vector
      requests.push_back(peer_req);
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
