#include "lab_pan.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    const char *ok="HTTP/1.1 200 OK\r\n";
    for(unsigned n=0;n<12;n++) assert(lab_pan_http_status(ok,n)==-1);
    assert(lab_pan_http_status(ok,strlen(ok))==200);
    assert(lab_pan_http_status("HTTP/1.0 204\r\n",14)==204);
    assert(lab_pan_http_status("HTTP/1.1 302 Found\r\n",20)==302);
    assert(lab_pan_http_status("HTTP/1.1 503 Busy\r\n",19)==503);
    assert(lab_pan_http_status("HTTP/1.1 2000\r\n",15)==-1);
    assert(lab_pan_http_status("HTTP/2.0 200 OK",15)==-1);
    assert(lab_pan_http_status("HTTP/1.1 2O0 OK",15)==-1);
    assert(lab_pan_http_status("HTTP/1.1 600 OK",15)==-1);
    puts("PASS: PAN HTTP status parsing, partial and malformed responses");
}
