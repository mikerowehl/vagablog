/* arch-tag: main implementation file for vagablog
 *
 * Vagablog - Palm based Blog utility
 *  
 * Copyright (C) 2003,2004,2005 Mike Rowehl <miker@bitsplitter.net>
 *
 * Currently only simple posting is supported, with a few options.  It should
 * do a whole lot more.
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
#include <DLServer.h>
#include <Unix/sys_socket.h>
#include "rsrc/resource.h"

#include "http.h"


#define VAGABLOG_ID "77783D0E1EE8808BD4D3327D1EB2601C78DB3D65"

const unsigned long gCreator = 'VBlg';
const unsigned long gDBType = 'Data';
const char gDBName[] = "VagablogDB";
const unsigned long gPrefDBType = 'Pref';
const char gPrefDBName[] = "VagablogPrefDB";
const unsigned long gAppPrefID = 0;
const unsigned long gAppPrefVersion = 6;

const char gDefaultHost[] = "www.blogger.com";
const char gDefaultPort[] = "80";
const char gDefaultURL[] = "/api/RPC2";
const char gDefaultTimeout[] = "60";


/* Used to store the text returned by the XMLRPC server */
const char gTempDBName[] = "VBLGTempDATA";


#define FIELD_LEN (64)
#define NAME_LEN FIELD_LEN
#define PASS_LEN FIELD_LEN
#define HOST_LEN FIELD_LEN
#define URL_LEN (128)
#define PORT_LEN (6)
#define TIMEOUT_LEN (6)
#define BLOG_ID_LEN (12)
#define REGCODE_LEN FIELD_LEN
#define BLOG_NAME_LEN (50)
#define INFOREQ_SIZE (17000)

#define NUM_UNREGPOSTS (5)
#define MAX_BLOGS (10)


/*
 * Represents the information about a single blog.  Saving the string unpadded
 * would be more space efficient, but would only save less than 1k, at the
 * very maximum.  And would probably cause more than that in code size
 * increase, just not worth it.
 */

typedef struct BlogEntry_struct {
    char name[BLOG_NAME_LEN];
    char id[BLOG_ID_LEN];
} BlogEntry;


/*
 * The structure stored as the application preferences.  'posts' is the number
 * of posts that the user has performed, if the application is unregistered.
 * When the app does get registered the counter should be reset to 0.  This way
 * if the user does something to invalidate the reg code they have a grace
 * period during which they can get support to figure out what happened, but
 * they can still do some posting.
 */

#define PA_NOTHING (0)
#define PA_CLEAR_TEXT (1)

#define PUB_IMMED (0)
#define PUB_LATER (1)

#define BT_BLOGGER (0)
#define BT_JOURNALSPACE (1)
#define BT_WORDPRESS (2)
#define BT_MOVABLETYPE (3)
#define BT_TYPEPAD (4)
#define BT_B2 (5)
#define BT_LIVEJOURNAL (6)

typedef struct VagablogPrefs_struct {
    char name[NAME_LEN];
    char pass[PASS_LEN];
    char blogID[BLOG_ID_LEN];
    char host[HOST_LEN];
    char port[PORT_LEN];
    char url[URL_LEN];
    char reg[REGCODE_LEN];
    int posts;
    int postAction;
    char timeout[TIMEOUT_LEN];
    int blogType;
    int publishFlag;
} VagablogPrefs;

static VagablogPrefs gPrefs;


typedef struct VagablogPrefs_v1_struct {
    char name[NAME_LEN];
    char pass[PASS_LEN];
    char blogID[BLOG_ID_LEN];
    char host[HOST_LEN];
    char port[PORT_LEN];
    char url[URL_LEN];
    char reg[REGCODE_LEN];
    int posts;
} VagablogPrefs_v1;

typedef struct VagablogPrefs_v2_struct {
    char name[NAME_LEN];
    char pass[PASS_LEN];
    char blogID[BLOG_ID_LEN];
    char host[HOST_LEN];
    char port[PORT_LEN];
    char url[URL_LEN];
    char reg[REGCODE_LEN];
    int posts;
    int postAction;
    int blogEntries;
    BlogEntry blogs[MAX_BLOGS];
} VagablogPrefs_v2;

typedef struct VagablogPrefs_v3_struct {
    char name[NAME_LEN];
    char pass[PASS_LEN];
    char blogID[BLOG_ID_LEN];
    char host[HOST_LEN];
    char port[PORT_LEN];
    char url[URL_LEN];
    char reg[REGCODE_LEN];
    int posts;
    int postAction;
    int blogEntries;
    BlogEntry blogs[MAX_BLOGS];
    char timeout[TIMEOUT_LEN];
} VagablogPrefs_v3;

typedef struct VagablogPrefs_v4_struct {
    char name[NAME_LEN];
    char pass[PASS_LEN];
    char blogID[BLOG_ID_LEN];
    char host[HOST_LEN];
    char port[PORT_LEN];
    char url[URL_LEN];
    char reg[REGCODE_LEN];
    int posts;
    int postAction;
    int blogEntries;
    BlogEntry blogs[MAX_BLOGS];
    char timeout[TIMEOUT_LEN];
    int blogType;
} VagablogPrefs_v4;

typedef struct VagablogPrefs_v5_struct {
    char name[NAME_LEN];
    char pass[PASS_LEN];
    char blogID[BLOG_ID_LEN];
    char host[HOST_LEN];
    char port[PORT_LEN];
    char url[URL_LEN];
    char reg[REGCODE_LEN];
    int posts;
    int postAction;
    int blogEntries;
    BlogEntry blogs[MAX_BLOGS];
    char timeout[TIMEOUT_LEN];
    int blogType;
    int publishFlag;
} VagablogPrefs_v5;


/*
 * The databases, the normal DB is the posts database, the PrefDB holds info
 * about the blog list.
 */

static DmOpenRef gDBRef = NULL;
static DmOpenRef gPrefDBRef = NULL;


/*
 * Used to pass info into and out of the create link popup
 */

static char *gLinkText = NULL;
static char *gLinkURL = NULL;


/*
 * Used to store info about a fault returned by the XMLRPC server.  We put the
 * XMLRPC server message up in the alert box to try to give the user a bit of
 * a clue.
 */

#define FAULT_STR_LEN (256)

typedef struct FaultInfo_struct {
    Int32 code;
    char string[FAULT_STR_LEN];
} FaultInfo;


/*
 * XMLRPC call templates
 */

static char *gUsersBlogsReq = \
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n" \
    "<methodCall>\n" \
    "  <methodName>blogger.getUsersBlogs</methodName>\n" \
    "  <params>\n" \
    "    <param>\n" \
    "      <value><string>" VAGABLOG_ID "</string></value>\n" \
    "    </param>\n" \
    "    <param>\n" \
    "      <value><string>%s</string></value>\n" \
    "    </param>\n" \
    "    <param>\n" \
    "      <value><string>%s</string></value>\n" \
    "    </param>\n" \
    "  </params>\n" \
    "</methodCall>";

static char *gNewPostReq = \
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n" \
    "<methodCall>\n" \
    "  <methodName>blogger.newPost</methodName>\n" \
    "  <params>\n" \
    "    <param>\n" \
    "      <value><string>" VAGABLOG_ID "</string></value>\n" \
    "    </param>\n" \
    "    <param>\n" \
    "      <value><string>%s</string></value>\n" \
    "    </param>\n" \
    "    <param>\n" \
    "      <value><string>%s</string></value>\n" \
    "    </param>\n" \
    "    <param>\n" \
    "      <value><string>%s</string></value>\n" \
    "    </param>\n" \
    "    <param>\n" \
    "      <value><string>%s%s%s</string></value>\n" \
    "    </param>\n" \
    "    <param>\n" \
    "      <value><boolean>%d</boolean></value>\n" \
    "    </param>\n" \
    "  </params>\n" \
    "</methodCall>\n";


static const char *gXMLDeclStart = "<?xml";
static const char *gXMLDeclEnd  = "?>";

static const char *gXMLRPCParamsStart = "<params>";
static const char *gXMLRPCParamsEnd = "</params>";

static const char *gXMLRPCParamStart = "<param>";
static const char *gXMLRPCParamEnd = "</param>";

static const char *gXMLRPCFaultStart = "<fault>";
static const char *gXMLRPCFaultEnd = "</fault>";

static const char *gXMLRPCStructStart = "<struct>";
static const char *gXMLRPCStructEnd = "</struct>";

static const char *gXMLRPCValueStart = "<value>";
static const char *gXMLRPCValueEnd = "</value>";

static const char *gXMLRPCNameStart = "<name>";
static const char *gXMLRPCNameEnd = "</name>";

static const char *gXMLRPCMemberStart = "<member>";
static const char *gXMLRPCMemberEnd = "</member>";

static const char *gXMLRPCIntStart = "<int>";
static const char *gXMLRPCIntEnd = "</int>";

static const char *gXMLRPCI4Start = "<i4>";
static const char *gXMLRPCI4End = "</i4>";

static const char *gXMLRPCStringStart = "<string>";
static const char *gXMLRPCStringEnd = "</string>";

static const char *gXMLRPCFaultName = "faultString";

static const char *gTitleBegin = "&lt;title&gt;";
static const char *gTitleEnd = "&lt;/title&gt;";

static const char *gCatBegin = "&lt;category&gt;";
static const char *gCatEnd = "&lt;/category&gt;";


/*
 * Hacks because of Palm API features that encourage poor encapsulation.. maybe
 * using C++ would fix some of the user visible aspects of this, but it 
 * definitely exposes a whole other class of issues.
 */

char **gBlogListNames;
char **gBlogListIDs;
Int16 gBlogListCount;
Int16 gBlogListReloaded;


/*
 */

int gSvrFormSaved = 0;


/*
 * Local prototypes
 */

/* General helpers */
static void *GetObjectPtr( FormPtr formP, Int16 resourceNo );
static void *GetCurrFormObjPtr( Int16 resourceNo );
static void SetTextField( FieldPtr field, const char *text );
static Boolean FormHasCategory( void );
static Boolean FormHasTitle( void );
static UInt16 FormForType( int type );
static void AddMarkup( Int16 res, const char *single, const char *front,
                       const char *back );

/* XMLRPC helpers */
static char *EscapeString( char *string );
static Boolean IsXMLWhitespace( char ch );
static char *SkipWhitespaceMatch( const char *source, const char *match );

/* Basic XMLRPC and HTTP interfacing */
static void ParseFaultInfo( char *faultTag, FaultInfo *fault );
static int ParseXMLRPCDecl( char *start, char **end );
static int ParseXMLRPCResponse( char *response, FaultInfo *fault );
static Boolean ReadWholeFile( const char *file, char *buffer, UInt32 length );
static int PostFormData( void );

/* Init and cleanup */
static void StartApp( void );
static void EndApp( void );

/* Blog list handling */
static void FreeBlogListInfo( int count );

/* Event handling */
static void PostFormUpdateScrollbar( void );
static void PostFormScroll( Int16 lines );
static void PostFormPageScroll( WinDirectionType direction );
static Boolean PostActionFormHandler( EventType *event );
static Boolean MainFormHandler( EventType *event );
static Boolean AboutFormHandler( EventType *event );
static Boolean IdentityFormHandler( EventType *event );
static Boolean CreateLinkFormHandler( EventType *event );
static Boolean BlogLoadFormHandler( EventType *event );
static Boolean BlogListFormHandler( EventType *event );
static Boolean ServerFormHandler( EventType *event );
static Boolean ApplicationHandleEvent( EventType *event );


/*
 *
 */

static void *GetObjectPtr( FormPtr formP, Int16 resourceNo )
{
    UInt16 objIndex = FrmGetObjectIndex( formP, resourceNo );

    if ( objIndex == frmInvalidObjectId ) {
        return NULL;
    }

    return FrmGetObjectPtr( formP, objIndex );
}


/*
 *
 */

static void *GetCurrFormObjPtr( Int16 resourceNo )
{
    return GetObjectPtr( FrmGetActiveForm(), resourceNo );
}


/*
 * Name:   SetTextField()
 * Input:  field - pointer to the field to set
 *         text - the text to put in the field
 * Output: none
 * Desc:   There seems to be no function within the standard API to do this,
 *         set a field to a value from a text string.  There are some functions
 *         in there that do some odd stuff, so maybe I'm just not seeing the
 *         one that does this.
 */

static void SetTextField( FieldPtr field, const char *text )
{
    MemHandle fieldHandle;
    char *fieldText;

    fieldHandle = FldGetTextHandle( field );
    if ( fieldHandle == NULL ) {
        fieldHandle = MemHandleNew( StrLen( text ) + 1 );
        fieldText = MemHandleLock( fieldHandle );
        StrCopy( fieldText, text );
        MemHandleUnlock( fieldHandle );
        FldSetTextHandle( field, fieldHandle );
    } else {
        FldSetTextHandle( field, NULL );
        if ( MemHandleResize( fieldHandle, StrLen( text ) + 1 ) == 0 ) {
            fieldText = MemHandleLock( fieldHandle );
            StrCopy( fieldText, text );
            MemHandleUnlock( fieldHandle );
            FldSetTextHandle( field, fieldHandle );
        } else {
            FldSetTextHandle( field, fieldHandle );
        }
    }
}


/*
 * Name:   EscapeString()
 * Input:  string - text to apply escaping to
 * Output: a newly allocated string on success, NULL on failure
 * Desc:   Adds in special character sequences in the place of characters which
 *         can cause problems for XML parsing.  The return value is heap
 *         allocated and must be freed by the caller after a successfull 
 *         return.
 */

static char *EscapeString( char *string )
{
    UInt16 unescapedLen;
    UInt16 escapedLen;
    UInt16 index;
    UInt16 escapedIndex;
    char *newString;
    char convStr[maxStrIToALen];

    unescapedLen = StrLen( string );
    escapedLen = 0;

    for ( index = 0; index < unescapedLen; index++ ) {
        if ( (unsigned char)string[index] < (unsigned char)127 ) {
            switch ( string[index] ) {
                case '&':
                    escapedLen += 5;
                    break;

                case '\'':
                    escapedLen += 6;
                    break;

                case '<':
                    escapedLen += 4;
                    break;

                case '>':
                    escapedLen += 4;
                    break;

                case '"':
                    escapedLen += 6;
                    break;

                default:
                    escapedLen++;
                    break;
            }
        } else {
            escapedLen += 6;
        }
    }

    newString = (char *)MemPtrNew( escapedLen + 1 ); /* NULL terminator */
    if ( newString == NULL ) {
        return NULL;
    }

    escapedIndex = 0; 
    for ( index = 0; index < unescapedLen; index++ ) {
        if ( (unsigned char)string[index] < (unsigned char)127 ) {
            switch ( string[index] ) {
                case '&':
                    newString[escapedIndex++] = '&';
                    newString[escapedIndex++] = 'a';
                    newString[escapedIndex++] = 'm';
                    newString[escapedIndex++] = 'p';
                    newString[escapedIndex++] = ';';
                    break;

                case '\'':
                    newString[escapedIndex++] = '&';
                    newString[escapedIndex++] = 'a';
                    newString[escapedIndex++] = 'p';
                    newString[escapedIndex++] = 'o';
                    newString[escapedIndex++] = 's';
                    newString[escapedIndex++] = ';';
                    break;

                case '<':
                    newString[escapedIndex++] = '&';
                    newString[escapedIndex++] = 'l';
                    newString[escapedIndex++] = 't';
                    newString[escapedIndex++] = ';';
                    break;

                case '>':
                    newString[escapedIndex++] = '&';
                    newString[escapedIndex++] = 'g';
                    newString[escapedIndex++] = 't';
                    newString[escapedIndex++] = ';';
                    break;

                case '"':
                    newString[escapedIndex++] = '&';
                    newString[escapedIndex++] = 'q';
                    newString[escapedIndex++] = 'u';
                    newString[escapedIndex++] = 'o';
                    newString[escapedIndex++] = 't';
                    newString[escapedIndex++] = ';';
                    break;

                default:
                    newString[escapedIndex++] = string[index];
                    break;
            }
        } else {
            StrIToA( convStr, (unsigned char)string[index] );
            newString[escapedIndex++] = '&';
            newString[escapedIndex++] = '#';
            newString[escapedIndex++] = convStr[0];
            newString[escapedIndex++] = convStr[1];
            newString[escapedIndex++] = convStr[2];
            newString[escapedIndex++] = ';';
        }
    }
    newString[escapedIndex++] = '\0';

    return newString;
}


/*
 * Desc:   Returns true if the character given is a whitespace character.
 */

static Boolean IsXMLWhitespace( char ch )
{
    switch ( ch ) {
        case '\r':
        case '\n':
        case ' ':
        case '\t':
            return true;
            break;

        default:
            return false;
            break;
    }
}


/*
   Skip any leading whitespace and attempt to find the end of 'matchString'
   if we hit non-whitespace that doesn't match the given input, return null.
   Otherwise return a pointer to the next byte after the matching string.
 */

static char *SkipWhitespaceMatch( const char *source, const char *match )
{
    char *search;
    Int32 length;

    search = (char *)source;

    while ( IsXMLWhitespace( *search ) ) {
        search++;
    }

    length = StrLen( match );
    if ( StrNCaselessCompare( search, match, length ) == 0 ) {
        return search + length;
    }

    return NULL;
}


/*
 */

static void CopyUpTo( const char *start, const char *marker, FaultInfo *fault )
{
    int strIndx;

    strIndx = 0;

    while ( StrNCompare( start + strIndx, marker, StrLen( marker ) ) != 0 ) {
        fault->string[strIndx] = start[strIndx];
        if ( start[strIndx] == '\0' ) {
            return;
        }
        strIndx++;
        if ( strIndx == (FAULT_STR_LEN - 1) ) {
            fault->string[strIndx] = '\0';
        }
    }

    fault->string[strIndx] = '\0';
}


/*
 */

static void FindNextValue( char *begin, FaultInfo *fault )
{
    char *endName;
    char *afterValue;
    char *afterString;
    char *testLessThan;

    endName = StrStr( begin, gXMLRPCNameEnd );
    if ( endName == NULL ) {
        StrCopy( fault->string, "Unable to find fault string" );
        return;
    }
    endName += StrLen( gXMLRPCNameEnd );

    afterValue = SkipWhitespaceMatch( endName, gXMLRPCValueStart );
    if ( afterValue == NULL ) {
        StrCopy( fault->string, "Unable to find fault string" );
        return;
    }

    afterString = SkipWhitespaceMatch( afterValue, gXMLRPCStringStart );
    if ( afterString == NULL ) {
        /* The <string> tag might just have been left off */
        testLessThan = SkipWhitespaceMatch( afterValue, "<" );
        if ( testLessThan != NULL ) {
            StrCopy( fault->string, "Unable to find fault string" );
            return;
        }
        CopyUpTo( afterValue, gXMLRPCValueEnd, fault );
    } else {
        CopyUpTo( afterString, gXMLRPCStringEnd, fault );
    }
}


/*
 */

static void ParseFaultInfo( char *faultTag, FaultInfo *fault )
{
    char *afterValue;
    char *afterStruct;
    char *afterMember;
    char *beginElem;
    char *beginName;

    afterValue = SkipWhitespaceMatch( faultTag, gXMLRPCValueStart );
    if ( afterValue == NULL ) {
        StrCopy( fault->string, "Unable to find fault string" );
        return;
    }
    afterStruct = SkipWhitespaceMatch( afterValue, gXMLRPCStructStart );
    if ( afterStruct == NULL ) {
        StrCopy( fault->string, "Unable to find fault string" );
        return;
    }

    beginElem = afterStruct;

    while ( (afterMember = SkipWhitespaceMatch( beginElem, 
                                                gXMLRPCMemberStart ))
            != NULL ) {
        beginName = SkipWhitespaceMatch( afterMember, gXMLRPCNameStart );
        if ( beginName == NULL ) {
            beginElem = StrStr( beginElem, gXMLRPCMemberEnd );
            if ( beginElem == NULL ) {
                StrCopy( fault->string, "Unable to find fault string" );
                return;
            }
            beginElem += StrLen( gXMLRPCMemberEnd );
        } else {
            if ( StrNCompare( beginName, gXMLRPCFaultName, 
                              StrLen( gXMLRPCFaultName ) ) == 0 ) {
                FindNextValue( beginName, fault );
                return;
            } else {
                beginElem = StrStr( beginElem, gXMLRPCMemberEnd );
                if ( beginElem == NULL ) {
                    StrCopy( fault->string, "Unable to find fault string" );
                    return;
                }
                beginElem += StrLen( gXMLRPCMemberEnd );
            }
        }
    }
}


/*
 */

static int ParseXMLRPCDecl( char *start, char **end )
{
    char *afterDecl;
    char *endDecl;

    if ( StrNCaselessCompare( start, gXMLDeclStart, StrLen( gXMLDeclStart ) )
            != 0 ) {
        return -1;
    }

    afterDecl = start + StrLen( gXMLDeclStart );
    endDecl = StrStr( afterDecl, gXMLDeclEnd );
    if ( endDecl == NULL ) {
        return -1;
    }

    *end = endDecl + StrLen( gXMLDeclEnd );
    return 0;
}


/*
   Returns 0 if there was no fault, negative if there was.  If there was a
   fault the info is coppied into the fault struct (fault code and string)
 */

static int ParseXMLRPCResponse( char *response, FaultInfo *fault )
{
    char *methodResponse;
    char *afterResponse;
    char *params;
    char *faultTag;

    if ( ParseXMLRPCDecl( response, &methodResponse ) != 0 ) {
        fault->code = 0;
        StrCopy( fault->string, "Unable to find XML declaration" );
        return -1;
    }

    afterResponse = SkipWhitespaceMatch( methodResponse, "<methodResponse>" );
    if ( afterResponse == NULL ) {
        fault->code = 0;
        StrCopy( fault->string, "Missing method response tag" );
        return -1;
    }

    params = SkipWhitespaceMatch( afterResponse, "<params>" );
    if ( params != NULL ) {
        return 0;
    }

    faultTag = SkipWhitespaceMatch( afterResponse, gXMLRPCFaultStart );
    if ( faultTag != NULL ) {
        ParseFaultInfo( faultTag, fault );
        return -1;
    }

    fault->code = 0;
    StrCopy( fault->string, "Unexpected tag after method response" );

    return -1;
}


/*
 */

static Boolean ReadWholeFile( const char *file, char *buffer, UInt32 length )
{
    void *fd;
    UInt32 filledBytes;
    Int32 thisRead;

    fd = FileOpen( 0, file, 'DATA', 'BRWS', fileModeReadOnly, NULL );
    if ( fd == NULL ) {
        return false;
    }

    filledBytes = 0;
    while ( (filledBytes < (length - 1)) &&
            ((thisRead = FileRead( fd, buffer + filledBytes, 1, 
                           (length - 1) - filledBytes, NULL )) != 0 ) ) {
        filledBytes += thisRead;
    }

    buffer[filledBytes] = '\0';

    if ( !FileEOF( fd ) ) {
        FileClose( fd );
        return false;
    }

    FileClose( fd );
    return true;
}


/*
 */

static void ConditionalUnlockHandle( MemHandle handle )
{
    if ( handle != NULL ) {
        MemHandleUnlock( handle );
    }
}


/*
 */

static char *StrDup( char *string )
{
    int strSize;
    char *returnStr;

    strSize = StrLen( string ) + 1;
    returnStr = MemPtrNew( strSize );
    if ( returnStr == NULL ) {
        return NULL;
    }

    StrCopy( returnStr, string );
    return returnStr;
}


/*
 */

static char *TaggedString( const char *beginTag, char *content, 
                           const char *endTag )
{
    int strSize;
    char *returnStr;

    strSize = StrLen( beginTag ) + StrLen( content ) + StrLen( endTag ) + 1;
    returnStr = MemPtrNew( strSize );

    StrCopy( returnStr, beginTag );
    StrCat( returnStr, content );
    StrCat( returnStr, endTag );
    return returnStr;
}


/*
 */

static int PostFormData( void )
{
    FormPtr form;
    FieldPtr postField;
    MemHandle postHandle;
    char *postFldText;
    FieldPtr titleField;
    MemHandle titleHandle;
    char *titleFldText;
    FieldPtr catField;
    MemHandle catHandle;
    char *catFldText;
    FormPtr statusForm;
    FieldPtr statusField;
    char *postText;
    URLTarget target;
    int retValue;
    char *escaped;
    char *escapedCat;
    char *escapedTitle;
    char *escapedName;
    char *escapedPass;
    char *catEntry;
    char *titleEntry;
    FaultInfo fault;
    Int16 postres;
    int publishFld;

    form = FrmGetFormPtr( FormForType( gPrefs.blogType ) );
    postField = GetObjectPtr( form, BlogEntryFld );
    postHandle = FldGetTextHandle( postField );
    if ( postHandle == NULL ) {
        return -1;
    }
    postFldText = MemHandleLock( postHandle );

    titleField = GetObjectPtr( form, BlogTitleFld );
    if ( titleField == NULL ) {
        titleHandle = NULL;
        titleFldText = "";
    } else {
        titleHandle = FldGetTextHandle( titleField );
        if ( titleHandle == NULL ) {
            titleFldText = "";
        } else {
            titleFldText = MemHandleLock( titleHandle );
        }
    }

    if ( gPrefs.blogType == BT_LIVEJOURNAL ) {
        catField = NULL;
    } else {
        catField = GetObjectPtr( form, BlogCategoryFld );
    }
    if ( catField == NULL ) {
        catHandle = NULL;
        catFldText = "";
    } else {
        catHandle = FldGetTextHandle( catField );
        if ( catHandle == NULL ) {
            catFldText = "";
        } else {
            catFldText = MemHandleLock( catHandle );
        }
    }

    statusForm = FrmGetActiveForm();
    statusField = (FieldPtr)GetObjectPtr( statusForm, PostActionStatus );

    SetTextField( statusField, "Formatting request" );
    FldDrawField( statusField );

    escaped = EscapeString( postFldText );
    if ( escaped == NULL ) {
        MemHandleUnlock( postHandle );
        ConditionalUnlockHandle( titleHandle );
        ConditionalUnlockHandle( catHandle );
        return -2;
    }
    MemHandleUnlock( postHandle );

    if ( titleHandle == NULL ) {
        titleEntry = StrDup( "" );
    } else {
        if ( StrLen( titleFldText ) == 0 ) {
            titleEntry = StrDup( "" );
        } else {
            escapedTitle = EscapeString( titleFldText );
            if ( escapedTitle == NULL ) {
                titleEntry = NULL;
            } else {
                titleEntry = TaggedString( gTitleBegin, escapedTitle,
                                           gTitleEnd );
                MemPtrFree( escapedTitle );
            }
        }
        MemHandleUnlock( titleHandle );
    }

    if ( titleEntry == NULL ) {
        MemPtrFree( escaped );
        ConditionalUnlockHandle( catHandle );
    }

    if ( catHandle == NULL ) {
        catEntry = StrDup( "" );
    } else {
        if ( StrLen( catFldText ) == 0 ) {
            catEntry = StrDup( "" );
        } else {
            escapedCat = EscapeString( catFldText );
            if ( escapedCat == NULL ) {
                catEntry = NULL;
            } else {
                catEntry = TaggedString( gCatBegin, escapedCat, gCatEnd );
                MemPtrFree( escapedCat );
            }
        }
        MemHandleUnlock( catHandle );
    }
    if ( catEntry == NULL ) {
        MemPtrFree( escaped );
        MemPtrFree( titleEntry );
    }

    postText = (char *)MemPtrNew( 6000 );
    if ( postText == NULL ) {
        MemPtrFree( escaped );
        MemPtrFree( titleEntry );
        MemPtrFree( catEntry );
        return -3;
    }

    escapedName = EscapeString( gPrefs.name );
    if ( escapedName == NULL ) {
        MemPtrFree( escaped );
        MemPtrFree( titleEntry );
        MemPtrFree( catEntry );
        MemPtrFree( postText );
        return -4;
    }

    escapedPass = EscapeString( gPrefs.pass );
    if ( escapedPass == NULL ) {
        MemPtrFree( escaped );
        MemPtrFree( titleEntry );
        MemPtrFree( catEntry );
        MemPtrFree( postText );
        MemPtrFree( escapedName );
        return -5;
    }

    if ( gPrefs.publishFlag == PUB_LATER ) {
        publishFld = 0;
    } else {
        publishFld = 1;
    }

    StrPrintF( postText, gNewPostReq, gPrefs.blogID, escapedName, escapedPass,
               titleEntry, catEntry, escaped, publishFld );

    MemPtrFree( escaped );
    MemPtrFree( titleEntry );
    MemPtrFree( catEntry );
    MemPtrFree( escapedName );
    MemPtrFree( escapedPass );

    target.host = gPrefs.host;
    target.port = StrAToI( gPrefs.port );
    target.path = gPrefs.url;

    SetTextField( statusField, "Transmitting" );
    FldDrawField( statusField );

    retValue = -1;
    if ( (postres = HTTPPost( &target, postText, (char *)gTempDBName )) == HTTPErr_OK ) {
        SetTextField( statusField, "Processing Response" );
        FldDrawField( statusField );
        if ( ReadWholeFile( gTempDBName, postText, 6000 ) ) {
            if ( ParseXMLRPCResponse( postText, &fault ) == 0 ) {
                FrmAlert( PostSuccessAlert );
                retValue = 0;
                
                if ( gPrefs.postAction == PA_CLEAR_TEXT ) {
                    SetTextField( postField, "" );
                    if ( titleField != NULL ) {
                        SetTextField( titleField, "" );
                    }
                    if ( catField != NULL ) {
                        SetTextField( catField, "" );
                    }
                }
            } else {
                FrmCustomAlert( PostErrAlert, fault.string, NULL, NULL );
            }

        } else {
            FrmCustomAlert( PostErrAlert, "Out of memory processing response",
                            NULL, NULL );
        }
    } else {
        FrmCustomAlert( PostErrAlert, "Unable to contact server", NULL, NULL );
    }

    MemPtrFree( postText );

    return retValue;
}


/*
 */

static Boolean PostActionFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;

    switch ( event->eType ) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();
            FrmDrawForm( frm );
            PostFormData();
            FrmReturnToForm( FormForType( gPrefs.blogType ) );
            FrmUpdateForm( FormForType( gPrefs.blogType ),
                           frmRedrawUpdateCode );
            handled = true;
            break;

        case frmUpdateEvent:
            FrmDrawForm( FrmGetActiveForm() );
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

void DefaultPrefs( void )
{
    gPrefs.name[0] = '\0';
    gPrefs.pass[0] = '\0';
    gPrefs.blogID[0] = '\0';
    StrCopy( gPrefs.host, gDefaultHost );
    StrCopy( gPrefs.port, gDefaultPort );
    StrCopy( gPrefs.url, gDefaultURL );
    StrCopy( gPrefs.timeout, gDefaultTimeout );
    gPrefs.reg[0] = '\0';
    gPrefs.posts = 0;
    gPrefs.postAction = PA_CLEAR_TEXT;
    gPrefs.blogType = BT_BLOGGER;
    gPrefs.publishFlag = PUB_IMMED;
}


/*
 */

void Version1PrefsConvert( void )
{
    VagablogPrefs_v1 prefs;
    UInt16 prefsSize;
    Int16 version;

    prefsSize = sizeof(prefs);
    version = PrefGetAppPreferences( gCreator, gAppPrefID, &prefs, &prefsSize,
                                     true );
    if ( version != 1 ) {
        /* Umm.. weird */
        DefaultPrefs();
        return;
    }

    /* Fill in the new fields */
    gPrefs.postAction = PA_CLEAR_TEXT;
    StrCopy( gPrefs.timeout, gDefaultTimeout );
    gPrefs.blogType = BT_BLOGGER;
    gPrefs.publishFlag = PUB_IMMED;

    /* Move the older fields */
    StrCopy( gPrefs.name, prefs.name );
    StrCopy( gPrefs.pass, prefs.pass );
    StrCopy( gPrefs.blogID, prefs.blogID );
    StrCopy( gPrefs.host, prefs.host );
    StrCopy( gPrefs.port, prefs.port );
    StrCopy( gPrefs.url, prefs.url );
    StrCopy( gPrefs.reg, prefs.reg );
    gPrefs.posts = prefs.posts;
}


/*
 */

void BlogListToDB( int entries, BlogEntry *blogs )
{
    DmOpenRef ref;
    int i;
    Err error;
    UInt16 index;
    UInt32 saveSize;
    MemHandle dbHandle;
    void *rec;

    ref = DmOpenDatabaseByTypeCreator( gPrefDBType, gCreator,
                                       dmModeReadWrite );
    if ( !ref ) {
        error = DmCreateDatabase( 0, gPrefDBName, gCreator, gPrefDBType,
                                  false );
        if ( error ) {
            return;
        }

        ref = DmOpenDatabaseByTypeCreator( gPrefDBType, gCreator,
                                           dmModeReadWrite );
        if ( !ref ) {
            return;
        }
    }

    saveSize = sizeof(BlogEntry);

    for ( i = 0; i < entries; i++ ) {
        index = dmMaxRecordIndex;

        dbHandle = DmNewRecord( ref, &index, saveSize );
        if ( dbHandle == NULL ) {
            return;
        }

        rec = MemHandleLock( dbHandle );
        if ( rec == NULL ) {
            return;
        }

        DmStrCopy( rec, 0, blogs[i].name );
        DmStrCopy( rec, BLOG_NAME_LEN, blogs[i].id );

        MemHandleUnlock( dbHandle );
        DmReleaseRecord( ref, index, false );
    }

    DmCloseDatabase( ref );
}


/*
 */

void Version2PrefsConvert( void )
{
    VagablogPrefs_v2 prefs;
    UInt16 prefsSize;
    Int16 version;

    prefsSize = sizeof(prefs);
    version = PrefGetAppPreferences( gCreator, gAppPrefID, &prefs, &prefsSize,
                                     true );
    if ( version != 2 ) {
        /* Umm.. weird */
        DefaultPrefs();
        return;
    }

    /* Fill in the new fields */
    StrCopy( gPrefs.timeout, gDefaultTimeout );
    gPrefs.blogType = BT_BLOGGER;
    gPrefs.publishFlag = PUB_IMMED;

    /* Copy over the old fields */
    StrCopy( gPrefs.name, prefs.name );
    StrCopy( gPrefs.pass, prefs.pass );
    StrCopy( gPrefs.blogID, prefs.blogID );
    StrCopy( gPrefs.host, prefs.host );
    StrCopy( gPrefs.port, prefs.port );
    StrCopy( gPrefs.url, prefs.url );
    StrCopy( gPrefs.reg, prefs.reg );
    gPrefs.posts = prefs.posts;
    gPrefs.postAction = prefs.postAction;

    /* Transcribe blogs to prefs database */
    BlogListToDB( prefs.blogEntries, prefs.blogs );
}


/*
 */

void Version3PrefsConvert( void )
{
    VagablogPrefs_v3 prefs;
    UInt16 prefsSize;
    Int16 version;

    prefsSize = sizeof(prefs);
    version = PrefGetAppPreferences( gCreator, gAppPrefID, &prefs, &prefsSize,
                                     true );
    if ( version != 3 ) {
        /* Umm.. weird */
        DefaultPrefs();
        return;
    }

    /* Fill in the new fields */
    gPrefs.blogType = BT_BLOGGER;
    gPrefs.publishFlag = PUB_IMMED;

    /* Copy over the old fields */
    StrCopy( gPrefs.name, prefs.name );
    StrCopy( gPrefs.pass, prefs.pass );
    StrCopy( gPrefs.blogID, prefs.blogID );
    StrCopy( gPrefs.host, prefs.host );
    StrCopy( gPrefs.port, prefs.port );
    StrCopy( gPrefs.url, prefs.url );
    StrCopy( gPrefs.reg, prefs.reg );
    gPrefs.posts = prefs.posts;
    gPrefs.postAction = prefs.postAction;
    StrCopy( gPrefs.timeout, prefs.timeout );

    /* Transcribe blogs to prefs database */
    BlogListToDB( prefs.blogEntries, prefs.blogs );
}


/*
 */

void Version4PrefsConvert( void )
{
    VagablogPrefs_v4 prefs;
    UInt16 prefsSize;
    Int16 version;

    prefsSize = sizeof(prefs);
    version = PrefGetAppPreferences( gCreator, gAppPrefID, &prefs, &prefsSize,
                                     true );
    if ( version != 4 ) {
        /* Umm.. weird */
        DefaultPrefs();
        return;
    }

    /* Fill in the new fields */
    gPrefs.publishFlag = PUB_IMMED;

    /* Copy over the old fields */
    StrCopy( gPrefs.name, prefs.name );
    StrCopy( gPrefs.pass, prefs.pass );
    StrCopy( gPrefs.blogID, prefs.blogID );
    StrCopy( gPrefs.host, prefs.host );
    StrCopy( gPrefs.port, prefs.port );
    StrCopy( gPrefs.url, prefs.url );
    StrCopy( gPrefs.reg, prefs.reg );
    gPrefs.posts = prefs.posts;
    gPrefs.postAction = prefs.postAction;
    StrCopy( gPrefs.timeout, prefs.timeout );
    gPrefs.blogType = prefs.blogType;

    /* Transcribe blogs to prefs database */
    BlogListToDB( prefs.blogEntries, prefs.blogs );
}


/*
 */

void Version5PrefsConvert( void )
{
    VagablogPrefs_v5 prefs;
    UInt16 prefsSize;
    Int16 version;

    prefsSize = sizeof(prefs);
    version = PrefGetAppPreferences( gCreator, gAppPrefID, &prefs, &prefsSize,
                                     true );
    if ( version != 5 ) {
        /* Umm.. weird */
        DefaultPrefs();
        return;
    }

    /* Copy over the old fields */
    StrCopy( gPrefs.name, prefs.name );
    StrCopy( gPrefs.pass, prefs.pass );
    StrCopy( gPrefs.blogID, prefs.blogID );
    StrCopy( gPrefs.host, prefs.host );
    StrCopy( gPrefs.port, prefs.port );
    StrCopy( gPrefs.url, prefs.url );
    StrCopy( gPrefs.reg, prefs.reg );
    gPrefs.posts = prefs.posts;
    gPrefs.postAction = prefs.postAction;
    StrCopy( gPrefs.timeout, prefs.timeout );
    gPrefs.blogType = prefs.blogType;
    gPrefs.publishFlag = prefs.publishFlag;

    /* Transcribe blogs to prefs database */
    BlogListToDB( prefs.blogEntries, prefs.blogs );
}


/*
 */

static void StartApp( void )
{
    UInt16 prefsSize;
    Err error;
    Int16 version;

    prefsSize = 0;
    version = PrefGetAppPreferences( gCreator, gAppPrefID, NULL, &prefsSize,
                                     true );
    if ( version == noPreferenceFound ) {
        DefaultPrefs();
    } else if ( version != gAppPrefVersion ) {
        switch ( version ) {
            case 1:
                Version1PrefsConvert();
                break;

            case 2:
                Version2PrefsConvert();
                break;

            case 3:
                Version3PrefsConvert();
                break;

            case 4:
                Version4PrefsConvert();
                break;

            case 5:
                Version5PrefsConvert();
                break;

            default:
                DefaultPrefs();
                break;
        }
    } else {
        prefsSize = sizeof(gPrefs);
        version = PrefGetAppPreferences( gCreator, gAppPrefID, &gPrefs,
                                         &prefsSize, true );
    }

    HTTPLibStart( 'VBlg', StrAToI( gPrefs.timeout ) );

    gDBRef = DmOpenDatabaseByTypeCreator( gDBType, gCreator, dmModeReadWrite );
    if ( !gDBRef ) { 
        error = DmCreateDatabase( 0, gDBName, gCreator, gDBType, false ); 
        if ( error ) {
            gDBRef = NULL;
            return;
        }

        gDBRef = DmOpenDatabaseByTypeCreator( gDBType, gCreator,
                                              dmModeReadWrite ); 
        if ( !gDBRef ) {
            gDBRef = NULL;
            return;
        }
    }

    gPrefDBRef = DmOpenDatabaseByTypeCreator( gPrefDBType, gCreator,
                                              dmModeReadWrite );
    if ( !gPrefDBRef ) {
        error = DmCreateDatabase( 0, gPrefDBName, gCreator, gPrefDBType, 
                                 false );
        if ( error ) {
            gPrefDBRef = NULL;
            return;
        }

        gPrefDBRef = DmOpenDatabaseByTypeCreator( gPrefDBType, gCreator,
                                                  dmModeReadWrite );
        if ( !gPrefDBRef ) {
            gPrefDBRef = NULL;
            return;
        }
    }

    FrmGotoForm( FormForType( gPrefs.blogType ) );
}


/*
 */

static void EndApp( void )
{
    PrefSetAppPreferences( gCreator, gAppPrefID, gAppPrefVersion, &gPrefs,
                           sizeof(gPrefs), true );

    FrmCloseAllForms();
    HTTPLibStop();
    if ( gDBRef != NULL ) {
        DmCloseDatabase( gDBRef );
    }
    if ( gPrefDBRef != NULL ) {
        DmCloseDatabase( gPrefDBRef );
    }
}


/*
 */

UInt32 PilotMain( UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags )
{
    EventType event;
    Err err;

    if ( cmd != sysAppLaunchCmdNormalLaunch ) {
        return 0;
    }

    StartApp();

    do {
        EvtGetEvent( &event, evtWaitForever );

        if ( !SysHandleEvent( &event ) ) {
            if ( !MenuHandleEvent( 0, &event, &err ) ) {
                if ( !ApplicationHandleEvent( &event ) ) {
                    FrmDispatchEvent( &event );
                }
            }
        }
    } while( event.eType != appStopEvent );

    EndApp();

    return 0;
}


/*
 */

static void PostFormUpdateScrollbar( void )
{
    FieldPtr field;
    ScrollBarPtr scrollbar;
    UInt16 position;
    UInt16 height;
    UInt16 fieldHeight;
    Int16 maxValue;

    field = GetCurrFormObjPtr( BlogEntryFld );
    scrollbar = GetCurrFormObjPtr( BlogEntryScrl );

    FldGetScrollValues( field, &position, &height, &fieldHeight );

    if ( height > fieldHeight ) {
        maxValue = (height - fieldHeight) + FldGetNumberOfBlankLines( field );
    } else if ( position ) {
        maxValue = position;
    } else {
        maxValue = 0;
    }
    
    SclSetScrollBar( scrollbar, position, 0, maxValue, fieldHeight - 1 );
}


/*
 */

static void PostFormScroll( Int16 lines )
{
    FieldPtr field;
    UInt16 blank;

    field = GetCurrFormObjPtr( BlogEntryFld );
    blank = FldGetNumberOfBlankLines( field );

    if ( lines < 0 ) {
        FldScrollField( field, -lines, winUp );
    } else if ( lines > 0 ) {
        FldScrollField( field, lines, winDown );
    }

    PostFormUpdateScrollbar();
}


/*
 */

static void PostFormPageScroll( WinDirectionType direction )
{
    FieldPtr field;
    UInt16 lines;

    field = GetCurrFormObjPtr( BlogEntryFld );
    if ( FldScrollable( field, direction ) ) {
        lines = FldGetVisibleLines( field ) - 1;

        if ( direction == winUp ) {
            lines = -lines;
        }

        PostFormScroll( lines );
    }
}


/*
 */

static void ParsePostFields( char *record, UInt32 length )
{
    int i;
    int numNulls;
    char *title;
    char *category;
    char *post;
    FieldPtr titleFld;
    FieldPtr catFld;

    numNulls = 0;
    for ( i = 0; i < length; i++ ) {
        if ( record[i] == '\0' ) {
            numNulls++;
        }
    }
    if ( numNulls != 3 ) {
        return;
    }

    title = record;
    category = title + (StrLen( title ) + 1);
    post = category + (StrLen( category ) + 1);

    if ( FormHasTitle() ) {
        if ( ( titleFld = GetCurrFormObjPtr( BlogTitleFld ) ) != NULL ) {
            SetTextField( titleFld, title );
        }
    }
    if ( FormHasCategory() ) {
        if ( ( catFld = GetCurrFormObjPtr( BlogCategoryFld ) ) != NULL ) {
            SetTextField( GetCurrFormObjPtr( BlogCategoryFld ), category );
        }
    }
    SetTextField( GetCurrFormObjPtr( BlogEntryFld ), post );
}


/*
 */

static void UnserializeDBRec( MemHandle hndl )
{
    UInt32 recSize;
    char *fieldText;

    recSize = MemHandleSize( hndl );
    fieldText = MemHandleLock( hndl );

    if ( StrLen( fieldText ) != (recSize - 1) ) {
        ParsePostFields( fieldText, recSize );
    }

    MemHandleUnlock( hndl );
}


/*
 */

static void SerializeDBRec( void )
{
    FieldPtr titleField;
    FieldPtr catField;
    FieldPtr postField;
    MemHandle titleHndl;
    MemHandle catHndl;
    MemHandle postHndl;
    char *titleText;
    char *catText;
    char *postText;
    UInt32 saveSize;
    UInt16 index;
    UInt32 recOff;
    MemHandle dbHandle;
    char *recText;

    if ( FormHasTitle() ) {
        titleField = GetCurrFormObjPtr( BlogTitleFld );
    } else {
        titleField = NULL;
    }

    if ( titleField == NULL ) {
        titleHndl = NULL;
    } else {
        titleField = GetCurrFormObjPtr( BlogTitleFld );
        titleHndl = FldGetTextHandle( titleField );
    }

    if ( FormHasCategory() ) {
        catField = GetCurrFormObjPtr( BlogCategoryFld );
    } else {
        catField = NULL;
    }

    if ( catField == NULL ) {
        catHndl = NULL;
    } else { 
        catField = GetCurrFormObjPtr( BlogCategoryFld );
        catHndl = FldGetTextHandle( catField );
    }

    postField = GetCurrFormObjPtr( BlogEntryFld );
    postHndl = FldGetTextHandle( postField );

    if ( titleHndl == NULL ) {
        saveSize = 1;
        titleText = NULL;
    } else {
        titleText = MemHandleLock( titleHndl );
        saveSize = StrLen( titleText ) + 1;
    }

    if ( catHndl == NULL ) {
        saveSize += 1;
        catText = NULL;
    } else {
        catText = MemHandleLock( catHndl );
        saveSize += (StrLen( catText ) + 1);
    }

    if ( postHndl == NULL ) {
        saveSize += 1;
        postText = NULL;
    } else {
        postText = MemHandleLock( postHndl );
        saveSize += (StrLen( postText ) + 1);
    }

    index = 0;
    if ( DmNumRecords( gDBRef ) > 0 ) {
        dbHandle = DmResizeRecord( gDBRef, index, saveSize );
        if ( dbHandle == NULL ) {
            goto unlock_fields;
        }
        dbHandle = DmGetRecord( gDBRef, index );
    } else {
        dbHandle = DmNewRecord( gDBRef, &index, saveSize );
        if ( dbHandle == NULL ) {
            goto unlock_fields;
        }
    }

    recOff = 0;
    recText = MemHandleLock( dbHandle );
    if ( recText != NULL ) {
        if ( titleHndl == NULL ) {
            DmWrite( recText, 0, "", 1 );
            recOff += 1;
        } else {
            DmWrite( recText, 0, titleText, (StrLen( titleText ) + 1) );
            recOff += (StrLen( titleText ) + 1);
        }

        if ( catHndl == NULL ) {
            DmWrite( recText, recOff, "", 1 );
            recOff += 1;
        } else {
            DmWrite( recText, recOff, catText, (StrLen( catText ) + 1) );
            recOff += (StrLen( catText ) + 1);
        }

        if ( postHndl == NULL ) {
            DmWrite( recText, recOff, "", 1 );
            recOff += 1;
        } else {
            DmWrite( recText, recOff, postText, (StrLen( postText ) + 1) );
            recOff += (StrLen( postText ) + 1);
        }

        MemHandleUnlock( dbHandle );
    }

    DmReleaseRecord( gDBRef, index, false );

unlock_fields:
    if ( titleHndl != NULL ) {
        MemHandleUnlock( titleHndl );
    }
    if ( catHndl != NULL ) {
        MemHandleUnlock( catHndl );
    }
    if ( postHndl != NULL ) {
        MemHandleUnlock( postHndl );
    }
}


/*
 */

static Boolean FormHasCategory( void )
{
    switch ( FrmGetActiveFormID() ) {
        case MinimalForm:
            return( false );
            break;

        case MainForm:
            return( true );
            break;

        default:
            break;
    }

    return( false );
}


/*
 */

static Boolean FormHasTitle( void )
{
    switch ( FrmGetActiveFormID() ) {
        case MinimalForm:
            return( false );
            break;

        case MainForm:
            return( true );
            break;

        default:
            break;
    }

    return( false );
}


/*
 */

static UInt16 FormForType( int type )
{
    switch ( type ) {
        case BT_BLOGGER:
        case BT_MOVABLETYPE:
        case BT_TYPEPAD:
            return( MinimalForm );
            break;

        case BT_JOURNALSPACE:
        case BT_WORDPRESS:
        case BT_B2:
        case BT_LIVEJOURNAL:
            return( MainForm );
            break;

        default:
            break;
    }

    return( MinimalForm );
}


/*
 */

static void AddMarkup( Int16 res, const char *single, const char *front,
                       const char *back )
{
    FieldPtr field;
    UInt16 start;
    UInt16 end;
    char *fieldTxt;
    char *insertTxt;
    int index;
    int length;
    int copyi;

    field = GetCurrFormObjPtr( res );
    FldGetSelection( field, &start, &end );
    if ( start == end ) {
        FldInsert( field, single, StrLen( single ) );
    } else {
        fieldTxt = FldGetTextPtr( field );
        length = (end - start) + StrLen( front ) + StrLen( back );
        insertTxt = (char *)MemPtrNew( length );
        if ( insertTxt == NULL ) {
            return;
        }
        index = 0;
        for ( copyi = 0; copyi < StrLen( front ); copyi++ ) {
            insertTxt[index++] = front[copyi];
        }
        for ( copyi = start; copyi < end; copyi++ ) {
            insertTxt[index++] = fieldTxt[copyi];
        }
        for ( copyi = 0; copyi < StrLen( back ); copyi++ ) {
            insertTxt[index++] = back[copyi];
        }
        FldInsert( field, insertTxt, length );
        MemPtrFree( insertTxt );
    }
}


/*
 */

static Boolean MainFormHandler( EventType *event )
{
    Boolean handled = false;
    EventType updateEvt;
    FormType *frm;
    MemHandle dbHandle;
    UInt16 frmId;
    FieldPtr field;
    UInt16 start;
    UInt16 end;
    char *fieldTxt;
    int index;
    int length;
    int copyi;
    char *newText;

    switch ( event->eType ) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();

            if ( DmNumRecords( gDBRef ) > 0 ) {
                dbHandle = DmQueryRecord( gDBRef, 0 );
                if ( dbHandle != NULL ) {
                    UnserializeDBRec( dbHandle );
                /*
                    recText = MemHandleLock( dbHandle );
                    SetTextField( GetCurrFormObjPtr( BlogEntryFld ), recText );
                    MemHandleUnlock( dbHandle );
                */
                }
            }

            FrmDrawForm( frm );

            PostFormUpdateScrollbar();
            if ( (gPrefs.blogType == BT_BLOGGER) || 
                    (gPrefs.blogType == BT_MOVABLETYPE) ||
                    (gPrefs.blogType == BT_TYPEPAD) ) {
                FrmSetFocus( frm, FrmGetObjectIndex( frm, BlogEntryFld ) );
            } else {
                FrmSetFocus( frm, FrmGetObjectIndex( frm, BlogTitleFld ) );
            }

            MemSet(&updateEvt, sizeof(EventType), 0);
            updateEvt.eType = frmUpdateEvent;
            updateEvt.data.frmUpdate.formID = FrmGetActiveFormID();
            EvtAddEventToQueue(&updateEvt);

            handled = true;
            break;

        case frmCloseEvent:
            if ( gDBRef == NULL ) {
                break;
            }
            /*

            field = GetCurrFormObjPtr( BlogEntryFld );
            fieldHandle = FldGetTextHandle( field );
            if ( fieldHandle == NULL ) {
                break;
            }

            fieldText = MemHandleLock( fieldHandle );
            saveSize = StrLen( fieldText ) + 1;
            index = 0;

            if ( DmNumRecords( gDBRef ) > 0 ) {
                dbHandle = DmResizeRecord( gDBRef, index, saveSize );
                if ( dbHandle == NULL ) {
                    MemHandleUnlock( fieldHandle );
                    break;
                }
            } else {
                dbHandle = DmNewRecord( gDBRef, &index, saveSize );
                if ( dbHandle == NULL ) {
                    MemHandleUnlock( fieldHandle );
                    break;
                }
            }

            recText = MemHandleLock( dbHandle );
            if ( recText == NULL ) {
                DmReleaseRecord( gDBRef, index, false );
                MemHandleUnlock( fieldHandle );
                break;
            }

            DmWrite( recText, 0, fieldText, saveSize );

            MemHandleUnlock( dbHandle );
            DmReleaseRecord( gDBRef, index, false );
            MemHandleUnlock( fieldHandle );
            */
            SerializeDBRec();
            break;

        case winEnterEvent:
            frmId = FrmGetActiveFormID();
            if ( frmId != FormForType( gPrefs.blogType ) ) {
                FrmGotoForm( FormForType( gPrefs.blogType ) );
                break;
            }

            if ( (gLinkText != NULL) && (gLinkURL != NULL) ) {
                newText = (char *)MemPtrNew( StrLen( gLinkText ) +
                                             StrLen(gLinkURL ) + 16 );
                if ( newText != NULL ) {
                    StrPrintF( newText, "<a href=\"%s\">%s</a>", gLinkURL,
                               gLinkText );
                    field = GetCurrFormObjPtr( BlogEntryFld );
                    FldInsert( field, newText, StrLen( newText ) );
                    MemPtrFree( newText );
                    MemPtrFree( gLinkText );
                    gLinkText = NULL;
                    MemPtrFree( gLinkURL );
                    gLinkURL = NULL;
                }
            }
            break;

        case frmUpdateEvent:
            FrmDrawForm( FrmGetActiveForm() );
            handled = true;
            break;

        case fldChangedEvent:
            PostFormUpdateScrollbar();
            handled = true;
            break;

        case sclRepeatEvent:
            PostFormScroll( event->data.sclRepeat.newValue - 
                    event->data.sclRepeat.value );
            break;

        case ctlSelectEvent:
            switch ( event->data.ctlSelect.controlID ) {
                case PostBtn:
                    FrmPopupForm( PostActionForm );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case menuEvent:
            switch ( event->data.menu.itemID ) {
                case IdentityMItem:
                    FrmPopupForm( IdentityForm );
                    handled = true;
                    break;

                case ServerMItem:
                    gSvrFormSaved = gPrefs.blogType;
                    FrmPopupForm( ServerForm );
                    handled = true;
                    break;

                case PostOptsMItem:
                    FrmPopupForm( PostOptsForm );
                    handled = true;
                    break;

                case AboutMItem:
                    FrmPopupForm( AboutForm );
                    handled = true;
                    break;

                case BoldMItem:
                    AddMarkup( BlogEntryFld, "<b>", "<b>", "</b>" );
                    handled = true;
                    break;

                case EndBoldMItem:
                    AddMarkup( BlogEntryFld, "</b>", "<b>", "</b>" );
                    handled = true;
                    break;

                case ItalicMItem:
                    AddMarkup( BlogEntryFld, "<i>", "<i>", "</i>" );
                    handled = true;
                    break;

                case EndItalicMItem:
                    AddMarkup( BlogEntryFld, "</i>", "<i>", "</i>" );
                    handled = true;
                    break;

                case UnderlineMItem:
                    AddMarkup( BlogEntryFld, "<u>", "<u>", "</u>" );
                    handled = true;
                    break;

                case EndUnderlineMItem:
                    AddMarkup( BlogEntryFld, "</u>", "<u>", "</u>" );
                    handled = true;
                    break;

                case EmphasisMItem:
                    AddMarkup( BlogEntryFld, "<em>", "<em>", "</em>" );
                    handled = true;
                    break;

                case EndEmphasisMItem:
                    AddMarkup( BlogEntryFld, "</em>", "<em>", "</em>" );
                    handled = true;
                    break;

                case LinkMItem:
                    field = GetCurrFormObjPtr( BlogEntryFld );
                    FldGetSelection( field, &start, &end );
                    if ( start != end ) {
                        fieldTxt = FldGetTextPtr( field );
                        length = (end - start) + 1;

                        gLinkText = (char *)MemPtrNew( length );
                        if ( gLinkText != NULL ) {
                            index = 0;
                            for ( copyi = start; copyi < end; copyi++ ) {
                                gLinkText[index++] = fieldTxt[copyi];
                            }
                            gLinkText[index++] = '\0';
                        }
                    }
                    FrmPopupForm( CreateLinkForm );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case keyDownEvent: 
            switch ( event->data.keyDown.chr ) {
                case pageDownChr:
                    PostFormPageScroll( winDown );
                    handled = true;
                    break;

                case pageUpChr:
                    PostFormPageScroll( winUp );
                    handled = true;
                    break;

                case vchrNextField:
                    if ( (gPrefs.blogType == BT_MOVABLETYPE) ||
                         (gPrefs.blogType == BT_BLOGGER) ||
                         (gPrefs.blogType == BT_TYPEPAD) ) {
                        handled = true;
                        break;
                    }
                    frm = FrmGetActiveForm();
                    switch ( FrmGetObjectId( frm, FrmGetFocus( frm ) ) ) {
                        case BlogEntryFld:
                            FrmSetFocus( frm, 
                                    FrmGetObjectIndex( frm, BlogTitleFld ) );
                            break;

                        case BlogTitleFld:
                            FrmSetFocus( frm,
                                   FrmGetObjectIndex( frm, BlogCategoryFld ) );
                            break;

                        case BlogCategoryFld:
                            FrmSetFocus( frm,
                                   FrmGetObjectIndex( frm, BlogEntryFld ) );
                            break;

                        default:
                            break;
                    }
                    handled = true;
                    break;

                case vchrPrevField:
                    if ( (gPrefs.blogType == BT_MOVABLETYPE) ||
                         (gPrefs.blogType == BT_BLOGGER) ||
                         (gPrefs.blogType == BT_TYPEPAD) ) {
                        handled = true;
                        break;
                    }
                    frm = FrmGetActiveForm();
                    switch ( FrmGetObjectId( frm, FrmGetFocus( frm ) ) ) {
                        case BlogEntryFld:
                            FrmSetFocus( frm, 
                                   FrmGetObjectIndex( frm, BlogCategoryFld ) );
                            break;

                        case BlogTitleFld:
                            FrmSetFocus( frm,
                                   FrmGetObjectIndex( frm, BlogEntryFld ) );
                            break;

                        case BlogCategoryFld:
                            FrmSetFocus( frm,
                                   FrmGetObjectIndex( frm, BlogTitleFld ) );
                            break;

                        default:
                            break;
                    }
                    handled = true;
                    break;


                default:
                    break;
            }
            break;

        case nilEvent:
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

static Boolean AboutFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;

    switch (event->eType) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();
            FrmDrawForm(frm);

            break;

        case frmUpdateEvent:
            FrmDrawForm(FrmGetActiveForm());
            handled = true;
            break;

        case ctlSelectEvent:
            switch ( event->data.ctlSelect.controlID ) {
                case AboutOKBtn:
                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case nilEvent:
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}

#define VALUE_TAG "<value>"
#define STRING_TAG "<string>"

#define BLOG_ID_SRCH "<name>blogid</name>"
#define BLOG_ID_SRCH_LEN (19)


/*
 */

static int ExtractBlogID( char *start, char *end, BlogEntry *entry )
{
    char *search;
    char *skiped;
    char *endSearch;
    int copyOffset;

    search = start;
    endSearch = end - BLOG_ID_SRCH_LEN;
    if ( endSearch < search ) {
        return -1;
    }

    while ( search < endSearch ) {
        if ( *search == '<' ) {
            if ( StrNCompare( search, BLOG_ID_SRCH, BLOG_ID_SRCH_LEN ) == 0 ) {
                search += BLOG_ID_SRCH_LEN;
                search = StrStr( search, VALUE_TAG );
                if ( search == NULL ) {
                    return -1;
                }
                search += StrLen( VALUE_TAG );
                skiped = SkipWhitespaceMatch( search, STRING_TAG );
                if ( skiped != NULL ) {
                    search = skiped;
                }
                copyOffset = 0;
                while ( (*search != '<') && (search < end) && 
                        (copyOffset < (BLOG_ID_LEN - 1)) ) {
                    (entry->id)[copyOffset++] = *search++;
                }
                (entry->id)[copyOffset] = '\0';
                return 0;
            }
        }
        search++;
    }

    return -1;
}


#define BLOG_NAME_SRCH "<name>blogName</name>"
#define BLOG_NAME_SRCH_LEN (21)


/*
 */

static int ExtractBlogName( char *start, char *end, BlogEntry *entry )
{
    char *search;
    char *skiped;
    char *endSearch;
    int copyOffset;

    search = start;
    endSearch = end - BLOG_NAME_SRCH_LEN;
    if ( endSearch < search ) {
        return -1;
    }

    while ( search < endSearch ) {
        if ( *search == '<' ) {
            if ( StrNCompare( search, BLOG_NAME_SRCH,
                              BLOG_NAME_SRCH_LEN ) == 0 ) {
                search += BLOG_NAME_SRCH_LEN;
                search = StrStr( search, VALUE_TAG );
                if ( search == NULL ) {
                    return -1;
                }
                search += StrLen( VALUE_TAG );
                skiped = SkipWhitespaceMatch( search, STRING_TAG );
                if ( skiped != NULL ) {
                    search = skiped;
                }
                copyOffset = 0;
                while ( (*search != '<') && (search < end) && 
                        (copyOffset < (BLOG_NAME_LEN - 1)) ) {
                    (entry->name)[copyOffset++] = *search++;
                }
                (entry->name)[copyOffset] = '\0';
                return 0;
            }
        }
        search++;
    }
    
    return -1;
}


/*
 */

static int ResizeEntries( BlogEntry **entries, int count, int startCount )
{
    BlogEntry *newEntry;
    int i;

    newEntry = (BlogEntry *)MemPtrNew( sizeof(BlogEntry) * count );
    if ( newEntry == NULL ) {
        return 1;
    }

    for ( i = 0; i < startCount; i++ ) {
        StrCopy( newEntry[i].name, (*entries)[i].name );
        StrCopy( newEntry[i].id, (*entries)[i].id );
    }

    MemPtrFree( *entries );
    *entries = newEntry;

    return 0;
}


/*
 */

static void PullBlogInfo( char *params, BlogEntry **entries, int *count )
{
    char *structStart;
    char *structEnd;
    int allocLen;
    FormType *form;
    FieldPtr field;
    char statusString[50];

    form = FrmGetActiveForm();
    field = (FieldPtr)GetObjectPtr( form, BlogLoadStatus );

    allocLen = MAX_BLOGS;
    *entries = (BlogEntry *)MemPtrNew( sizeof(BlogEntry) * allocLen );
    if ( *entries == NULL ) {
        *count = 0;
        return;
    }

    structEnd = params;
    *count = 0;

    while ( (structStart = StrStr( structEnd, "<struct>" )) != NULL ) {
        if ( *count == allocLen ) {
            if ( ResizeEntries( entries, allocLen + MAX_BLOGS, allocLen )
                    != 0 ) {
                MemPtrFree( *entries );
                return;
            }
            allocLen += MAX_BLOGS;
        }

        structEnd = StrStr( structStart, "</struct>" );
        if ( structEnd == NULL ) {
            return;
        }
        if ( ExtractBlogID( structStart, structEnd,
                            &((*entries)[*count]) ) != 0 ) {
            return;
        }
        if ( ExtractBlogName( structStart, structEnd,
                              &((*entries)[*count]) ) != 0 ) {
            return;
        }
        *count += 1;

        StrPrintF( statusString, "Found blog %d", *count );
        SetTextField( field, statusString );
        FldDrawField( field );
    }
}


/*
 */

static int ParseBlogInfo( char *response, FaultInfo *fault, 
                          BlogEntry **entries, int *count )
{
    char *methodResponse;
    char *afterResponse;
    char *params;
    char *faultTag;

    if ( ParseXMLRPCDecl( response, &methodResponse ) != 0 ) {
        fault->code = 0;
        StrCopy( fault->string, "Unable to find XML declaration" );
        return -1;
    }

    afterResponse = SkipWhitespaceMatch( methodResponse, "<methodResponse>" );
    if ( afterResponse == NULL ) {
        fault->code = 0;
        StrCopy( fault->string, "Missing method response tag" );
        return -1;
    }

    params = SkipWhitespaceMatch( afterResponse, "<params>" );
    if ( params != NULL ) {
        PullBlogInfo( params, entries, count );
        return 0;
    }

    faultTag = SkipWhitespaceMatch( afterResponse, "<fault>" );
    if ( faultTag != NULL ) {
        ParseFaultInfo( faultTag, fault );
        return -1;
    }

    fault->code = 0;
    StrCopy( fault->string, "Unexpected tag after method response" );

    return -1;
}


/*
 */

static int LoadUserBlogs( BlogEntry **entries, int *count )
{
    char *request;
    URLTarget target;
    int retValue;
    FaultInfo fault;
    FormType *form;
    FieldPtr field;
    char *escapedName;
    char *escapedPass;

    form = FrmGetActiveForm();
    field = (FieldPtr)GetObjectPtr( form, BlogLoadStatus );

    SetTextField( field, "Formatting request" );
    FldDrawField( field );

    escapedName = EscapeString( gPrefs.name );
    if ( escapedName == NULL ) {
        FrmCustomAlert( BlogLoadErrAlert,
                        "Out of memory trying to form request", NULL, NULL );
        return -1;
    }

    escapedPass = EscapeString( gPrefs.pass );
    if ( escapedPass == NULL ) {
        FrmCustomAlert( BlogLoadErrAlert,
                        "Out of memory trying to form request", NULL, NULL );
        MemPtrFree( escapedName );
        return -1;
    }

    request = (char *)MemPtrNew( INFOREQ_SIZE );
    if ( request == NULL ) {
        FrmCustomAlert( BlogLoadErrAlert,
                        "Out of memory trying to form request", NULL, NULL );
        MemPtrFree( escapedName );
        MemPtrFree( escapedPass );
        return -1;
    }

    StrPrintF( request, gUsersBlogsReq, escapedName, escapedPass );

    MemPtrFree( escapedName );
    MemPtrFree( escapedPass );

    target.host = gPrefs.host;
    target.port = StrAToI( gPrefs.port );
    target.path = gPrefs.url;

    SetTextField( field, "Transmitting" );
    FldDrawField( field );

    retValue = -1;
    if ( HTTPPost( &target, request, (char *)gTempDBName ) == HTTPErr_OK ) {
        SetTextField( field, "Processing Response" );
        FldDrawField( field );
        if ( ReadWholeFile( gTempDBName, request, INFOREQ_SIZE ) ) {
            if ( ParseBlogInfo( request, &fault, entries, count ) == 0 ) {
                retValue = 0;
            } else {
                FrmCustomAlert( BlogLoadErrAlert, fault.string, NULL, NULL );
            }

        } else {
            FrmCustomAlert( BlogLoadErrAlert, 
                            "Out of memory processing response", NULL, NULL );
        }
    } else {
        FrmCustomAlert( BlogLoadErrAlert, "Unable to contact server", NULL,
                        NULL );
    }

    MemPtrFree( request );

    return retValue;
}


/*
 */

static int RefreshBlogListEntries( void )
{
    BlogEntry *entries;
    int count;
    char **newNames;
    char **newIds;
    int i;

    entries = NULL;
    if ( LoadUserBlogs( &entries, &count ) != 0 ) {
        return -1;
    }

    if ( count == 0 ) {
        return -1;
    }

    newNames = (char **)MemPtrNew( sizeof(char *) * count );
    if ( newNames == NULL ) {
        MemPtrFree( entries );
        return -1;
    }
    for ( i = 0; i < count; i++ ) {
        newNames[i] = NULL;
    }

    newIds = (char **)MemPtrNew( sizeof(char *) * count );
    if ( newIds == NULL ) {
        MemPtrFree( entries );
        MemPtrFree( newNames );
        return -1;
    }
    for ( i = 0; i < count; i++ ) {
        newIds[i] = NULL;
    }

    for ( i = 0; i < count; i++ ) {
        newNames[i] = (char *)MemPtrNew( StrLen( entries[i].name ) + 1 );
        if ( newNames[i] == NULL ) {
            MemPtrFree( entries );
            MemPtrFree( newNames );
            MemPtrFree( newIds );
            return -1;
        }
        newIds[i] = (char *)MemPtrNew( StrLen( entries[i].id ) + 1 );
        if ( newIds[i] == NULL ) {
            MemPtrFree( entries );
            MemPtrFree( newNames );
            MemPtrFree( newIds );
            return -1;
        }
        StrCopy( newNames[i], entries[i].name );
        StrCopy( newIds[i], entries[i].id );
    }

    MemPtrFree( entries );

    FreeBlogListInfo( gBlogListCount );

    gBlogListNames = newNames;
    gBlogListIDs = newIds;
    gBlogListCount = count;

    return 0;
}


/*
 */

static void FreeBlogListInfo( int count )
{
    int i;

    if ( gBlogListNames != NULL ) {
        for ( i = 0; i < count; i++ ) {
            if ( gBlogListNames[i] != NULL ) {
                MemPtrFree( gBlogListNames[i] );
                gBlogListNames[i] = NULL;
            }
        }
        MemPtrFree( gBlogListNames );
        gBlogListNames = NULL;
    }

    if ( gBlogListIDs != NULL ) {
        for ( i = 0; i < count; i++ ) {
            if ( gBlogListIDs[i] != NULL ) {
                MemPtrFree( gBlogListIDs[i] );
                gBlogListIDs[i] = NULL;
            }
        }
        MemPtrFree( gBlogListIDs );
        gBlogListIDs = NULL;
    }

    gBlogListCount = 0;
}


/*
 */

static int LoadBlogsFromPrefs( void )
{
    int i;
    int entries;
    MemHandle dbHandle;
    BlogEntry *rec;

    entries = DmNumRecords( gPrefDBRef );

    gBlogListNames = (char **)MemPtrNew( sizeof(char *) * entries );
    if ( gBlogListNames == NULL ) {
        return -1;
    }
    for ( i = 0; i < entries; i++ ) {
        gBlogListNames[i] = NULL;
    }

    gBlogListIDs = (char **)MemPtrNew( sizeof(char *) * entries );
    if ( gBlogListIDs == NULL ) {
        MemPtrFree( gBlogListNames );
        return -1;
    }
    for ( i = 0; i < entries; i++ ) {
        gBlogListIDs[i] = NULL;
    }

    for ( i = 0; i < entries; i++ ) {
        dbHandle = DmQueryRecord( gPrefDBRef, i );
        if ( dbHandle == NULL ) {
            continue;
        }

        rec = (BlogEntry *)MemHandleLock( dbHandle );
        if ( rec == NULL ) {
            continue;
        }

        gBlogListNames[i] = (char *)MemPtrNew( StrLen( rec->name ) + 1 );
        if ( gBlogListNames[i] == NULL ) {
            FreeBlogListInfo( entries );
            MemHandleUnlock( dbHandle );
            return -1;
        }
        gBlogListIDs[i] = (char *)MemPtrNew( StrLen( rec->id ) + 1 );
        if ( gBlogListIDs[i] == NULL ) {
            FreeBlogListInfo( entries );
            MemHandleUnlock( dbHandle );
            return -1;
        }
        StrCopy( gBlogListNames[i], rec->name );
        StrCopy( gBlogListIDs[i], rec->id );

        MemHandleUnlock( dbHandle );
    }

    gBlogListCount = entries;
    return 0;
}


/*
 */

static void SaveBlogsToPrefs( void )
{
    int numRecs;
    int i;
    UInt16 index;
    UInt32 saveSize;
    MemHandle dbHandle;
    void *rec;

    numRecs = DmNumRecords( gPrefDBRef );
    for ( i = 0; i < numRecs; i++) {
        DmRemoveRecord( gPrefDBRef, 0 );
    }

    saveSize = sizeof(BlogEntry);

    for ( i = 0; i < gBlogListCount; i++ ) {
        index = dmMaxRecordIndex;

        dbHandle = DmNewRecord( gPrefDBRef, &index, saveSize );
        if ( dbHandle == NULL ) {
            return;
        }

        rec = MemHandleLock( dbHandle );
        if ( rec == NULL ) {
            return;
        }

        DmStrCopy( rec, 0, gBlogListNames[i] );
        DmStrCopy( rec, BLOG_NAME_LEN, gBlogListIDs[i] );

        MemHandleUnlock( dbHandle );
        DmReleaseRecord( gPrefDBRef, index, false );
    }
}


/*
 */

static Boolean BlogLoadFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;

    switch ( event->eType ) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();
            FrmDrawForm( frm );
            RefreshBlogListEntries(); /* Check the return here!!! */
            gBlogListReloaded = true;
            FrmReturnToForm( BlogListForm );
            FrmUpdateForm( BlogListForm, frmRedrawUpdateCode );
            handled = true;
            break;

        case frmUpdateEvent:
            FrmDrawForm( FrmGetActiveForm() );
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

static Boolean BlogListFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;
    ListPtr list;
    Int16 selected;
    EventType actionEvent;

    switch ( event->eType ) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();

            list = (ListPtr)GetObjectPtr( frm, BlogList );
            LoadBlogsFromPrefs();
            LstSetListChoices( list, gBlogListNames, gBlogListCount );
            gBlogListReloaded = false;

            // FrmSetFocus( frm, FrmGetObjectIndex( frm, BlogList ) );

            FrmDrawForm( frm );

            break;

        case frmCloseEvent:
            frm = FrmGetActiveForm();
            list = (ListPtr)GetObjectPtr( frm, BlogList );
            LstEraseList( list );
            if ( gBlogListCount != 0 ) {
                FreeBlogListInfo( gBlogListCount );
            }
            break;

        case frmUpdateEvent:
            frm = FrmGetActiveForm();
            list = (ListPtr)GetObjectPtr( frm, BlogList );
            LstSetListChoices( list, gBlogListNames, gBlogListCount );
            FrmDrawForm( FrmGetActiveForm() );
            handled = true;
            break;

        case ctlSelectEvent:
            switch ( event->data.ctlSelect.controlID ) {
                case BlogListOKBtn:
                    frm = FrmGetActiveForm();
                    list = (ListPtr)GetObjectPtr( frm, BlogList );
                    selected = LstGetSelection( list );
                    if ( selected != noListSelection ) {
                        StrCopy( gPrefs.blogID, gBlogListIDs[selected] );
                    }
                    if ( gBlogListReloaded == true ) {
                        SaveBlogsToPrefs();
                    }
                    LstEraseList( list );
                    FreeBlogListInfo( gBlogListCount );
                    FrmGotoForm( IdentityForm );
                    handled = true;
                    break;

                case BlogListRefreshBtn:
                    frm = FrmGetActiveForm();
                    list = (ListPtr)GetObjectPtr( frm, BlogList );

                    FrmPopupForm( BlogLoadForm );
#if 0
                    RefreshBlogListEntries();
                    LstSetListChoices( list, gBlogListNames, gBlogListCount );
                    LstDrawList( list );
                    gBlogListReloaded = true;
#endif /* 0 */

                    handled = true;
                    break;

                case BlogListCancelBtn:
                    frm = FrmGetActiveForm();
                    list = (ListPtr)GetObjectPtr( frm, BlogList );
                    LstEraseList( list );
                    FreeBlogListInfo( gBlogListCount );
                    FrmGotoForm( IdentityForm );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case menuEvent:
            switch ( event->data.menu.itemID ) {
                case ListOKMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = BlogListOKBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                case ListRefreshMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = BlogListRefreshBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                case ListCancelMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = BlogListCancelBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case nilEvent:
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

static void SaveIdentityInfo( void )
{
    FieldPtr field;
    MemHandle fieldHandle;
    char *fieldText;

    field = GetCurrFormObjPtr( IdentUsername );
    fieldHandle = FldGetTextHandle( field );
    if ( fieldHandle != NULL ) {
        fieldText = MemHandleLock( fieldHandle );
        StrNCopy( gPrefs.name, fieldText, NAME_LEN );
        gPrefs.name[NAME_LEN-1] = '\0';
        MemHandleUnlock( fieldHandle );
    }

    field = GetCurrFormObjPtr( IdentPassword );
    fieldHandle = FldGetTextHandle( field );
    if ( fieldHandle != NULL ) {
        fieldText = MemHandleLock( fieldHandle );
        StrNCopy( gPrefs.pass, fieldText, PASS_LEN );
        gPrefs.pass[PASS_LEN-1] = '\0';
        MemHandleUnlock( fieldHandle );
    }

    field = GetCurrFormObjPtr( IdentBlogID );
    fieldHandle = FldGetTextHandle( field );
    if ( fieldHandle != NULL ) {
        fieldText = MemHandleLock( fieldHandle );
        StrNCopy( gPrefs.blogID, fieldText, BLOG_ID_LEN );
        gPrefs.blogID[BLOG_ID_LEN-1] = '\0';
        MemHandleUnlock( fieldHandle );
    }
}


/*
 */

static Boolean PostOptsFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;
    ControlPtr control;
    EventType actionEvent;

    switch (event->eType) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();

            control = GetCurrFormObjPtr( ClearCheck );
            if ( gPrefs.postAction == PA_NOTHING ) {
                CtlSetValue( control, 0 );
            } else {
                CtlSetValue( control, 1 );
            }

            control = GetCurrFormObjPtr( PublishCheck );
            if ( gPrefs.publishFlag == PUB_LATER ) {
                CtlSetValue( control, 0 );
            } else {
                CtlSetValue( control, 1 );
            }

            FrmDrawForm(frm);

            break;

        case frmUpdateEvent:
            FrmDrawForm( FrmGetActiveForm() );
            handled = true;
            break;

        case ctlSelectEvent:
            switch ( event->data.ctlSelect.controlID ) {
                case PostOptsOKBtn:
                    control = GetCurrFormObjPtr( ClearCheck );
                    if ( CtlGetValue( control ) == 0 ) {
                        gPrefs.postAction = PA_NOTHING;
                    } else {
                        gPrefs.postAction = PA_CLEAR_TEXT;
                    }

                    control = GetCurrFormObjPtr( PublishCheck );
                    if ( CtlGetValue( control ) == 0 ) {
                        gPrefs.publishFlag = PUB_LATER;
                    } else {
                        gPrefs.publishFlag = PUB_IMMED;
                    }

                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                case PostOptsCancelBtn:
                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case menuEvent:
            switch ( event->data.menu.itemID ) {
                case PostOptOKMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = PostOptsOKBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                case PostOptCancelMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = PostOptsCancelBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case nilEvent:
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

static Boolean IdentityFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;
    EventType actionEvent;

    switch (event->eType) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();

            if ( gPrefs.name[0] != '\0' ) {
                SetTextField( GetCurrFormObjPtr( IdentUsername ),
                              gPrefs.name );
            }

            if ( gPrefs.pass[0] != '\0' ) {
                SetTextField( GetCurrFormObjPtr( IdentPassword ),
                              gPrefs.pass );
            }

            if ( gPrefs.blogID[0] != '\0' ) {
                SetTextField( GetCurrFormObjPtr( IdentBlogID ),
                              gPrefs.blogID );
            }

            FrmSetFocus( frm, FrmGetObjectIndex( frm, IdentUsername ) );

            FrmDrawForm( frm );

            break;

        case frmUpdateEvent:
            FrmDrawForm(FrmGetActiveForm());
            handled = true;
            break;

        case ctlSelectEvent:
            switch ( event->data.ctlSelect.controlID ) {
                case IdentOKBtn:
                    SaveIdentityInfo();
                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                case IdentListBtn:
                    SaveIdentityInfo();
                    FrmGotoForm( BlogListForm );
                    handled = true;
                    break;

                case IdentCancelBtn:
                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case menuEvent:
            switch ( event->data.menu.itemID ) {
                case IdentOKMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = IdentOKBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                case IdentBlogMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = IdentListBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                case IdentCancelMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = IdentCancelBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case keyDownEvent: 
            switch ( event->data.keyDown.chr ) {
                case vchrNextField:
                    frm = FrmGetActiveForm();
                    switch ( FrmGetObjectId( frm, FrmGetFocus( frm ) ) ) {
                        case IdentUsername:
                            FrmSetFocus( frm,
                                     FrmGetObjectIndex( frm, IdentPassword ) );
                            break;

                        case IdentPassword:
                            FrmSetFocus( frm,
                                       FrmGetObjectIndex( frm, IdentBlogID ) );
                            break;

                        case IdentBlogID:
                            FrmSetFocus( frm,
                                     FrmGetObjectIndex( frm, IdentUsername ) );
                            break;

                        default:
                            break;
                    }
                    handled = true;
                    break;

                case vchrPrevField:
                    frm = FrmGetActiveForm();
                    switch ( FrmGetObjectId( frm, FrmGetFocus( frm ) ) ) {
                        case IdentUsername:
                            FrmSetFocus( frm,
                                     FrmGetObjectIndex( frm, IdentBlogID ) );
                            break;

                        case IdentPassword:
                            FrmSetFocus( frm,
                                     FrmGetObjectIndex( frm, IdentUsername ) );
                            break;

                        case IdentBlogID:
                            FrmSetFocus( frm,
                                     FrmGetObjectIndex( frm, IdentPassword ) );
                            break;

                        default:
                            break;
                    }
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case nilEvent:
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

static Boolean CreateLinkFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;
    FieldPtr field;
    MemHandle fieldHandle;
    char *fieldText;
    EventType actionEvent;

    switch (event->eType) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();

            if ( gLinkText != NULL ) {
                SetTextField( GetCurrFormObjPtr( LinkText ), gLinkText );
                MemPtrFree( gLinkText );
                gLinkText = NULL;
                FrmSetFocus( frm, FrmGetObjectIndex( frm, LinkURL ) );
            } else {
                FrmSetFocus( frm, FrmGetObjectIndex( frm, LinkText ) );
            }

            FrmDrawForm( frm );

            break;

        case frmUpdateEvent:
            FrmDrawForm( FrmGetActiveForm() );
            handled = true;
            break;

        case ctlSelectEvent:
            switch ( event->data.ctlSelect.controlID ) {
                case LinkOKBtn:

                    field = GetCurrFormObjPtr( LinkText );
                    fieldHandle = FldGetTextHandle( field );
                    if ( fieldHandle != NULL ) {
                        fieldText = MemHandleLock( fieldHandle );
                        gLinkText = StrDup( fieldText );
                        MemHandleUnlock( fieldHandle );
                    } else {
                        gLinkText = StrDup( "" );
                    }

                    field = GetCurrFormObjPtr( LinkURL );
                    fieldHandle = FldGetTextHandle( field );
                    if ( fieldHandle != NULL ) {
                        fieldText = MemHandleLock( fieldHandle );
                        gLinkURL = StrDup( fieldText );
                        MemHandleUnlock( fieldHandle );
                    } else {
                        gLinkURL = StrDup( "" );
                    }

                    if ( (gLinkText == NULL) || (gLinkURL == NULL) ) {
                        if ( gLinkText != NULL ) {
                            MemPtrFree( gLinkText );
                            gLinkText = NULL;
                        }
                        if ( gLinkURL != NULL ) {
                            MemPtrFree( gLinkURL );
                            gLinkURL = NULL;
                        }
                    }

                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                case LinkCancelBtn:
                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case menuEvent:
            switch ( event->data.menu.itemID ) {
                case LinkOKMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = LinkOKBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                case LinkCancelMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = LinkCancelBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case keyDownEvent: 
            switch ( event->data.keyDown.chr ) {
                case vchrPrevField:
                case vchrNextField:
                    frm = FrmGetActiveForm();
                    switch ( FrmGetObjectId( frm, FrmGetFocus( frm ) ) ) {
                        case LinkText:
                            FrmSetFocus( frm, 
                                         FrmGetObjectIndex( frm, LinkURL ) );
                            break;

                        case LinkURL:
                            FrmSetFocus( frm, 
                                         FrmGetObjectIndex( frm, LinkText ) );
                            break;

                        default:
                            break;
                    }
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case nilEvent:
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

static Boolean ServerFormHandler( EventType *event )
{
    Boolean handled = false;
    FormType *frm;
    FieldPtr field;
    MemHandle fieldHandle;
    char *fieldText;
    ListPtr list;
    char *typeText;
    ControlPtr popup;
    EventType actionEvent;

    switch ( event->eType ) {

        case frmOpenEvent:
            frm = FrmGetActiveForm();

            if ( gPrefs.host[0] != '\0' ) {
                SetTextField( GetCurrFormObjPtr( ServerHost ),
                              gPrefs.host );
            }

            if ( gPrefs.port[0] != '\0' ) {
                SetTextField( GetCurrFormObjPtr( ServerPort ),
                              gPrefs.port );
            }

            if ( gPrefs.url[0] != '\0' ) {
                SetTextField( GetCurrFormObjPtr( ServerURL ),
                              gPrefs.url );
            }

            if ( gPrefs.timeout[0] != '\0' ) {
                SetTextField( GetCurrFormObjPtr( ServerTimeout ),
                              gPrefs.timeout );
            }

            list = GetCurrFormObjPtr( ServerTypeList );
            LstSetSelection( list, gPrefs.blogType );
            typeText = LstGetSelectionText( list, gPrefs.blogType );
            if ( typeText != NULL ) {
                popup = GetCurrFormObjPtr( ServerType );
                CtlSetLabel( popup, typeText );
            }

            FrmSetFocus( frm, FrmGetObjectIndex( frm, ServerHost ) );

            FrmDrawForm( frm );

            break;

        case frmUpdateEvent:
            FrmDrawForm( FrmGetActiveForm() );
            handled = true;
            break;

        case ctlSelectEvent:
            switch ( event->data.ctlSelect.controlID ) {
                case ServerOKBtn:
                    field = GetCurrFormObjPtr( ServerHost );
                    fieldHandle = FldGetTextHandle( field );
                    if ( fieldHandle != NULL ) {
                        fieldText = MemHandleLock( fieldHandle );
                        StrNCopy( gPrefs.host, fieldText, HOST_LEN );
                        gPrefs.host[HOST_LEN-1] = '\0';
                        MemHandleUnlock( fieldHandle );
                    }

                    field = GetCurrFormObjPtr( ServerPort );
                    fieldHandle = FldGetTextHandle( field );
                    if ( fieldHandle != NULL ) {
                        fieldText = MemHandleLock( fieldHandle );
                        StrNCopy( gPrefs.port, fieldText, PORT_LEN );
                        gPrefs.port[PORT_LEN-1] = '\0';
                        MemHandleUnlock( fieldHandle );
                    }

                    field = GetCurrFormObjPtr( ServerURL );
                    fieldHandle = FldGetTextHandle( field );
                    if ( fieldHandle != NULL ) {
                        fieldText = MemHandleLock( fieldHandle );
                        StrNCopy( gPrefs.url, fieldText, URL_LEN );
                        gPrefs.url[URL_LEN-1] = '\0';
                        MemHandleUnlock( fieldHandle );
                    }

                    field = GetCurrFormObjPtr( ServerTimeout );
                    fieldHandle = FldGetTextHandle( field );
                    if ( fieldHandle != NULL ) {
                        fieldText = MemHandleLock( fieldHandle );
                        StrNCopy( gPrefs.timeout, fieldText, TIMEOUT_LEN );
                        gPrefs.timeout[TIMEOUT_LEN-1] = '\0';
                        MemHandleUnlock( fieldHandle );

                        HTTPLibStop();
                        HTTPLibStart( 'VBlg', StrAToI( gPrefs.timeout ) );
                    }

                    list = GetCurrFormObjPtr( ServerTypeList );

                    gPrefs.blogType = LstGetSelection( list );

                    FrmReturnToForm( FormForType( gSvrFormSaved ) );

                    handled = true;
                    break;

                case ServerDefaultBtn:
                    SetTextField( GetCurrFormObjPtr( ServerHost ),
                                  gDefaultHost );
                    SetTextField( GetCurrFormObjPtr( ServerPort ),
                                  gDefaultPort );
                    SetTextField( GetCurrFormObjPtr( ServerURL ),
                                  gDefaultURL );
                    SetTextField( GetCurrFormObjPtr( ServerTimeout ),
                                  gDefaultTimeout );

                    gPrefs.blogType = BT_BLOGGER;
                    list = GetCurrFormObjPtr( ServerTypeList );
                    LstSetSelection( list, gPrefs.blogType );
                    typeText = LstGetSelectionText( list, gPrefs.blogType );
                    if ( typeText != NULL ) {
                        popup = GetCurrFormObjPtr( ServerType );
                        CtlSetLabel( popup, typeText );
                    }

                    FrmDrawForm( FrmGetActiveForm() );
                    handled = true;
                    break;

                case ServerCancelBtn:
                    FrmReturnToForm( FormForType( gPrefs.blogType ) );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case menuEvent:
            switch ( event->data.menu.itemID ) {
                case ServerOKMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = ServerOKBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                case ServerCancelMItem:
                    MemSet( &actionEvent, sizeof(EventType), 0);
                    actionEvent.eType = ctlSelectEvent;
                    actionEvent.data.ctlSelect.controlID = ServerCancelBtn;
                    EvtAddEventToQueue( &actionEvent );
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case keyDownEvent: 
            switch ( event->data.keyDown.chr ) {
                case vchrNextField:
                    frm = FrmGetActiveForm();
                    switch ( FrmGetObjectId( frm, FrmGetFocus( frm ) ) ) {
                        case ServerHost:
                            FrmSetFocus( frm,
                                       FrmGetObjectIndex( frm, ServerPort ) );
                            break;

                        case ServerPort:
                            FrmSetFocus( frm,
                                       FrmGetObjectIndex( frm, ServerURL ) );
                            break;

                        case ServerURL:
                            FrmSetFocus( frm,
                                     FrmGetObjectIndex( frm, ServerTimeout ) );
                            break;

                        case ServerTimeout:
                            FrmSetFocus( frm,
                                       FrmGetObjectIndex( frm, ServerHost ) );
                            break;

                        default:
                            break;
                    }
                    handled = true;
                    break;

                case vchrPrevField:
                    frm = FrmGetActiveForm();
                    switch ( FrmGetObjectId( frm, FrmGetFocus( frm ) ) ) {
                        case ServerHost:
                            FrmSetFocus( frm, 
                                     FrmGetObjectIndex( frm, ServerTimeout ) );
                            break;

                        case ServerPort:
                            FrmSetFocus( frm, 
                                     FrmGetObjectIndex( frm, ServerHost ) );
                            break;

                        case ServerURL:
                            FrmSetFocus( frm, 
                                     FrmGetObjectIndex( frm, ServerPort ) );
                            break;

                        case ServerTimeout:
                            FrmSetFocus( frm, 
                                     FrmGetObjectIndex( frm, ServerURL ) );
                            break;

                        default:
                            break;
                    }
                    handled = true;
                    break;

                default:
                    break;
            }
            break;

        case nilEvent:
            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


/*
 */

static Boolean ApplicationHandleEvent( EventType *event )
{
    Boolean handled = false;
    UInt16 formID;
    FormType *frm;

    switch ( event->eType ) {

        case frmLoadEvent:
            formID = event->data.frmLoad.formID;
            frm = FrmInitForm( formID );
            FrmSetActiveForm( frm );

            switch ( formID ) {
                case IdentityForm:
                    FrmSetEventHandler( frm, IdentityFormHandler );
                    break;

                case CreateLinkForm:
                    FrmSetEventHandler( frm, CreateLinkFormHandler );
                    break;

                case ServerForm:
                    FrmSetEventHandler( frm, ServerFormHandler );
                    break;

                case MainForm:
                case MinimalForm:
                    FrmSetEventHandler( frm, MainFormHandler );
                    break;

                case PostActionForm:
                    FrmSetEventHandler( frm, PostActionFormHandler );
                    break;

                case PostOptsForm:
                    FrmSetEventHandler( frm, PostOptsFormHandler );
                    break;

                case BlogListForm:
                    FrmSetEventHandler( frm, BlogListFormHandler );
                    break;

                case BlogLoadForm:
                    FrmSetEventHandler( frm, BlogLoadFormHandler );
                    break;

                case AboutForm:
                    FrmSetEventHandler( frm, AboutFormHandler );
                    break;

                default:
                    break;
            }

            handled = true;
            break;

        default:
            break;
    }

    return handled;
}


