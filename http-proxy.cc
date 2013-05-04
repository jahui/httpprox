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
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

const int LISTEN_PORT = 37621;
const int MAX_CONNECTIONS = 20;
const int BUFFER_SIZE = 512;


struct PeerRequest {

  PeerRequest();

  //the request from the peer
  HttpRequest req;

  //the response to the peer
  HttpResponse resp;

  bool finished; //true if resp is ready to be sent to peer
  
  //the socket for the server
  int server_socket;

  //the socket for the peer
  int peer_socket;
  
  //this buffer holds the server response in its text form
  //string server_buffer;
  string content;
  bool stale; //true if the cache returned a stale resp
};

PeerRequest::PeerRequest() {
  finished = false;
  server_socket = -1;
  //server_buffer = "";
  stale = false;
  peer_socket = -1;
}



//Given an PeerRequest object, contacts the
//server and returns the response by modifying the
//PeerRequest.
void getResponse(PeerRequest* node)
{
  //DEBUG
  cout << "Entering getResponse " << endl;

  // create the socket
  node->server_socket = socket(AF_INET, SOCK_STREAM, 0);

  //convert port to string
  stringstream ss;
  ss << node->req.GetPort();
  string portString = ss.str();

  // get the server ip
  struct addrinfo* ai = NULL;
  if(getaddrinfo((node->req.GetHost()).c_str(), portString.c_str(), NULL, &ai)) 
    {
      cout << "Error getting address info" << endl;
      exit(1);
    }


  // connect to the server
  if(connect(node->server_socket,ai->ai_addr, ai->ai_addrlen))
    {
      cout << "Unable to connect to the server!" << endl;
      exit(1);
    }

  // format the request
  size_t reqLen = node->req.GetTotalLength();
  char* req_buffer = new char[reqLen];
  node->req.FormatRequest(req_buffer);

  int bytes_read = 0;

  // try sending
  if(send(node->server_socket, req_buffer, reqLen, 0) < 0)
    {
      cout << "Failed to send request to the server!" << endl;
      delete [] req_buffer;
      exit(1);
    }

  delete [] req_buffer;
 

  char resp_buffer[BUFFER_SIZE];
  ssize_t recv_len;
  size_t end;
  string response;

  // loop until we have a response
  while(string::npos == (end = response.find("\r\n\r\n")))
    {
      recv_len = recv(node->server_socket, resp_buffer, BUFFER_SIZE, 0 /*MSG_DONTWAIT*/);
      //cout << recv_len << endl;
      if(recv_len > 0)
        {
          response.append(resp_buffer, recv_len);
          bytes_read += recv_len;
        }
      
    }

  //split into content and header
  string content = response.substr(end + 4);
  response = response.substr(0, end+4);


  // if the value was stale from the cache
  if(node->stale){

    HttpResponse check;
    check.ParseResponse(response.c_str(), response.size());

    //check if the server sent response 304
    if(check.GetStatusCode() == "304")
      {

        //we are done with this node
        node->finished = true;
        
        //DEBUG
        cout << "Recieved 304" << endl;
        return;
      }
  }


  //obtain the content length
  HttpResponse parser;
  parser.ParseResponse(response.c_str(), response.size());
  string cont_length_str = parser.FindHeader("Content-length");
  unsigned int cont_length = 0;

  if(cont_length_str.size() != 0)
    cont_length = atoi(cont_length_str.c_str());

  if(cont_length == 0)
    {
      cont_length_str = parser.FindHeader("Content-Length");
      if(cont_length_str.size() != 0)
        cont_length = atoi(cont_length_str.c_str());
    }
  
 
  //DEBUG
  cout << "Length of content from header " << cont_length << endl;
  
  //block until we finish reading the content
  while(content.size() < cont_length)
    {
      recv_len = recv(node->server_socket, resp_buffer, BUFFER_SIZE, 0 /*MSG_DONTWAIT*/);
      if(recv_len > 0)
        content.append(resp_buffer, recv_len);
    }
  
  //DEBUG
  cout << "Length of actual content " << content.size() << endl;  

  // parse the response
  node->resp.ParseResponse(response.c_str(), response.size());

  //set the content
  node->content = content;

  //this node is complete
  node->finished = true;
  if(node->peer_socket != node->server_socket)
    {
      close(node->server_socket);
    }
  cout << "Leaving getResponse" << endl;
  return;
}


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

    CacheData(char* header, int header_size, string content, time_t expireTime) 
      : header(header), header_size(header_size), content(content),
        expireTime(expireTime)  {}

    char* header;
    int header_size;
    string content;
    time_t expireTime;
  };
  
  boost::unordered_map<string, CacheData> cache;

  pthread_mutex_t mutex;
};

HttpProxyCache::HttpProxyCache() 
{
  pthread_mutex_init(&mutex, NULL);
}


void HttpProxyCache::Query(PeerRequest* pr) 
{
  //DEBUG
  cout << "Entering Query" << endl;

  string url = pr->req.GetHost() +  pr->req.GetPath();

  //find the data
  boost::unordered_map<string, CacheData>::const_iterator data = cache.find(url);
  
  //if the data was not found then return
  if(data == cache.end())
    {
      //DEBUG
      cout << "Data was not found in the Query" << endl;

      return;
    }


  //set the response and content to the cache data
  pr->resp.ParseResponse(data->second.header, data->second.header_size);
  pr->content = data->second.content;

  time_t currTime = time(NULL);

  //DEBUG
  cout << "currTime " << currTime << endl;
  cout << "expireTime " << data->second.expireTime << endl;

  //if the data is expired
  if(data->second.expireTime < currTime)
    {
      //DEBUG
      cout << "Found stale data in the Query" << endl;
      //the data is stale
      pr->stale = true;

      //find the last time the page was modified
      string lastModified = pr->resp.FindHeader("Last-Modified");

      //set "If-Modified-Since" header in order to perform conditional GET
      pr->req.ModifyHeader("If-Modified-Since", lastModified);
    }
  else
    {
      //DEBUG
      cout << "Found data in the Query" << endl;

      //the data is correct
      pr->finished = true;
    }


}

void HttpProxyCache::AttemptAdd(PeerRequest* pr)
{
  //DEBUG
  cout << endl << "Entering AttemptAdd" << endl;

  string expire_text = pr->resp.FindHeader("Expires");
  struct tm time_struct;
  time_t expTime;


  // parse the expire text
  if(strptime(expire_text.c_str(), 
              "%a, %d %b %Y %H:%M:%S GMT", 
              &time_struct) == NULL)
    {
      cout << "Expires header is in incorrect format." << endl <<
        "Assuming expired page." << endl;
      expTime = 0;
    }
  else
    {
      // we need to add an hour for some reason. DST?
      // Also need an extra 2 seconds to make the script work
      expTime = mktime(&time_struct) - timezone + 2;
    }

 
  

  //create the cache key
  string url = pr->req.GetHost() + pr->req.GetPath();

  boost::unordered_map<string, CacheData>::const_iterator existingData = cache.find(url);

  //DEBUG
  if(existingData != cache.end())
    {
      cout << "expTime: " << expTime << endl;
      cout << "existingExpTime" << endl;
    }



  // if the data is not in the cache or it is fresher than what's in the cache
  if(existingData == cache.end() || existingData->second.expireTime < expTime)
    {  
      //DEBUG
      cout << "Creating Cache Data" << endl;

      //create the cache data
      int response_text_size = pr->resp.GetTotalLength();
      char* response_text = new char[response_text_size + 1];
      pr->resp.FormatResponse(response_text);
      response_text[response_text_size] = '\0'; //terminate the string
      CacheData data(response_text, response_text_size, pr->content, expTime);

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
    }
  //DEBUG
  cout << "Leaving attemptAdd" << endl;

  return;
}

HttpProxyCache http_cache;

void* servePeer(void* arg_sock)
{
  int peer_sock = *((int*) arg_sock);
  struct PeerRequest peer_req;
  char buffer[BUFFER_SIZE]; // buffer
  string request = ""; // request string
  string req_to_be_parsed; // string ready to be parsed by HttpRequest
  ssize_t recv_len; // recv return value
  size_t end; // position of the end of a request
  
  peer_req.peer_socket = peer_sock; 

  //loop through multiple requests
  while(1)
    {
    request = "";
    //loop until we get a full request
    while(std::string::npos == (end = request.find("\r\n\r\n")))
      {

        recv_len = recv(peer_sock, buffer, BUFFER_SIZE, 0 /*MSG_DONTWAIT*/);
    
        //if the connection is closed then exit
        if(recv_len == 0) 
          {
            close(peer_sock);
            pthread_exit(NULL);
          }
        //if there is an error, output error and exit
        else if(0 > recv_len && errno != EAGAIN && errno != EWOULDBLOCK)
          {
           
           
            perror("Error occurred in recv");
           
            close(peer_sock);
            pthread_exit(NULL);
          }
        //if we recieve some bytes, append them to the request buffer
        else if(recv_len > 0)
          {
            request.append(buffer, recv_len); 
          }
      }
    
    //DEBUG
	cout << "Raw request: " << request << endl;


    // split the request into two parts, the full request to be parsed
    // and whatever is left over
    req_to_be_parsed = request.substr(0, (end + 4));
    request = request.substr((end + 4));

    // the first part is parsed by HttpRequest,
    HttpRequest req;
    req.ParseRequest(req_to_be_parsed.c_str(), req_to_be_parsed.length() + 1);

    // put HttpRequest into the PeerRequest
    peer_req.req = req;

    // give the peer request to the cache for processing
    http_cache.Query(&peer_req);

    // if the cache got a hit, the peer_req will be finished, if not we
    // we need to get from the server
    if(!peer_req.finished)
      {
        // get the response from the server
        getResponse(&peer_req);
      }


    // bring the formatted response into a buffer
    size_t response_len = peer_req.resp.GetTotalLength();
    char* formatted_response = new char[response_len + 1];
    peer_req.resp.FormatResponse(formatted_response);
    formatted_response[response_len] = '\0';
    
    // output the final response for debugging
    string ffr = formatted_response;
    cout << endl;
    cout << "Final formatted response:" << endl << ffr << endl;
    cout << "Final content: " << endl << peer_req.content << endl;

    // add the response to the cache
    http_cache.AttemptAdd(&peer_req);

    // output the header and the content to the peer
    send(peer_sock, formatted_response, response_len, 0);
    send(peer_sock, peer_req.content.c_str(), peer_req.content.size(), 0);

    // delete the buffer we created earlier
    delete formatted_response;

    // this request is finished, but mark it false so
    // we are ready for the next one
    peer_req.finished = false;

    // if the peer set the request to "close" then close
    if(peer_req.req.FindHeader("Connection").compare("close") == 0)
      {
        cout << "closing thread" << endl;
        close(peer_sock);
        pthread_exit(NULL);
      }
    }
  
  cout << "End of serve peer" << endl;
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

  int num_threads = 0;
  while(true)
    {
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

      // create a new thread
      pthread_create(&threads[num_threads], NULL, &servePeer, &peer_sock);

      num_threads++;
      if(num_threads == MAX_CONNECTIONS) 
        {
          num_threads = 0;
          for(int j = 0; j < MAX_CONNECTIONS; j++) 
            {
              pthread_join(threads[j], NULL);
            }
        }   
    }
}
