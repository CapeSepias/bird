/*
 *	BIRD -- Path Operations
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *	(c) 2000 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/attrs.h"
#include "lib/resource.h"
#include "lib/unaligned.h"
#include "lib/string.h"

struct adata *
as_path_prepend(struct linpool *pool, struct adata *olda, int as)
{
  struct adata *newa;

  if (olda->length && olda->data[0] == 2 && olda->data[1] < 255) /* Starting with sequence => just prepend the AS number */
    {
      newa = lp_alloc(pool, sizeof(struct adata) + olda->length + 2);
      newa->length = olda->length + 2;
      newa->data[0] = 2;
      newa->data[1] = olda->data[1] + 1;
      memcpy(newa->data+4, olda->data+2, olda->length-2);
    }
  else					/* Create new path segment */
    {
      newa = lp_alloc(pool, sizeof(struct adata) + olda->length + 4);
      newa->length = olda->length + 4;
      newa->data[0] = 2;
      newa->data[1] = 1;
      memcpy(newa->data+4, olda->data, olda->length);
    }
  put_u16(newa->data+2, as);
  return newa;
}

void
as_path_format(struct adata *path, byte *buf, unsigned int size)
{
  byte *p = path->data;
  byte *e = p + path->length - 8;
  byte *end = buf + size;
  int sp = 1;
  int l, type, isset, as;

  while (p < e)
    {
      if (buf > end)
	{
	  strcpy(buf, " ...");
	  return;
	}
      isset = (*p++ == AS_PATH_SET);
      l = *p++;
      if (isset)
	{
	  if (!sp)
	    *buf++ = ' ';
	  *buf++ = '{';
	  sp = 0;
	}
      while (l-- && buf <= end)
	{
	  if (!sp)
	    *buf++ = ' ';
	  buf += bsprintf(buf, "%d", get_u16(p));
	  p += 2;
	  sp = 0;
	}
      if (isset)
	{
	  *buf++ = ' ';
	  *buf++ = '}';
	  sp = 0;
	}
    }
  *buf = 0;
}

int
as_path_getlen(struct adata *path)
{
  int res = 0;
  u8 *p = path->data;
  u8 *q = p+path->length;
  int len;

  while (p<q) {
    switch (*p++) {
    case 1: len = *p++; res++;    p += 2*len; break;
    case 2: len = *p++; res+=len; p += 2*len; break;
    default: bug("This should not be in path");
    }
  }
  return res;
}

#define MASK_PLUS do { mask = mask->next; if (!mask) return next == q; \
		       asterix = (mask->val == PM_ANY); \
                       if (asterix) { mask = mask->next; if (!mask) { return 1; } } \
		       } while(0)

int
as_path_match(struct adata *path, struct f_path_mask *mask)
{
  int i;
  int asterix = 0;
  u8 *p = path->data;
  u8 *q = p+path->length;
  int len;
  u8 *next;

  asterix = (mask->val == PM_ANY);
  if (asterix) { mask = mask->next; if (!mask) { return 1; } }

  while (p<q) {
    switch (*p++) {
    case 1:	/* This is a set */
      len = *p++;
      {
	u8 *p_save = p;
	next = p_save + 2*len;
      retry:
	p = p_save;
	for (i=0; i<len; i++) {
	  if (asterix && (get_u16(p) == mask->val)) {
	    MASK_PLUS;
	    goto retry;
	  }
	  if (!asterix && (get_u16(p) == mask->val)) {
	    p = next;
	    MASK_PLUS;
	    goto okay;
	  }
	  p+=2;
	}
	if (!asterix)
	  return 0;
      okay:
      }
      break;

    case 2:	/* This is a sequence */
      len = *p++;
      for (i=0; i<len; i++) {
	next = p+2;
	if (asterix && (get_u16(p) == mask->val))
	  MASK_PLUS;
	else if (!asterix) {
	  if (get_u16(p) != mask->val)
	    return 0;
	  MASK_PLUS;
	}
	p+=2;
      }
      break;

    default:
      bug("This should not be in path");
    }
  }
  return 0;
}