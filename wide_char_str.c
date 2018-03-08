#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"

#include <wchar.h>


#ifdef utf8_to_uvchr_buf
   #define UTF8_TO_UVCHR(src, src_end, ret_len) \
      utf8_to_uvchr_buf(src, src_end, ret_len)
#else /* For compatibility with older perls. */
   #define UTF8_TO_UVCHR(src, src_end, ret_len) \
      utf8_to_uvuni(src, ret_len)
#endif


#ifdef Newxz
   #define NEWXZ(ptr, nitems, type)   Newxz(ptr, nitems, type);
#else /* For compatibility with older perls. */
   #define NEWXZ(ptr, nitems, type)   Newz(0, ptr, nitems, type);
#endif
/**********************************************************************/


#define WCHAR_IS_UTF16   __WCHAR_MAX__ == 0xFFFFu


#define UTF16_OK_BMP              0
#define UTF16_OK_SURROGATE_PAIR   1
#define UTF16_MISSING_HIGH        2
#define UTF16_MISSING_LOW         3
#define UTF16_MISSING_LOW         3


static int
validate_utf16(wchar_t pair[2]) {
   if (
      (pair[0] >= 0xD800u && pair[0] <= 0xDBFFu) &&
      (pair[1] >= 0xDC00u && pair[1] <= 0xDFFFu)
   ) {
      return UTF16_OK_SURROGATE_PAIR;
   }
   else if (
      (pair[0] >= 0xD800u && pair[0] <= 0xDBFFu) &&
      (pair[1] <  0xDC00u || pair[1] >  0xDFFFu)
   ) {
      return UTF16_MISSING_LOW;
   }
   else if (
      (pair[0] <  0xD800u || pair[0] >  0xDBFFu) &&
      (pair[1] >= 0xDC00u && pair[1] <= 0xDFFFu)
   ) {
      return UTF16_MISSING_HIGH;
   }
   else {
      return UTF16_OK_BMP;
   }
}


static void
croak_malformed_utf16(pTHX_ const char *msg, wchar_t pair[2]) {
   Perl_croak(aTHX_
      "Malformed UTF-16: %s (%04X %04X)", msg, pair[0], pair[1]
   );
}


static UV
parse_utf16_chr(pTHX_ wchar_t **src) {
   UV chr;
   
   switch (validate_utf16(*src)) {
      
      /* UTF-16 code unit in the Basic Multilingual Plane. */
      case UTF16_OK_BMP:
         chr = *(*src)++;
         break;
      
      /* UTF-16 surrogate pair; compute the character */
      case UTF16_OK_SURROGATE_PAIR:
         chr = ((*src)[0] - 0xD800u) * 0x400u
               + (*src)[1] - 0xDC00u + 0x10000u;
         
         (*src) += 2;
         break;
      
      /* Malformed UTF-16 surrogate pair (low without high.) */
      case UTF16_MISSING_HIGH:
         croak_malformed_utf16(aTHX_ "Missing high surrogate", *src);
         break;
      
      /* Malformed UTF-16 surrogate pair (high without low.) */
      case UTF16_MISSING_LOW:
         croak_malformed_utf16(aTHX_ "Missing low surrogate", *src);
         break;
   }
   
   return chr;
}
/**********************************************************************/

wchar_t *
sv_to_wide_char_str(pTHX_ SV *src) {
   wchar_t *dest;
   wchar_t *dest_cur;
   
   /* Length of the wchar_t string. */
   /* This is the number of code units before the terminating null. */
   /* This is used for the allocation of the wide character buffer. */
   STRLEN dest_len = 0;
   
   /* Length of a UTF-8 sequence that represents a unicode character. */
   STRLEN utf8_seq_len;
   
   
   /* Work with src as UTF-8. This prevents the need for SvUTF8(src)
      tests and makes for cleaner and simpler code. */
#ifdef utf8_to_uvchr_buf
   STRLEN src_utf8_len;
   U8 *src_utf8 = SvPVutf8(src, src_utf8_len);
   U8 *src_utf8_end = src_utf8 + src_utf8_len;
#else
   U8 *src_utf8 = SvPVutf8_nolen(src);
#endif
   
   /* Determine the size of the wide character buffer. */
   U8 *src_utf8_cur = src_utf8;
   
   while (*src_utf8_cur) {
      UV chr = UTF8_TO_UVCHR(src_utf8_cur, src_utf8_end, &utf8_seq_len);
      
      (WCHAR_IS_UTF16 && chr > 0xFFFFu)
         ? dest_len += 2
         : dest_len++;
      
      src_utf8_cur += utf8_seq_len;
   } printf("{sv_to_wide_char} [dest_len: %d] [sizeof(wchar_t): %d]\n", dest_len, sizeof(wchar_t));
   
   
   /* Allocate the wide character buffer. */
   NEWXZ(dest, dest_len + 1, wchar_t);
   
   /* Prevent leaking if an exception or croaking occurs. */
   SAVEFREEPV(dest);
   
   
   /* Perform the UTF-8 to wide character conversion. */
   dest_cur = dest;
   src_utf8_cur = src_utf8;
   
   while (*src_utf8_cur) {
      UV chr = UTF8_TO_UVCHR(src_utf8_cur, src_utf8_end, &utf8_seq_len);
      
      if (WCHAR_IS_UTF16 && chr > 0xFFFFu) { /* UTF-16. */
         /* Calculate UTF-16 surrogate pair. */
         wchar_t high = ((chr - 0x10000u) / 0x400u) + 0xD800u;
         wchar_t low  = (chr - 0x10000u) % 0x400u + 0xDC00u;
         
         *dest_cur++ = high;
         *dest_cur++ = low;
      }
      else { /* UTF-32, or UTF-16 code unit in the BMP range. */
         *dest_cur++ = chr;
      }
      
      src_utf8_cur += utf8_seq_len;
   }
   
   *dest_cur = 0; /* Terminate the string. */
   
   return dest;
}


SV *
wide_char_str_to_sv(pTHX_ wchar_t *src, SV *dest_sv) {
   U8 *dest;
   U8 *dest_cur;
   
   /* Length of the U8 string. */
   /* This is the number of code units before the terminating null. */
   /* This is used for the allocation of the UTF-8 octet buffer. */
   STRLEN dest_len = 0;
   
   
   /* Determine the size for the UTF-8 buffer. */
   /* Also perform UTF-16 validation here when wchar_t is 16-bit. */
   wchar_t* src_cur = src;
   
   while (*src_cur) {
      UV chr = WCHAR_IS_UTF16
         ? parse_utf16_chr(aTHX_ &src_cur) /* UTF-16. */
         : *src_cur++;                     /* UTF-32. */
      
      U8 utf8_seq[UTF8_MAXBYTES + 1] = {0};
      
      uvchr_to_utf8(utf8_seq, chr);
      dest_len += strlen(utf8_seq);
   } printf("{wide_char_to_sv} [dest_len: %d] [sizeof(U8): %d]\n", dest_len, sizeof(U8));
   
   
   /* Allocate the UTF-8 buffer. */
   /* Was: newx(dest, UTF8_MAXBYTES * wcslen(src) + 1, U8); */
   NEWXZ(dest, dest_len + 1, U8);
   
   /* Prevent leaking if an exception or croaking occurs. */
   SAVEFREEPV(dest);
   
   
   /* Perform the wide character to UTF-8 conversion. */
   src_cur = src;
   dest_cur = dest;
   
   while (*src_cur) {
      UV chr = WCHAR_IS_UTF16
         ? parse_utf16_chr(aTHX_ &src_cur) /* UTF-16. */
         : *src_cur++;                     /* UTF-32. */
      
      dest_cur = uvchr_to_utf8(dest_cur, chr);
   }
   
   *dest_cur = 0; /* Terminate the string. */
   
   sv_setpv((SV*)dest_sv, dest); /*(char*)dest*/
   sv_utf8_decode(dest_sv);
   
   return dest_sv;
}
/**********************************************************************/
