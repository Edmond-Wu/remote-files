#ifndef 	_LIBNETFILES_H_
#define    	_LIBNETFILES_H_


/////////////////////////////////////////////////////////////
//
// This "libnetfiles.h" file is shared by both the client
// and the server.  There are definitions common to both
//
/////////////////////////////////////////////////////////////




/////////////////////////////////////////////////////////////
//
// Constant and type definitions 
//
/////////////////////////////////////////////////////////////


//
// Hard-coded port number for net file server
//
#define NET_SERVER_PORT_NUM  54321  


//
// Max size of the file descriptor table.  
//
#define FD_TABLE_SIZE   128  


//
// Size of each message between the
// server and the client.
//
#define MSG_SIZE  256 


//
// Number of sockets available to use for file transfer
//
//
#define MAX_FILE_TRANSFER_SOCKETS   10  



//
// Net file read and write are done in multiple "chunk" 
// of bytes each of this size.  This chunk size is also
// used to calculate how many listener ports are needed 
// to faciliate a net file read or write. 
//
//
#define DATA_CHUNK_SIZE   2048  



//
// Constant definitions
//
#define TRUE    1
#define FALSE   0

#define SUCCESS 0
#define FAILURE -1



//
// Network file connection mode
//
typedef enum {
    UNRESTRICTED_MODE = 1,
    EXCLUSIVE_MODE    = 2,
    TRANSACTION_MODE  = 3,
    INVALID_FILE_MODE = 99
} FILE_CONNECTION_MODE;



//
// Net server function types
//
typedef enum {
    NET_SERVERINIT = 1,
    NET_OPEN  = 2,
    NET_READ  = 3,
    NET_WRITE = 4,
    NET_CLOSE = 5,
    INVALID   = 99
} NET_FUNCTION_TYPE;





/////////////////////////////////////////////////////////////
//
// Function declarations 
//
/////////////////////////////////////////////////////////////

extern int netserverinit(char *hostname, int filemode);
extern int netopen(const char *pathname, int flags);
extern ssize_t netread(int fildes, void *buf, size_t nbyte); 
extern ssize_t netwrite(int fildes, const void *buf, size_t nbyte); 
extern int netclose(int fd);



#endif    // _LIBNETFILES_H_

