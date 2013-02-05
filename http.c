/* tag: http protocol implementation file for PalmHTTP
 * arch-tag: http protocol implementation file for PalmHTTP
 *
 * PalmHTTP - an HTTP library for Palm devices
 * 
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is PalmHTTP.
 *
 * The Initial Developer of the Original Code is
 * Mike Rowehl <miker@bitsplitter.net>
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Mike Rowehl <miker@bitsplitter.net>
 *
 * ***** END LICENSE BLOCK *****
 */

#include <PalmOS.h>
#include <Unix/sys_socket.h>

#include "http.h"

Err errno;

#define HTTP_POST_METH "POST "
#define HTTP_GET_METH "GET "
#define HTTP_VERSION " HTTP/1.0"
#define HTTP_HOST_HDR "Host: "
#define HTTP_CONTENTLENGTH_HDR "Content-Length: "
#define HTTP_LINE_ENDING "\r\n"
#define HTTP_CONTENTTYPE_LINE "Content-Type: text/xml" ## HTTP_LINE_ENDING
#define HTTP_USERAGENT_LINE \
        "User-Agent: PalmHTTP/0.1-PalmOS" ## HTTP_LINE_ENDING

#define HTTPLIB_TYPE 'TEMP'
#define HTTPLIB_NAME "HTTPLib_Scratch_Area"

/* Size of strings used to hold ascii conversion of numbers */
#define CLS_LENGTH (11)


/*
 * Used to represent the different stages of processing an HTTP response (the
 * first line of the response, the headers, and the body of the message).  The
 * PS_Done state is set after the entire body has been read and there is no
 * more data expected.  PS_Error is used for any error condition.
 */

typedef enum ParseState_enum {
    PS_ResponseLine,
    PS_Headers,
    PS_Body,
    PS_Done,
    PS_Error
} ParseState;


/*
 * Used to hold the values associated with processing an HTTP request/response
 * cycle.  Some of the values in here aren't exposed to end users yet, cause 
 * there's a lot we still don't support.  The readBuffer and bufferPos fields
 * are used in combination.  bufferPos holds the index of the next position to
 * fill in the buffer.  There are a few convenience and cover functions to 
 * keep the bufferPos field in sync with the readBuffer.  endOfStream is used
 * to hold the end of file result from the socket.  If we don't have a 
 * content length header we have to rely on hitting the end of the stream to 
 * tell us how much data there is.
 */

#define READ_BUF_SIZE (2048)

typedef struct HTTPParse_struct {
    ParseState state;
    unsigned int responseCode;
    char *saveFileName;
    void *fd;
    unsigned long contentLength;
    unsigned long contentRead;
    Int8 endOfStream;
    Int8 needData;
    UInt16 bufferPos;
    char readBuffer[READ_BUF_SIZE];
} HTTPParse;


/*
 * There are still some servers that behave poorly on certain combinations of
 * valid network operations (if you don't feed them enough data for them to 
 * get what they want in a single network read).  Since it's also slightly more
 * efficient to form the image of the headers and send them in larger network
 * writes, we use a scratch record in a database to accumulate the request line
 * and headers and then send off the whole thing.  The struct has n error flag
 * field that's used to short circuit requests, so that we can just push data 
 * in without checking to see what the status is.  And then at the end we 
 * check once to see if the whole batch was successful.
 */

typedef struct ScratchRec_struct {
    MemHandle handle;
    UInt16 index;
    UInt16 size;
    UInt8 errFlag;
} ScratchRec;


/*
 * Local prototypes (private to this file)
 */

/* Temp scratch area */
static int StartScratchArea( ScratchRec *scratch );
static void AddToScratch( ScratchRec *scratch, char *text, int length );
#define AddScratchLine( scratch, text ) \
          AddToScratch( scratch, text, StrLen( text ) )

/* Parse read buffer handling */
static UInt16 BufSizeRemaining( HTTPParse *parse );
static void BufConsumeToPointer( HTTPParse *parse, char *newFirst );
static char *NextBufByte( HTTPParse *parse );
static int FillReadBuff( NetSocketRef sock, HTTPParse *parse );

/* Parse functions */
static int MarkEOL( HTTPParse *parse );
static char *ParseResponseCode( char *start );
static void ParseEngine( NetSocketRef sock, HTTPParse *parse );
static void ParseResponseLine( HTTPParse *parse );
static void ParseHeaders( HTTPParse *parse );
static void ParseBody( HTTPParse *parse );

/* Network cover */
int SendAll( NetSocketRef sock, char *data, UInt32 length );


/*
 * Private globals
 */

static DmOpenRef gHttpLib = NULL;
static int gTimeout = 0;


/*
 * Name:   HTTPLibStart()
 * Args:   creator - creator ID to use for the database
 *         secTimeout - number of seconds to allow for server response
 * Return: 0 on success, -1 on error
 * Desc:   Creates the temp database used by the library.  I haven't registered
 *         a creator ID for the library itself, so the calling app should pass
 *         it's own creator ID and this function will create a database under
 *         that ID.
 */

int HTTPLibStart( UInt32 creator, int secTimeout )
{
    Err error;
    int recs;
    int i;

    gHttpLib = DmOpenDatabaseByTypeCreator( HTTPLIB_TYPE, creator,
                                            dmModeReadWrite ); 
    if ( !gHttpLib ) { 
        error = DmCreateDatabase( 0, HTTPLIB_NAME, creator, HTTPLIB_TYPE,
                                  false ); 
        if ( error ) {
            return -1;
        }

        gHttpLib = DmOpenDatabaseByTypeCreator( HTTPLIB_TYPE, creator,
                                                dmModeReadWrite ); 
        if ( !gHttpLib ) {
            return -2;
        }
    } else {
        recs = DmNumRecords( gHttpLib );
        for ( i = 0; i < recs; i++ ) {
            DmRemoveRecord( gHttpLib, 0 );
        }
    }

    gTimeout = secTimeout;

    return 0;
}


/*
 * Name:   HTTPLibStop()
 * Args:   none
 * Return: none
 * Desc:   Attempts to free up the temporary storage database records and
 *         shut the open database.
 */

void HTTPLibStop( void )
{
    int recs;
    int i;

    if ( gHttpLib != NULL ) {
        recs = DmNumRecords( gHttpLib );
        for ( i = 0; i < recs; i++ ) {
            DmRemoveRecord( gHttpLib, 0 );
        }
        DmCloseDatabase( gHttpLib );
        gHttpLib = NULL;
    }
}


/*
 * This was ripped from GNU GotMail, it's the method it uses for connecting a
 * socket instead of NetUTCPOpen().  The Palm docs say that NetUTCPOpen() is
 * not production quality code, whatever that means.
 */

#if 0

int connectsock(char *host, char *service)
{
    NetSocketRef sock;
    NetSocketAddrType laddr, raddr;
    NetIPAddr inetval;
    int one;
    short i1;
    NetSocketAddrINType saddr;
    NetHostInfoPtr phe;
    NetHostInfoBufType AppHostInfo;
    char tempbuf[64];
    Boolean nameresolved=false;
    UInt32 hosthash;

    MemSet((char *) &saddr, sizeof(saddr), 0);
    saddr.family = netSocketAddrINET;
    saddr.port = NetHToNS(StrAToI(service));

    if ((sock = NetLibSocketOpen(AppNetRefnum, netSocketAddrINET,
                    netSocketTypeStream, 0, AppNetTimeout, &errno)) < 0) {
        return -1;
    }

    nameresolved = false;

    if (!nameresolved ||
            ((saddr.addr = NetLibAddrAToIN(AppNetRefnum, host)) == -1)) {
        if ((phe = NetLibGetHostByName(AppNetRefnum, host, &AppHostInfo,
                        AppNetTimeout, &errno)) != 0) {
            /*
             * Sometimes the first address in the list is zero.  This
             * makes me have to search through them to find the first
             * non-zero address.
             */
            for (i1=0; i1 < netDNSMaxAddresses; i1++) {
                if (phe->addrListP[i1] != NULL) {
                    if (*phe->addrListP[i1] != '\0') {
                        MemMove((char *) &saddr.addr,
                                (char *) phe->addrListP[i1], phe->addrLen);
                        nameresolved = true;
                        break;
                    }
                }
            }
        }
        if (!nameresolved) {
            NetLibSocketClose(AppNetRefnum, sock, AppNetTimeout, &errno);
            return -1;
        }
    }

    if (NetLibSocketConnect(AppNetRefnum, sock, (NetSocketAddrType *) &saddr,
                sizeof(saddr), AppNetTimeout, &errno) != 0) {
        NetLibSocketClose(AppNetRefnum, sock, AppNetTimeout, &errno);
        return -1;
    }

    return sock;
}

#endif /* 0 */


/*
 * Name:   HTTPPost()
 * Args:   url - location to post data to
 *         data - text to send in the post body (currently must be a string)
 *         resultsDB - name of the stream DB to save the response into
 * Return: HTTPErr_OK on success, != HTTPErr_OK on all errors
 * Desc:   Sends the data passed in as the body of a POST request against the
 *         host/port/path specified in 'url'.  If the request is successfull,
 *         the response text is saved in a stream database with the name given
 *         in 'resultsDB'.
 */

HTTPErr HTTPPost( URLTarget *url, char *data, char *resultsDB )
{
    NetSocketRef sock;
    char contentLenStr[CLS_LENGTH];
    UInt32 length;
    Err err;
    Err err2;
    HTTPParse parse;
    ScratchRec scratch;
    char *headers;
    UInt8 allup;

    AppNetRefnum = 0;

    err = SysLibFind( "Net.lib", &AppNetRefnum );
    err = NetLibOpen( AppNetRefnum, &err2 );
    if ( (err && (err != netErrAlreadyOpen)) || err2 ) {
        NetLibClose( AppNetRefnum, true );
        return HTTPErr_ConnectError;
    }

    AppNetTimeout = SysTicksPerSecond() * gTimeout;

    NetLibConnectionRefresh( AppNetRefnum, true, &allup, &err2 );

    sock = NetUTCPOpen( url->host, NULL, url->port );
    // sock = connectsock( url->host, "8080" );
    if ( sock < 0 ) {
        err = NetLibClose( AppNetRefnum, true );
        return HTTPErr_ConnectError;
    }

    if ( StartScratchArea( &scratch ) != 0 ) {
        close( sock );
        err = NetLibClose( AppNetRefnum, true );
        return HTTPErr_TempDBErr;
    }

    AddScratchLine( &scratch, HTTP_POST_METH );
    AddScratchLine( &scratch, url->path );
    AddScratchLine( &scratch, HTTP_VERSION );
    AddScratchLine( &scratch, HTTP_LINE_ENDING );
    AddScratchLine( &scratch, HTTP_HOST_HDR );
    AddScratchLine( &scratch, url->host );
    AddScratchLine( &scratch, HTTP_LINE_ENDING );
    AddScratchLine( &scratch, HTTP_USERAGENT_LINE );
    AddScratchLine( &scratch, HTTP_CONTENTTYPE_LINE );
    AddScratchLine( &scratch, HTTP_CONTENTLENGTH_HDR );
    length = StrLen( data );
    StrPrintF( contentLenStr, "%ld", length );
    contentLenStr[CLS_LENGTH - 1] = '\0';
    AddScratchLine( &scratch, contentLenStr );
    AddScratchLine( &scratch, HTTP_LINE_ENDING );
    AddScratchLine( &scratch, HTTP_LINE_ENDING );
   
    if ( scratch.errFlag ) {
        close( sock );
        err = NetLibClose( AppNetRefnum, false );
        return HTTPErr_TempDBErr;
    }

    scratch.handle = DmQueryRecord( gHttpLib, scratch.index );
    headers = MemHandleLock( scratch.handle );
    SendAll( sock, headers, scratch.size );
    MemHandleUnlock( scratch.handle );

    SendAll( sock, data, length );

    MemSet( &parse, sizeof( parse ), 0 );
    parse.state = PS_ResponseLine;
    parse.saveFileName = resultsDB;

    ParseEngine( sock, &parse );

    close( sock );
    NetLibClose( AppNetRefnum, false );

    if ( parse.state != PS_Done ) {
        return HTTPErr_SizeMismatch;
    }

    return HTTPErr_OK;
}


/*
 * Name:   HTTPGet()
 * Args:   url - location to fetch from
 *         resultsDB - name of the stream DB to save the response into
 * Return: HTTPErr_OK on success, != HTTPErr_OK on all errors
 * Desc:   Attempts to read the data from the URL specified and writes the
 *         body into a stream database names 'resultsDB'.
 */

HTTPErr HTTPGet( URLTarget *url, char *resultsDB )
{
    NetSocketRef sock;
    Err err;
    Err err2;
    HTTPParse parse;
    ScratchRec scratch;
    char *headers;

    AppNetRefnum = 0;

    err = SysLibFind( "Net.lib", &AppNetRefnum );
    err = NetLibOpen( AppNetRefnum, &err2 );

    sock = NetUTCPOpen( url->host, NULL, url->port );
    if ( sock < 0 ) {
        err = NetLibClose( AppNetRefnum, false );
        return HTTPErr_ConnectError;
    }

    if ( StartScratchArea( &scratch ) != 0 ) {
        close( sock );
        err = NetLibClose( AppNetRefnum, false );
        return HTTPErr_TempDBErr;
    }

    AddScratchLine( &scratch, HTTP_GET_METH );
    AddScratchLine( &scratch, url->path );
    AddScratchLine( &scratch, HTTP_VERSION );
    AddScratchLine( &scratch, HTTP_LINE_ENDING );
    AddScratchLine( &scratch, HTTP_HOST_HDR );
    AddScratchLine( &scratch, url->host );
    AddScratchLine( &scratch, HTTP_LINE_ENDING );
    AddScratchLine( &scratch, HTTP_USERAGENT_LINE );
    AddScratchLine( &scratch, HTTP_LINE_ENDING );
   
    if ( scratch.errFlag ) {
        close( sock );
        err = NetLibClose( AppNetRefnum, false );
        return HTTPErr_TempDBErr;
    }

    scratch.handle = DmQueryRecord( gHttpLib, scratch.index );
    headers = MemHandleLock( scratch.handle );
    SendAll( sock, headers, scratch.size );
    MemHandleUnlock( scratch.handle );

    MemSet( &parse, sizeof( parse ), 0 );
    parse.state = PS_ResponseLine;
    parse.saveFileName = resultsDB;

    ParseEngine( sock, &parse );

    close( sock );
    NetLibClose( AppNetRefnum, false );

    if ( parse.state != PS_Done ) {
        return HTTPErr_SizeMismatch;
    }

    return HTTPErr_OK;
}


/*
 * Name:   StartScratchArea()
 * Args:   scratch - struct to use for tracking the status of the area
 * Return: 0 on success, -1 on error
 * Desc:   Creates a new record, or resizes to minimal size, the record with 
 *         index 0 from the temp database.  Initializes the other fields in 
 *         the 'scratch' struct so that the area starts out blank.
 */

static int StartScratchArea( ScratchRec *scratch )
{
    scratch->index = 0;

    if ( DmNumRecords( gHttpLib ) > 0 ) {
        scratch->handle = DmResizeRecord( gHttpLib, scratch->index, 1 );
        if ( scratch->handle == NULL ) {
            scratch->errFlag = 1;
            return -1;
        }
    } else {
        scratch->handle = DmNewRecord( gHttpLib, &(scratch->index), 1 );
        if ( scratch->handle == NULL ) {
            scratch->errFlag = 1;
            return -1;
        }
        DmReleaseRecord( gHttpLib, scratch->index, false );
    }

    scratch->size = 0;
    scratch->errFlag = 0;

    return 0;
}


/*
 * Name:   AddToScratch()
 * Args:   scratch - scratch area to add to
 *         text - pointer to the data to add
 *         length - number of bytes to add from the start of 'text'
 * Return: none
 * Desc:   Adds 'length' bytes to the end of the scratch record.  No status 
 *         is returned directly, on error a flag is set in the 'scratch' struct
 *         instead.  There's a cover, AddScratchLine(), which can be used to 
 *         add a null terminated character string without having to clutter up
 *         the calls with lots of StrLen() calls for the final arg.
 */

static void AddToScratch( ScratchRec *scratch, char *text, int length )
{
    char *scratchTxt;
    int newSize;

    if ( scratch->errFlag ) {
        return;
    }

    newSize = scratch->size + length;

    scratch->handle = DmResizeRecord( gHttpLib, scratch->index, newSize );
    if ( scratch->handle == NULL ) {
        scratch->errFlag = 1;
        return;
    }
    scratch->handle = DmGetRecord( gHttpLib, scratch->index );

    scratchTxt = MemHandleLock( scratch->handle );
    if ( scratchTxt == NULL ) {
        scratch->errFlag = 1;
        DmReleaseRecord( gHttpLib, scratch->index, false );
        return;
    }

    if ( DmWrite( scratchTxt, scratch->size, text, length ) != errNone ) {
        scratch->errFlag = 1;
        MemHandleUnlock( scratch->handle );
        DmReleaseRecord( gHttpLib, scratch->index, false );
        return;
    }

    MemHandleUnlock( scratch->handle );
    DmReleaseRecord( gHttpLib, scratch->index, true );

    scratch->size += length;
}


/*
 * Name:   SendAll()
 * Args:   sock - socket to send over
 *         data - the bytes to send
 *         length - the number of bytes to send
 * Return: 0 on success, -1 on error
 * Desc:   Sends exactly 'length' bytes to the socket 'sock'.  If unable to 
 *         send the full set of data, it is considered an error.
 */

int SendAll( NetSocketRef sock, char *data, UInt32 length )
{
    UInt32 written;
    Int16 thisWrite;

    written = 0;

    while ( written < length ) {
        thisWrite = send( sock, &(data[written]), length - written, 0 );
        if ( thisWrite <= 0 ) {
            return -1;
        }
        written += thisWrite;
    }

    return 0;
}



/*
 * Name:   BufSizeRemaining()
 * Args:   parse - struct to return info about
 * Return: Number of bytes free in the readBuffer of 'parse'
 * Desc:
 */

static UInt16 BufSizeRemaining( HTTPParse *parse )
{
    return( READ_BUF_SIZE - parse->bufferPos );
}


/*
 * Name:   BufConsumeToPointer()
 * Args:   parse - parse object to operate on
 *         newFirst - pointer to the new first byte within readBuffer
 * Return: none
 * Desc:   The 'newFirst' argument to this function should be a pointer to a
 *         location within the existing readBuffer for 'parse' (there should
 *         be an assert() to check to make sure that 'newFirst' lies within the
 *         buffer).  All the content in the readBuffer before 'newFirst' is
 *         deleted out of the buffer, and the internal state updated to be
 *         consistent.
 */

static void BufConsumeToPointer( HTTPParse *parse, char *newFirst )
{
    int newLength;

    newLength = NextBufByte( parse ) - newFirst;
    MemMove( parse->readBuffer, newFirst, newLength );
    parse->bufferPos = newLength;
}


/*
 * Name:   NextBufByte()
 * Args:   parse - structure to calculate from
 * Return: pointer to the next empty byte in the read buffer
 * Desc:   Just a convenient wrapper.
 */

static char *NextBufByte( HTTPParse *parse )
{
    return( parse->readBuffer + parse->bufferPos );
}


/*
 * Name:   FillReadBuff()
 * Args:   sock - socket to read from
 *         parse - the structure to write the data into
 * Return: number of bytes read on success, -1 on error
 * Desc:   Attempts to read data from the socket 'sock' and write into the
 *         read buffer associated with 'parse'.  This function should only be
 *         called if data is needed (the parse can't succeed without having 
 *         more data).  If any data at all is read, the needData flag from 
 *         'parse' is cleared.  It's up to the parse functions to determine if
 *         the new data is enough to proceed, and if not to reset the flag and
 *         call this function again.
 */

static int FillReadBuff( NetSocketRef sock, HTTPParse *parse )
{
    int readRes;

    if ( BufSizeRemaining( parse ) == 0 ) {
        return -1;
    }

    readRes = recv( sock, NextBufByte( parse ), BufSizeRemaining( parse ), 0 );
    if ( readRes < 0 ) {
        return -1;
    }

    if ( readRes == 0 ) {
        parse->endOfStream = 1;
        parse->needData = 0;
    } else {
        parse->bufferPos += readRes;
        parse->needData = 0;
    }

    return readRes;
}


/*
 * Name:   MarkEOL()
 * Args:   parse - buffer to look in
 * Return: 0 if a full line was found, -1 otherwise
 * Desc:   Attempts to find a full line (terminated by a caridge 
 *         return/linefeed pair) at the start of the readBuffer associated with
 *         'parse'.  If found, a null character is overwritten at the first
 *         line ending character so that the line can be treated as a string.
 *         This means that the function can only be called once for each line.
 */

static int MarkEOL( HTTPParse *parse )
{
    int i;
    char *endOfLine;

    i = 0;
    endOfLine = NULL;
    while ( (i < parse->bufferPos) && (endOfLine == NULL) ) {
        if ( parse->readBuffer[i] == '\r' ) {
            endOfLine = parse->readBuffer + i;
        }
        i++;
    }

    if ( (endOfLine == NULL) || (endOfLine == (NextBufByte( parse ) - 1)) ) {
        if ( parse->endOfStream ) {
            parse->state = PS_Error;
            return -1;
        }
        parse->needData = 1;
        return -1;
    }

    if ( endOfLine[1] != '\n' ) {
        parse->state = PS_Error;
        return -1;
    }

    *endOfLine = '\0';
    return 0;
}


/*
 * Name:   ParseResponseCode()
 * Args:   start - pointer to the start of the string to search in
 * Return: Pointer to first char in the response code on success, NULL on error
 * Desc:   Attempts to find a whitespace delimited word starting at or after
 *         the location pointed to by 'start'.  The value returned is the 
 *         location of the first non-whitespace character in the response code.
 */

static char *ParseResponseCode( char *start )
{
    char *beginField;
    char *endField;

    beginField = start;
    while ( TxtCharIsSpace( *beginField ) ) {
        beginField++;
    }

    if ( *beginField == '\0' ) {
        return( NULL );
    }

    endField = beginField + 1;
    while ( !TxtCharIsSpace( *endField ) && (*endField != '\0') ) {
        endField++;
    }

    if ( *endField == '\0' ) {
        return( NULL );
    }

    return( beginField );
}


/*
 * Name:   ParseEngine()
 * Args:   sock - socket to read from
 *         parse - struct to use to store the parse state
 * Return: none
 * Desc:   I've tried to structure this as a relatively generic parse loop,
 *         hoping that it'll somewhat match what's needed for an event driven
 *         version.  The only place that a read is done is right here, the
 *         parse functions just take care of handling the data that's already
 *         been deposited in the read buffer.
 */

static void ParseEngine( NetSocketRef sock, HTTPParse *parse )
{
    while ( (parse->state != PS_Error) && (parse->state != PS_Done) ) {
        if ( parse->needData == 1 ) {
            if ( FillReadBuff( sock, parse ) < 0 ) {
                parse->state = PS_Error;
                break;
            }
        }

        switch ( parse->state ) {
            case PS_ResponseLine:
                ParseResponseLine( parse );
                break;

            case PS_Headers:
                ParseHeaders( parse );
                break;

            case PS_Body:
                ParseBody( parse );
                break;

            default:
                break;
        }
    }
}


/*
 * Name:   ParseResponseLine()
 * Args:   parse - structure to use to track parse status
 * Return: none
 * Desc:   Attempts to pull the information from the initial response line of
 *         at HTTP response.  If the line seems to be malformed an error is
 *         flagged in the 'parse' struct and the parse loop takes care of
 *         exiting.  On success the response code is stored and the parse state
 *         is updated to process the headers.  The parse engine takes care of
 *         calling the next stage, we just set the state and return to the
 *         caller.
 */

static void ParseResponseLine( HTTPParse *parse )
{
    char *newFirstByte;
    char *pastVersion;
    char *responseVal;

    if ( MarkEOL( parse ) == -1 ) {
        return;
    }
    newFirstByte = parse->readBuffer + StrLen( parse->readBuffer ) + 2;

    pastVersion = parse->readBuffer;
    while ( !TxtCharIsSpace( *pastVersion ) && (*pastVersion != '\0') ) {
        pastVersion++;
    }

    if ( *pastVersion == '\0' ) {
        parse->state = PS_Error;
        return;
    }

    responseVal = ParseResponseCode( pastVersion );
    if ( responseVal == NULL ) {
        parse->state = PS_Error;
        return;
    }

    parse->responseCode = StrAToI( responseVal );
    BufConsumeToPointer( parse, newFirstByte );

    parse->state = PS_Headers;
}


/*
 * Name:   ParseHeaders()
 * Args:   parse - struct to use to track the parse state
 * Return: none
 * Desc:   Reads in HTTP headers from the socket associated with 'parse' until
 *         it finds an empty line.  If there's a content length line it gets
 *         parsed and the target byte count stored for later.  Once we see the
 *         terminating empty line the state is updated to process the body.  
 *         Before updating the state and allowing the next stage to progress we
 *         open up a stream database to write the response body into.
 */

static void ParseHeaders( HTTPParse *parse )
{
    char *newFirstByte;
    char *value;
    Int32 tmpLen;

    if ( MarkEOL( parse ) == -1 ) {
        return;
    }
    newFirstByte = parse->readBuffer + StrLen( parse->readBuffer ) + 2;

    if ( StrLen( parse->readBuffer ) == 0 ) {
        BufConsumeToPointer( parse, newFirstByte );

        parse->fd = FileOpen( 0, parse->saveFileName, 'DATA', 'BRWS',
                              fileModeReadWrite, NULL );
        if ( parse->fd == NULL ) {
            parse->state = PS_Error;
            return;
        }

        parse->state = PS_Body;
        return;
    }

    if ( StrNCompare( parse->readBuffer, HTTP_CONTENTLENGTH_HDR, 
                      StrLen( HTTP_CONTENTLENGTH_HDR ) ) == 0 ) {
        value = parse->readBuffer + StrLen( HTTP_CONTENTLENGTH_HDR );
        tmpLen = StrAToI( value );
        if ( tmpLen <= 0 ) {
            parse->state = PS_Error;
            return;
        }
        parse->contentLength = (UInt32)tmpLen;
    }

    BufConsumeToPointer( parse, newFirstByte );
}


/*
 * Name:   ParseBody()
 * Args:   parse - struct to use to track the parse
 * Return: none
 * Desc:   There are two different techniques to use while reading the body.
 *         If we know the content length from one of the headers in the
 *         response we have a very exact target to hit.  If there was no length
 *         header we read till we hit end of file on the stream.  The data is
 *         written into a stream database as we grab it.
 */

static void ParseBody( HTTPParse *parse )
{
    int bytesNeeded;
    int byteCount;

    if ( parse->contentLength != 0 ) {
        bytesNeeded = parse->contentLength - parse->contentRead;
        if ( bytesNeeded == 0 ) {
            parse->state = PS_Done;
            FileClose( parse->fd );
            return;
        }

        if ( parse->bufferPos == 0 ) {
            parse->needData = 1;
            return;
        }

        if ( parse->bufferPos > bytesNeeded ) {
            byteCount = bytesNeeded;
        } else {
            byteCount = parse->bufferPos;
        }
    } else {
        if ( parse->bufferPos == 0 ) {
            if ( parse->endOfStream != 0 ) {
                parse->state = PS_Done;
                FileClose( parse->fd );
                return;
            }
            parse->needData = 1;
            return;
        }

        byteCount = parse->bufferPos;
    }

    FileWrite( parse->fd, parse->readBuffer, 1, byteCount, NULL );
    parse->contentRead += byteCount;
    BufConsumeToPointer( parse, &(parse->readBuffer[byteCount]) );
}


