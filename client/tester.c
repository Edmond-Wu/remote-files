
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "libnetfiles.h"



/////////////////////////////////////////////////////////////
//
// Function declarations 
//
/////////////////////////////////////////////////////////////



 

/////////////////////////////////////////////////////////////
//
// Declare global variables
//
/////////////////////////////////////////////////////////////




/////////////////////////////////////////////////////////////


int main(int argc, char *argv[])
{
    char *hostname = NULL;
    int  rc = FAILURE;
    int  fd = -1;


    if (argc < 2) {
        fprintf(stderr, "Usage: %s hostname\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    hostname = argv[1];



    //
    // Test 01: netserverinit in exclusive mode
    //
    rc = netserverinit( hostname, EXCLUSIVE_MODE );
    if ( rc == SUCCESS ) 
    {
        printf("test 01: PASSED: netserverinit \"%s\", exclusive, errno= %d, h_errno= %d\n",
                         hostname,errno,h_errno);
    } else {
        printf("test 01: FAILED: netserverinit \"%s\", exclusive, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 02: netopen "junk.txt", this test should pass
    //
    rc = netopen( "./junk.txt", O_RDONLY); 
    if ( rc == FAILURE ) 
    {
        printf("test 02: FAILED: netopen(\"junk.txt\",O_RDONLY), errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    } else {
        printf("test 02: PASSED: netopen(\"junk.txt\",O_RDONLY) returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    };
    printf("---------------------------------------------------------------------------\n");



    // Test 03: call netserverinit again with a bad hostname
    rc = netserverinit( "NoSuchHostName", TRANSACTION_MODE);
    if (( rc == FAILURE ) && (h_errno == HOST_NOT_FOUND))
    {
        printf("test 03: PASSED: netserverinit \"NoSuchHostName\" returns HOST_NOT_FOUND, errno= %d (%s), h_errno= %d\n",
                  errno, strerror(errno), h_errno);
    } else {
        printf("test 03: FAILED: netserverinit \"NoSuchHostName\" did not return HOST_NOT_FOUND, transaction, errno= %d (%s), h_errno= %d\n",
                  errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    // Test 04: netopen "junk.txt", this test should fail now
    rc = netopen( "junk.txt", O_RDWR); 
    if ( rc == FAILURE ) 
    {
        printf("test 04: PASSED: netopen failed because no netserverinit, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 04: FAILED: netopen should fail because no netserverinit, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 05: netserverinit in unrestricted mode
    //
    rc = netserverinit( hostname, UNRESTRICTED_MODE );
    if ( rc == SUCCESS ) 
    {
        printf("test 05: PASSED: netserverinit \"%s\", unrestricted, errno= %d, h_errno= %d\n",
                         hostname,errno,h_errno);
    } else {
        printf("test 05: FAILED: netserverinit \"%s\", unrestricted, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 06: netopen with a NULL pathname.  This test should fail.
    //
    rc = netopen( NULL, O_RDWR); 
    if ( rc == FAILURE ) 
    {
        printf("test 06: PASSED: netopen with NULL pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
    } else {
        printf("test 06: FAILED: netopen with NULL pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 07: netopen with a blank pathname.  This test should fail.
    //
    rc = netopen( "", O_RDWR); 
    if ( rc == FAILURE ) 
    {
        printf("test 07: PASSED: netopen with blank pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
    } else {
        printf("test 07: FAILED: netopen with blank pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 08: netopen with a non-existing pathname.  This test should fail.
    //
    rc = netopen( "NoSuchFile.xyz", O_RDWR); 
    if ((rc == FAILURE) && (errno == ENOENT)) 
    {
        printf("test 08: PASSED: netopen(\"NoSuchFile.xyz\",O_RDWR), errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 08: FAILED: netopen(\"NoSuchFile.xyz\",O_RDWR), errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 09: netopen with a directory name.  This test should fail.
    //
    rc = netopen( "/usr", O_RDWR); 
    if ((rc == FAILURE) && (errno == EISDIR)) 
    {
        printf("test 09: PASSED: netopen(\"/usr\",O_RDWR), errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 09: FAILED: netopen(\"/usr\",O_RDWR), errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 10: netopen "noaccess.txt" without access permission.  
    // i        This test should fail.
    //
    rc = netopen( "noaccess.txt", O_RDWR); 
    if ((rc == FAILURE) && (errno == EACCES)) 
    {
        printf("test 10: PASSED: netopen(\"noaccess.txt\",O_RDWR), errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 10: FAILED: netopen(\"noaccess.txt\",O_RDWR), errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 11: netopen "junk.txt", this test should pass
    //
    rc = netopen("junk.txt", O_RDONLY); 
    if ( rc != FAILURE ) 
    {
        printf("test 11: PASSED: netopen(\"junk.txt\",O_RDONLY) returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    } else {
        printf("test 11: FAILED: netopen(\"junk.txt\",O_RDONLY) returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 12: netopen "junk.txt", this test should pass
    //
    rc = netopen("junk.txt", O_WRONLY); 
    if ( rc != FAILURE ) 
    {
        printf("test 12: PASSED: netopen(\"junk.txt\",O_WRONLY) returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    } else {
        printf("test 12: FAILED: netopen(\"junk.txt\",O_WRONLY) returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 13: netopen "junk.txt", this test should pass
    //
    rc = netopen("junk.txt", O_WRONLY); 
    if ( rc != FAILURE ) 
    {
        printf("test 13: PASSED: netopen(\"junk.txt\",O_RDWR) returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    } else {
        printf("test 13: FAILED: netopen(\"junk.txt\",O_RDWR) returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 14: netclose "junk.txt", this test should pass
    //
    fd = netopen("junk.txt", O_WRONLY); 
    rc = netclose( fd );
    if ( rc != FAILURE ) 
    {
        printf("test 14: PASSED: netclose(%d) returns %d, errno= %d, h_errno= %d\n",
                 fd, rc, errno,h_errno);
    } else {
        printf("test 14: FAILED: netclose(%d) returns %d, errno= %d (%s), h_errno= %d\n",
                 fd, rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 15: netclose with an invalid fd, this test should fail
    //
    rc = netclose( 123 );
    if ((rc == FAILURE) && (errno == EBADF))
    {
        printf("test 15: PASSED: netclose(123) returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 15: FAILED: netclose(123) returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");




    // netserverinit to a hard-cdoed IP address in unrestricted mode
    //rc = test_netserverinit( "135.25.37.158", UNRESTRICTED_MODE);

}


////////////////////////////////////////////////////////////////////////////////

