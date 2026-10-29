/*
 * Diary Server client: HTTP/1.0 over Winsock.
 * See PROTOCOL.md in the Diary-Server repository for the requests.
 */
#ifndef DIARY_NET_H
#define DIARY_NET_H

#include <windows.h>

typedef struct {
    int status;             /* HTTP status, e.g. 200 */
    char version[16];       /* the X-Diary-Version header, "" if none */
    char *body;             /* NUL-terminated; free with HttpFree() */
    long length;
} HttpResponse;

/* Sets the server as "host" or "host:port", and the key ("" for none). */
void NetSetServer(const char *address, const char *key);

/* Sends one request and waits for the reply. path is e.g. "/entries" or
   "/search?q=word"; "charset=windows-1252" is added. base, if not NULL, is
   sent as X-Diary-Base. Returns FALSE with a message in error if the
   server couldn't be reached; HTTP errors such as 404 come back in status. */
BOOL HttpRequest(const char *method, const char *path,
                 const char *body, long bodyLength, const char *base,
                 HttpResponse *response, char *error, int errorSize);

void HttpFree(HttpResponse *response);

/* Percent-encodes text for a query parameter. */
void NetEncode(const char *text, char *out, int outSize);

/* Shuts Winsock down if it was started. */
void NetCleanup(void);

#endif
