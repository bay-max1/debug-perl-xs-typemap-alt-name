#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"


#include "wide_char_str.h"


MODULE = Test1		PACKAGE = Test1		

PROTOTYPES: DISABLE


wchar_t * test_wide(buf)
   wchar_t * buf
   
   CODE:
      int len = wcslen(buf);
      int sz = (len + 1) * sizeof(buf[0]);
      int i;
      
      //buf[0] = 0xD801; // Malformed UTF-16 test - high without low
      //buf[1] = 0xDC01; // Malformed UTF-16 test - low without high
      
      printf("[buf: %ls]\n", buf);
      
      printf("[dec:"); for (i=0; i<sz; i++) {
         U8 byte = ((U8*)buf)[i];
         printf(" %3u", byte);
      }
      printf("]\n");
      
      printf("[hex:"); for (i=0; i<sz; i++) {
         U8 byte = ((U8*)buf)[i];
         printf("  %02X", byte);
      }
      printf("]\n");
      
      RETVAL = buf;
   
   OUTPUT:
      RETVAL

