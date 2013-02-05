/* tag: http protocol header file for PalmHTTP
 * arch-tag: http protocol header file for PalmHTTP
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

#if !defined(PALMHTTP_H_)
#define PALMHTTP_H_ 1

#include <PalmOS.h>


typedef struct URLTarget_struct {
    char *host;
    UInt16 port;
    char *path;
} URLTarget;


typedef enum HTTPErr_enum {
    HTTPErr_OK = 0,
    HTTPErr_ConnectError = 1,
    HTTPErr_TempDBErr = 2,
    HTTPErr_SizeMismatch = 3,
} HTTPErr;


int HTTPLibStart( UInt32 creator, int secTimeout );
void HTTPLibStop( void );
HTTPErr HTTPPost( URLTarget *url, char *data, char *resultsDB );
HTTPErr HTTPGet( URLTarget *url, char *resultsDB );


#endif /* PALMHTTP_H_ */


