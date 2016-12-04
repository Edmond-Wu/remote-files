#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "libnetfiles.h"


/////////////////////////////////////////////////////////////
//
// A structure defined to store information
// for the current net file server used.  
// This information is used by the client to
// maintain knowledge of which server it is
// current talking to.  The netserverinit
// function initializes this structure.
// All subsequent net function calls will be 
// directed to this net file server until it 
// is changed by another netserverinit call.  
// If the hostname variable is a null string, 
// that means the client has never called the
// netserverinit function to set up a server.
//
/////////////////////////////////////////////////////////////

typedef struct {
    char hostname[64];
    FILE_CONNECTION_MODE fcMode;
} NET_SERVER;




/////////////////////////////////////////////////////////////
//
// Function declarations 
//
/////////////////////////////////////////////////////////////

int     getSockfd( const char *hostname ); 
int     OldgetSockfd( const char *hostname ); 
int     isNetServerInitialized( NET_FUNCTION_TYPE iFunc );

int     netserverinit(char *hostname, int filemode);
int     netopen(const char *pathname, int flags);
ssize_t netread(int fildes, void *buf, size_t nbyte); 
ssize_t netwrite(int fildes, const void *buf, size_t nbyte); 
int     netclose(int fd);




/////////////////////////////////////////////////////////////
//
// Declare global variables
//
/////////////////////////////////////////////////////////////

NET_SERVER gNetServer;




/////////////////////////////////////////////////////////////


int getSockfd( const char * hostname )
{
    int sockfd = 0;

    struct sockaddr_in serv_addr;
    struct hostent *server = NULL;


    //
    // Create a new socket 
    //
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr,"libnetfiles: socket() failed, errno= %d\n", errno);
	return -1;
    }
  
    //
    // Find the address of the given server by name
    //
    server = gethostbyname(hostname);
    if (server == NULL) {
        errno = 0;
        h_errno = HOST_NOT_FOUND;
        //fprintf(stderr,"libnetfiles: host not found, h_errno= %d\n", h_errno);
	return -1;
    }

    //
    // Initialize the server address structure.  This 
    // structure is used to do the actual connect.
    //
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(PORT_NUMBER);
    if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
    {
        //errno = 0;
        //h_errno = HOST_NOT_FOUND;
        fprintf(stderr,"libnetfiles: cannot connect to %s, h_errno= %d\n", 
                hostname, h_errno);
	return -1;
    }

    return sockfd;
}


/////////////////////////////////////////////////////////////

int OldgetSockfd( const char * hostname )
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    int sfd = -1;
    int rc = 0;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Stream socket */
    hints.ai_flags = AI_PASSIVE;     /* For wildcard IP address */
    hints.ai_protocol = 0;           /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    //rc = getaddrinfo(hostname, PORT_NUMBER, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }


    //
    // getaddrinfo() returns a list of address structures.
    // Try each address until we successfully bind(2).
    // If socket(2) (or bind(2)) fails, we (close the socket
    // and) try the next address. 
    //

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;    // Success 

        close(sfd);
    }

    freeaddrinfo(result);   // No longer needed 

    if (rp == NULL) 
    {   
        // No address succeeded 
        //fprintf(stderr, "Could not bind an address to the socket\n");
        return -1;
    }

    return sfd;
}

/////////////////////////////////////////////////////////////

int isNetServerInitialized( NET_FUNCTION_TYPE iFunc )
{
    if ( (strcmp(gNetServer.hostname, "") == 0) ||
         (gNetServer.fcMode <= 0 ) )
    {
        switch (iFunc) {
            case NET_OPEN:
                //fprintf(stderr,"net server not initialized before calling netopen\n");
                break;

            case NET_READ:
                //fprintf(stderr,"net server not initialized before calling netread\n");
                break;

            case NET_WRITE:
                //fprintf(stderr,"net server not initialized before calling netwrite\n");
                break;

            case NET_CLOSE:
                //fprintf(stderr,"net server not initialized before calling netclose\n");
                break;

            default:
                break;
        }
        return FALSE;
    }

    return TRUE;
}

/////////////////////////////////////////////////////////////


/*******************************************************

  netserverinit needs to handle these error codes

       Required:

       Optional:

       Implemented h_errno:
           HOST_NOT_FOUND    =  1, host not found
           INVALID_FILE_MODE = 99, invalid file connection mode

       Implemented errno:
           EINVAL = 22, Invalid argument
           ECOMM  = 70, Communication error on send

******************************************************/
int netserverinit(char *hostname, int filemode)
{
    int rc = 0;
    int sockfd = -1;
    char msg[MSG_SIZE] = "";

    //
    // Clear errno and h_errno
    //
    errno = 0;
    h_errno = 0;


    //
    // Remove current net file server name 
    // and file connection mode setting.
    //
    strcpy(gNetServer.hostname, "");
    gNetServer.fcMode = INVALID_FILE_MODE;

    //
    // Verify the given file connection mode is valid
    //
    switch (filemode) {
        case UNRESTRICTED_MODE:
        case EXCLUSIVE_MODE:   
        case TRANSACTION_MODE:   
            break;

        default:
            h_errno = INVALID_FILE_MODE;
            fprintf(stderr, "netserverinit: invalid file connection mode\n");
            return FAILURE;
            break;
    }


    //
    // Verify given hostname
    //
    if ( hostname == NULL ) {
        errno = EINVAL;  // 22 = Invalid argument
        return FAILURE;
    };

    if ( strcmp(hostname,"") == 0 ) {
        errno = EINVAL;  // 22 = Invalid argument
        return FAILURE;
    };


    //
    // Get a socket to talk to my net file server
    //
    sockfd = getSockfd( hostname );
    if ( sockfd < 0 ) {
        errno = 0;
        h_errno = HOST_NOT_FOUND;
        //fprintf(stderr, "netserverinit: host not found, %s\n", hostname);
        //fprintf(stderr, "netserverinit: errno= %d, %s\n", errno, strerror(errno));
        //fprintf(stderr, "netserverinit: h_errno= %d, %s\n", h_errno, strerror(h_errno));
        return FAILURE;
    }
    //printf("netserverinit: sockfd= %d\n", sockfd);


    // 
    // Compose my net command to send to the server.  The format is:
    //
    //     netCmd,0,0,0
    //
    bzero(msg, MSG_SIZE);
    sprintf(msg, "%d,0,0,0", NET_SERVERINIT);


    //printf("netserverinit: send to server - \"%s\"\n", msg);
    rc = write(sockfd, msg, strlen(msg));
    if ( rc < 0 ) {
        // Failed to write command to server
        h_errno = ECOMM;  // 70 = Communication error on send
        //fprintf(stderr, "netserverinit: failed to write cmd to server.  rc= %d\n", rc);
        return FAILURE;
    }


    // 
    // Read the net response coming back from the server. 
    // The response message format is:
    //
    //   result,0,0,0
    //
    bzero(msg, MSG_SIZE);
    rc = read(sockfd, msg, MSG_SIZE -1);
    if ( rc < 0 ) {
        h_errno = ECOMM;  // 70 = Communication error on send
        //fprintf(stderr,"netserverinit: fails to read from socket\n");
        if ( sockfd != 0 ) close(sockfd);
        return FAILURE;
    }

    close(sockfd);  // Don't need this socket anymore

    //
    // Received a response back from the server
    //
    //printf("netserverinit: received from server - \"%s\"\n", msg);


    // Decode the response from the server
    sscanf(msg, "%d,", &rc);
    if ( rc == SUCCESS ) {
        //
        // Save the hostname of the net server.  All subsequent
        // network function calls will go this this net server.
        //
        strcpy(gNetServer.hostname, hostname);
        gNetServer.fcMode = (FILE_CONNECTION_MODE)filemode;

        //printf("netserverinit: netServerName= %s, connection mode= %d\n", 
        //         gNetServer.hostname, gNetServer.fcMode);
        //printf("netserverinit: server responded with SUCCESS\n");
    }

    return rc;
}


/////////////////////////////////////////////////////////////


/*******************************************************

  netopen needs to handle these error codes

       Required:
           EINTR  =  4, interrupted system call
           EROFS  = 30, Read-only file system

       Optional:
           EWOULDBLOCK = 11, Operation would block

       Implemented:
           EPERM  =  1, Operation not permitted
           ENOENT =  2, No such file or directory
           EACCES = 13, Permission denied
           EISDIR = 21, Is a directory
           EINVAL = 22, Invalid argument
           ENFILE = 23, File table overflow

******************************************************/

int netopen(const char *pathname, int flags)
{
    int netFd  = -1;
    int sockfd = -1;
    int rc     = 0;
    char msg[MSG_SIZE] = "";


    //
    // Clear errno and h_errno
    //
    errno = 0;
    h_errno = 0;

    if (pathname == NULL) {
        //fprintf(stderr,"netopen: pathname is NULL\n");
        errno = EINVAL;  // 22 = Invalid argument
        return FAILURE;
    } 
    if (strcmp(pathname,"") == 0) {
        //fprintf(stderr,"netopen: pathname is blank\n");
        errno = EINVAL;  // 22 = Invalid argument
        return FAILURE;
    } 
    //printf("netopen: pathname= %s, flags= %d\n", pathname, flags);

    if ( isNetServerInitialized( NET_OPEN ) != TRUE ) {
        errno = EPERM;  // 1 = Operation not permitted
        return FAILURE;
    }


    //
    // Get a socket to talk to my net file server
    //
    sockfd = getSockfd( gNetServer.hostname );
    if ( sockfd < 0 ) {
        // this error should not happen
        errno = 0;
        h_errno = HOST_NOT_FOUND;
        //fprintf(stderr, "netopen: host not found, %s\n", hostname);
        return FAILURE;
    }
    //printf("netopen: sockfd= %d\n", sockfd);


    // 
    // Compose my net command to send to the server.  The format is:
    //
    //     netCmd,connectionMode,fileOpenFlags,pathname
    //
    bzero(msg, MSG_SIZE);
    sprintf(msg, "%d,%d,%d,%s", NET_OPEN, gNetServer.fcMode, flags, pathname);


    //printf("netopen: send to server - \"%s\"\n", msg);
    rc = write(sockfd, msg, strlen(msg));
    if ( rc < 0 ) {
        // Failed to write command to server
        fprintf(stderr, "netopen: failed to write cmd to server.  rc= %d\n", rc);
        return FAILURE;
    }


    // 
    // Read the net response coming back from the server.
    // The response msg format is:
    //
    //    result,errno,h_errno,netFd
    //
    bzero(msg, MSG_SIZE);
    rc = read(sockfd, msg, MSG_SIZE -1);
    if ( rc < 0 ) {
        if ( sockfd != 0 ) close(sockfd);
        return FAILURE;
    }

    close(sockfd);  // Don't need this socket anymore

    //
    // Received a response back from the server
    //
    //printf("netopen: received from server - \"%s\"\n", msg);


    // Decode the response from the server
    sscanf(msg, "%d,%d,%d,%d", &rc, &errno, &h_errno, &netFd);
    if ( rc == FAILURE ) {
        //printf("netopen: server returns FAILURE, errno= %d (%s), h_errno=%d\n",
        //          errno, strerror(errno), h_errno);
        return FAILURE;
    }

    return netFd;
}

/////////////////////////////////////////////////////////////


/*******************************************************

  netclose needs to handle these error codes

       Required:
           EABDF  =  9, Bad file number

       Optional:
           none

       Implemented:
           none

******************************************************/

int netclose(int netFd)
{
    int fd = -1;
    int sockfd = -1;
    int rc     = 0;
    char msg[MSG_SIZE] = "";


    //
    // Clear errno and h_errno
    //
    errno = 0;
    h_errno = 0;


    if ( isNetServerInitialized( NET_CLOSE ) != TRUE ) {
        errno = EPERM;  // 1 = Operation not permitted
        return FAILURE;
    }


    //
    // Get a socket to talk to my net file server
    //
    sockfd = getSockfd( gNetServer.hostname );
    if ( sockfd < 0 ) {
        // this error should not happen
        errno = 0;
        h_errno = HOST_NOT_FOUND;
        //fprintf(stderr, "netclose: host not found, %s\n", hostname);
        return FAILURE;
    }
    //printf("netclose: sockfd= %d\n", sockfd);


    // 
    // Compose my net command to send to the server.  The format is:
    //
    //     netCmd,fd,0,0
    //
    bzero(msg, MSG_SIZE);
    sprintf(msg, "%d,%d,0,0", NET_CLOSE, netFd);


    //printf("netclose: send to server - \"%s\"\n", msg);
    rc = write(sockfd, msg, strlen(msg));
    if ( rc < 0 ) {
        // Failed to write command to server
        fprintf(stderr, "netclose: failed to write cmd to server.  rc= %d\n", rc);
        return FAILURE;
    }


    // 
    // Read the net response coming back from the server.
    // The response msg format is:
    //
    //    result,errno,h_errno,netFd
    //
    bzero(msg, MSG_SIZE);
    rc = read(sockfd, msg, MSG_SIZE -1);
    if ( rc < 0 ) {
        if ( sockfd != 0 ) close(sockfd);
        return FAILURE;
    }

    close(sockfd);  // Don't need this socket anymore

    //
    // Received a response back from the server
    //
    //printf("netclose: received from server - \"%s\"\n", msg);


    // Decode the response from the server
    sscanf(msg, "%d,%d,%d,%d", &rc, &errno, &h_errno, &fd);
    if ( rc == FAILURE ) {
        //fprintf(stderr, "netclose: server returns FAILURE, errno= %d (%s), h_errno=%d\n",
        //          errno, strerror(errno), h_errno);
        errno = EBADF;
        return FAILURE;
    }

    return SUCCESS;
}

/////////////////////////////////////////////////////////////


ssize_t netread(int fildes, void *buf, size_t nbyte)
{

    return 0;
}

/////////////////////////////////////////////////////////////


ssize_t netwrite(int fildes, const void *buf, size_t nbyte)
{

    return 0;
}

/////////////////////////////////////////////////////////////
