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

const int LISTEN_PORT = 14832;
const int MAX_CONNECTIONS = 50;
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

  //the request from the peer
  HttpRequest req;

  //the response to the peer
  HttpResponse resp;

  bool finished; //true if resp is ready to be sent to peer
  
  //the socket for the server
  int server_socket;
  
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
}



//TODO: for Jonathan
//Given an HttpRequest object, contacts the
//server and returns the response in a string.
//This function should use req.FormatRequest() to
//convert the request into "relative URL + Host 
//header" format (see the spec) before sending
//it to the server.
void getResponse(PeerRequest* node){

  cout<< "test25" << endl;
  static int times_run = 0;
  times_run++;
  //cout << server_address.sin_port << " " << htons(server_address.sin_port) << endl;
  // try connecting

  // create the socket
  node->server_socket = socket(AF_INET, SOCK_STREAM, 0);

  // get the server ip
  //cout << node->req.GetHost() << endl;
  struct addrinfo* ai = NULL;
  
  //convert port to string
  stringstream ss;
  ss << node->req.GetPort();
  string portString = ss.str();
  if(times_run == 2)
    {
      cout << "test7" << endl;
    }
  cout << "portString: " << portString << endl;
  if(getaddrinfo((node->req.GetHost()).c_str(), portString.c_str(), NULL, &ai)) {
    cout << "Error getting address info" << endl;
    exit(1);
  }
  if(times_run == 2)
    {
      cout << "test8" << endl;
    }
  if(connect(node->server_socket,ai->ai_addr, ai->ai_addrlen)){
    cout << "Unable to connect to the server!" << endl;
    //return;
    exit(1);
  }



      //cout << "test26.1" << endl;

      // format the request
  size_t reqLen = node->req.GetTotalLength();
  char* req_buffer = new char[reqLen];
  node->req.FormatRequest(req_buffer);

  int bytes_read = 0;

  // try sending
  if(send(node->server_socket, req_buffer, reqLen, 0) < 0){
    cout << "Failed to send request to the server!" << endl;
    delete [] req_buffer;
    return;
    //exit(EXIT_FAILURE);
  }
  //cout << "test27" << endl;

  delete [] req_buffer;
 

  char resp_buffer[BUFFER_SIZE];
  
  //cout << "test27.1" << endl;
  ssize_t recv_len;
  size_t end;

  string response;

  /*  while ( 0 < (recv_len = recv(node->server_socket, resp_buffer, BUFFER_SIZE, MSG_DONTWAIT))){*/

  while(string::npos == (end = response.find("\r\n\r\n")))
    {
      recv_len = recv(node->server_socket, resp_buffer, BUFFER_SIZE, MSG_DONTWAIT);
      cout << "test112" << endl;

      if(recv_len > 0)
        {
          response.append(resp_buffer, recv_len);
          bytes_read += recv_len;
        }
      
    }

  //split into server_buffer and content
  node->content = response.substr(end + 4);
  response = response.substr(0, end+4);

  cout << "content1: " << node->content << endl;
  cout << "server_buffer1: " << response << endl;
  cout << "bytes_read: " << bytes_read << endl;
  cout << "content1size " << node->content.size() << endl;
  cout << "server_buffer1size " << response.size() << endl;

    //cout << "test27.2" <<endl;
    
    //node->server_buffer.append(resp_buffer, recv_len);
    //node->buffer_num_chars += recv_len;
  //}

  

  /*if(recv_len == 0) {
    cout << "the connection was closed" << endl;
  }*/
  //cout << node->server_buffer << endl;
  // would have blocke
  /*if(std::string::npos != (end = node->server_buffer.find("\r\n\r\n")))  
    {*/

  // if value was stale before
  if(node->stale){
    HttpResponse check;
    cout << "test48" << endl;
    check.ParseResponse(response.c_str(), response.size());
    //cout << "test49" << endl;
    if(check.GetStatusCode() == "304"){
      cout << endl << "RECIEVED 304" << endl;
      char buff[500];
      memset(buff, 0, 500);
      cout << endl << "Request after 304" << endl << node->req.FormatRequest(buff) << endl;
      cout << endl << "content after 304" << endl << node->content << endl;
      node->finished = true;
      return;
    }
  }
  cout << "test0" << endl;
  //obtain the content length
  HttpResponse parser;
  parser.ParseResponse(response.c_str(), response.size());
  string cont_length_str = parser.FindHeader("Content-Length");
  unsigned int cont_length = 0;
  if(cont_length_str.size() != 0)
    cont_length = atoi(cont_length_str.c_str());
  //unsigned int total_length = end + 4 + cont_length;
 
  cout << "test1" << endl;
  //block until we finish reading the content
  while(node->content.size() < cont_length)
    {
      recv_len = recv(node->server_socket, resp_buffer, BUFFER_SIZE, MSG_DONTWAIT);
      if(recv_len > 0)
        node->content.append(resp_buffer, recv_len);
    }
  cout << "content: " << node->content << endl;

  //cout << "test46" << endl;
      
      cout << "Server_buffer raw: " << endl << response << endl;
      // setting the response
      
      node->resp.ParseResponse(response.c_str(), response.size());
      char bs[1000];
      node->resp.FormatResponse(bs);
      cout << "Formatted Response: " << bs << endl;
      cout << "test51" << endl;
      node->finished = true;
      close(node->server_socket);
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

  string url = pr->req.GetHost() +  pr->req.GetPath();

  //find the data
  boost::unordered_map<string, CacheData>::const_iterator data = cache.find(url);
  
  //if the data was not found then return
  if(data == cache.end())
    {
      return;
    }


  //set the response to the cache data
  cout << "second header size " << data->second.header_size << endl;
  for(int i = 0; i < 50; i++)
    {
      cout << data->second.header[i];
    }
  pr->resp.ParseResponse(data->second.header, data->second.header_size);
  pr->content = data->second.content;
  
  char bufff[300];
  memset(bufff, 0, 300);
  string r = pr->resp.FormatResponse(bufff);
  cout << "resp " << r << endl;
    cout << "cntent " << pr->content << endl;  

  cout << "expireTime" << data->second.expireTime << endl;
  cout << "time" << time(NULL) << endl;
  //if the data is expired
  if(data->second.expireTime < time(NULL))
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

  cout << "expire_text: " << expire_text << endl;

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
      time = mktime(&time_struct) - timezone + 3600;
    }

  //create the cache key

  string url = pr->req.GetHost() + pr->req.GetPath();

  boost::unordered_map<string, CacheData>::const_iterator existingData = cache.find(url);

  if(existingData == cache.end() || existingData->second.expireTime < time)
    {
  
      //create the cache data
      int response_text_size = pr->resp.GetTotalLength();
      cout << "response_text_size " << response_text_size << endl;
      char* response_text = new char[response_text_size + 1];
      pr->resp.FormatResponse(response_text);
      response_text[response_text_size] = '\0'; //terminate the string
      CacheData data(response_text, response_text_size, pr->content, time);

      //obtain a lock on the cache
      if(pthread_mutex_lock(&mutex))
        {
          //report error but continue anyway and hope for the best
          cout << "Error locking cache mutex" << endl;
        }

      //insert the key and data into the cache
      cout<< "url: " << url << endl;
      cache.insert(pair<string, CacheData>(url, data));
      cout <<  "test111" << endl;
  

      //unlock the cache
      if(pthread_mutex_unlock(&mutex))
        {
          cout << "Error unlocking cache mutex" << endl;
        }
      cout << "test115"<< endl;
    }
  return;
}

HttpProxyCache http_cache;

void* servePeer(void* arg_sock)
{
  int peer_sock = *((int*) arg_sock);
  //cout << "test7" << endl; 
  //std::vector<struct PeerRequest> requests; // vector of PeerRequests
  struct PeerRequest peer_req;
  char buffer[BUFFER_SIZE]; // buffer
  string request = ""; // request string
  string req_to_be_parsed; // string ready to be parsed by HttpRequest
  ssize_t recv_len; // recv return value
  size_t end; // position of the end of a request
  static int i = 0;
  /* Loop forever:
     recv() from peer, put result onto the end of buffer b. */

  //ofstream test;
  //test.open("test");
  while(1)
  {
    cout << "test8" << endl;
    request = "";
    //loop until we get a request
    while(std::string::npos == (end = request.find("\r\n\r\n")))
      {
        cout << "peer_sock: " << peer_sock << endl;
        recv_len = recv(peer_sock, buffer, BUFFER_SIZE, MSG_DONTWAIT);
        //cout << recv_len << endl;
    //cout << "Error" << errno;
    /*if recv() returns 0: (i.e. the connection has been closed)
       subtract 1 from num_connections (it needs to do this thread safe)
       thread should exit*/

        if(recv_len == -1) {
          switch(errno) {
          case EAGAIN:
            cout << "eagain" <<endl;
            break;
          case EBADF:
            cout << "ebadf"<<endl;
            break;
          case ECONNREFUSED:
            cout << "Econr"<<endl;
            break;
          case EFAULT:
            cout << "fult"<<endl;
            break;
          case EINTR:
            cout << "EinR"<<endl;
            break;
          case EINVAL:
            cout << "EinVAL"<<endl;
            break;
          case ENOMEM:
            cout << "ENinMEM"<<endl;
            break;
          case ENOTCONN:
            cout << "ENinCONN"<<endl;
            break;
          case ENOTSOCK:
            cout << "EomSOCK"<<endl;
            break;
  

          }
        }
        //cout << recv_len << endl;
      
    
        if(recv_len == 0) {
          close(peer_sock);
          pthread_mutex_lock(&num_connections_mutex);
          num_connections--;
          pthread_cond_signal(&num_connections_cond);
          pthread_mutex_unlock(&num_connections_mutex);          

          pthread_exit(NULL);
        }
        if(0 > recv_len && errno != EAGAIN && errno != EWOULDBLOCK)
          {
           
           
            perror("Error occurred in recv");
           
            close(peer_sock);
            pthread_mutex_lock(&num_connections_mutex);
            num_connections--;
            pthread_cond_signal(&num_connections_cond);
            pthread_mutex_unlock(&num_connections_mutex);
            pthread_exit(NULL);
          }
        else if(recv_len > 0)
          {
            cout << "test10 " << recv_len << endl;
            cout << "recv_len: " << recv_len << endl;
            buffer[recv_len] = '\0';
            string bf = buffer;
            cout << "buffer: " << bf << endl;
            request.append(buffer, recv_len);
            //cout << "raw request: " << request << endl;
            //cout << "test11" << endl;
            //process the buffer
          }
      }
    cout << "raw request: " << request << endl;
    //cout << "test21" << endl;
    req_to_be_parsed = request.substr(0, (end + 4));
    // the second part becomes the new buffer b
    //cout << "end: " << end << endl;
    //cout << "npos: " << std::string::npos << endl;
    request = request.substr((end + 4));
    //cout << "test23" << endl;
    //cout << req_to_be_parsed;
    // the first part is parsed by HttpRequest,
    HttpRequest req;
    //cout << req_to_be_parsed << endl;
    //cout << "test40" << endl;
    req.ParseRequest(req_to_be_parsed.c_str(), req_to_be_parsed.length() + 1);
    char b[200] = "hello";
    req.FormatRequest(b);
    cout << "host: " << req.GetHost()<< endl;
    cout << "port: " << req.GetPort()<< endl;
    cout << "path: " << req.GetPath()<< endl;
    cout << "formatted request: " << b << endl;
    //cout << "test41" << endl;
    // put HttpRequest into a PeerRequest
    peer_req.req = req;
    // put PeerRequest in requests vector

    //http_cache.Query(&peer_req);

    cout << "test117" << endl;
    if(!peer_req.finished)
      {
        cout << "test114" << endl;
        getResponse(&peer_req);
      }

    cout << "test113" << endl;
    size_t response_len = peer_req.resp.GetTotalLength();
    char* formatted_response = new char[response_len + 1];
    // format response
    peer_req.resp.FormatResponse(formatted_response);
    formatted_response[response_len + 1] = '\0';
    cout << "thread_id " << pthread_self() << endl;
    string ffr = formatted_response;
    cout << "Final formatted response:" << endl << ffr << endl;
    cout << "Final content: " << endl << peer_req.content << endl;
    // send data to user
    string fname = "testa";
    fname[4] = '0' + i;
    //int fd = open(fname.c_str(), O_CREAT);
    //http_cache.AttemptAdd(&peer_req);
    // if(i > 0) {
    send(peer_sock, formatted_response, response_len, 0);
      //write(fd, formatted_response, response_len - 1);
      //}
      //else {
      //send(peer_sock, formatted_response, response_len, 0);
      //write(fd, formatted_response, response_len);
      //}
    //close(fd);
    send(peer_sock, peer_req.content.c_str(), peer_req.content.size(), 0);
    i++;
    
    delete formatted_response;
    peer_req.finished = false;
    if(peer_req.req.FindHeader("Connection").compare("close") == 0)
      {
        cout << "closing thread" << endl;
        close(peer_sock);
        pthread_exit(EXIT_SUCCESS);
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

  //cout << "test0" << endl;
  pthread_t threads[MAX_CONNECTIONS];
  //bool thread_ran[MAX_CONNECTIONS];

  //no threads have been run yet
  //for(int i = 0; i < MAX_CONNECTIONS; i++)
    //thread_ran[i] = false;

  int i = 0;
  //begin accepting connections
  while(true)
  {
    //cout << "test1" << endl;
      // check if we have too many connections
      /*if(num_connections >= MAX_CONNECTIONS)
      {
        pthread_mutex_lock(&num_connections_mutex);
        // block until a thread signals (when it exits)
        pthread_cond_wait(&num_connections_cond, &num_connections_mutex);
        pthread_mutex_unlock(&num_connections_mutex);
      }*/

      //cout << "test1.5" << endl;

      //accept the connection from the peer
      struct sockaddr_in peer_addr;
      socklen_t addr_len = sizeof(peer_addr);
      int peer_sock = accept(listen_sock, 
                             (struct sockaddr*) &peer_addr, 
                             &addr_len);
      //cout << "test2" << endl;

      if(peer_sock <= 0)
        {
          //error accepting
          cout << "Failure to accept" << endl;
          exit(EXIT_FAILURE);
        }

      //thread_ran[i] = true;
      pthread_create(&threads[i], NULL, &servePeer, &peer_sock);

      cout << "num threads: " << i << endl;
      i++;
      //TODO: For Sherri
      //Look into using Pthreads or Boost threads instead of fork(), since
      //fork() doesn't allow the child processes to share the cache.

      //fork a child to serve the peer
      //int i;
      /*for(i = 0; i < MAX_CONNECTIONS; i++)
        {
          //cout << "test3" << endl;
        //if the thread hasn't ran yet or if the thread has exited
        if(!thread_ran[i] || ESRCH == pthread_kill(threads[i], 0))
        {
          //cout << "test4" << endl;
          thread_ran[i] = true;
          pthread_create(&threads[i], NULL, &servePeer, &peer_sock);
          break;
        }*/
        //cout << "test5" << endl;
      }
      pthread_mutex_lock(&num_connections_mutex);
      num_connections++;
      pthread_mutex_unlock(&num_connections_mutex);
}

  // join all threads
  /*int i;
  for(i = 0; i < MAX_CONNECTIONS; i++)
    pthread_join(threads[i], NULL);

    return 0;*/

