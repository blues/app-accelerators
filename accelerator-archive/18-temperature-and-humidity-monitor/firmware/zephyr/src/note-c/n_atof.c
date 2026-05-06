/*!
 * @file n_atof.c
 *
 * Derived from the "strtod" library procedure to be locale-independent
 *
 * Copyright (c) 1988-1993 The Regents of the University of California.
 * Copyright (c) 1994 Sun Microsystems, Inc.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * RCS: @(#) $Id$
 */

#ifdef HAVE_STDLIB_H
#   include <stdlib.h>
#endif
#include <ctype.h>

#include "n_lib.h"

#ifndef __STDC__
# ifdef __GNUC__
#  define const __const__
# else
#  define const
# endif
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#define MAX_EXPONENT 511    /* Largest possible base 10 exponent.  Any
                                 * exponent larger than this will already
                                 * produce underflow or overflow, so there's
                                 * no need to worry about additional digits.
                                 */

/*
 *----------------------------------------------------------------------
 *
 * atof -- a LOCALE-INDEPENDENT string to floating point
 *
 *      This procedure converts a floating-point number from an ASCII
 *      decimal representation to internal double-precision format.
 *
 * Results:
 *      The return value is the double-precision floating-point
 *      representation of the characters in string.  If endPtr isn't
 *      NULL, then *endPtr is filled in with the address of the
 *      next character after the last one that was part of the
 *      floating-point number.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

JNUMBER
JAtoN(string, endPtr)
const char *string;         /* A decimal ASCII floating-point number,
                                 * optionally preceded by white space.
                                 * Must have form "-I.FE-X", where I is the
                                 * integer part of the mantissa, F is the
                                 * fractional part of the mantissa, and X
                                 * is the exponent.  Either of the signs
                                 * may be "+", "-", or omitted.  Either I
                                 * or F may be omitted, or both.  The decimal
                                 * point isn't necessary unless F is present.
                                 * The "E" may actually be an "e".  E and X
                                 * may both be omitted (but not just one).
                                 */
char **endPtr;              /* If non-NULL, store terminating character's
                                 * address here. */
{
    int sign, expSign = FALSE;
    JNUMBER fraction, dblExp;
    register const char *p;
    register int c;
    int exp = 0;                /* Exponent read from "EX" field. */
    int fracExp = 0;            /* Exponent that derives from the fractional
                                 * part.  Under normal circumstatnces, it is
                                 * the negative of the number of digits in F.
                                 * However, if I is very long, the last digits
                                 * of I get dropped (otherwise a long I with a
                                 * large negative exponent could cause an
                                 * unnecessary overflow on I alone).  In this
                                 * case, fracExp is incremented one for each
                                 * dropped digit. */
    int mantSize;               /* Number of digits in mantissa. */
    int decPt;                  /* Number of mantissa digits BEFORE decimal
                                 * point. */
    const char *pExp;           /* Temporarily holds location of exponent
                                 * in string. */

    /*
     * Strip off leading blanks and check for a sign.
     */

    p = string;
    while (*p == ' ') {
        p += 1;
    }
    if (*p == '-') {
        sign = TRUE;
        p += 1;
    } else {
        if (*p == '+') {
            p += 1;
        }
        sign = FALSE;
    }

    /*
     * Count the number of digits in the mantissa (including the decimal
     * point), and also locate the decimal point.
     */

    decPt = -1;
    for (mantSize = 0; ; mantSize += 1) {
        c = *p;
        if (c < '0' || c > '9') {
            if ((c != '.') || (decPt >= 0)) {
                break;
            }
            decPt = mantSize;
        }
        p += 1;
    }

    /*
     * Now suck up the digits in the mantissa.  Use two integers to
     * collect 9 digits each (this is faster than using floating-point).
     * If the mantissa has more than 18 digits, ignore the extras, since
     * they can't affect the value anyway.
     */

    pExp  = p;
    p -= mantSize;
    if (decPt < 0) {
        decPt = mantSize;
    } else {
        mantSize -= 1;                  /* One of the digits was the point. */
    }
    if (mantSize > 18) {
        fracExp = decPt - 18;
        mantSize = 18;
    } else {
        fracExp = decPt - mantSize;
    }
    if (mantSize == 0) {
        fraction = 0.0;
        p = string;
        goto done;
    } else {
        long frac1, frac2;
        frac1 = 0L;
        for ( ; mantSize > 9; mantSize -= 1) {
            c = *p;
            p += 1;
            if (c == '.') {
                c = *p;
                p += 1;
            }
            frac1 = 10*frac1 + (c - '0');
        }
        frac2 = 0L;
        for (; mantSize > 0; mantSize -= 1) {
            c = *p;
            p += 1;
            if (c == '.') {
                c = *p;
                p += 1;
            }
            frac2 = 10*frac2 + (c - '0');
        }
        fraction = (1.0e9 * frac1) + frac2;
    }

    /*
     * Skim off the exponent.
     */

    p = pExp;
    if ((*p == 'E') || (*p == 'e')) {
        p += 1;
        if (*p == '-') {
            expSign = TRUE;
            p += 1;
        } else {
            if (*p == '+') {
                p += 1;
            }
            expSign = FALSE;
        }
        while (*p >= '0' && *p <= '9') {
            exp = exp * 10 + (*p - '0');
            p += 1;
        }
    }
    if (expSign) {
        exp = fracExp - exp;
    } else {
        exp = fracExp + exp;
    }

    /*
     * Generate a floating-point number that represents the exponent.
     * Do this by processing the exponent one bit at a time to combine
     * many powers of 2 of 10. Then combine the exponent with the
     * fraction.
     */

    if (exp < 0) {
        expSign = TRUE;
        exp = -exp;
    } else {
        expSign = FALSE;
    }
    if (exp > MAX_EXPONENT) {
        exp = MAX_EXPONENT;
    }
    dblExp = 1.0;
    int d;
    for (d = 0; exp != 0; exp >>= 1, d += 1) {
        /* Table giving binary powers of 10.  Entry */
        /* is 10^2^i.  Used to convert decimal */
        /* exponents into floating-point numbers. */
        JNUMBER p10 = 0.0;
        switch (d) {
        case 0:
            p10 = 10.0;
            break;
        case 1:
            p10 = 100.0;
            break;
        case 2:
            p10 = 1.0e4;
            break;
        case 3:
            p10 = 1.0e8;
            break;
        case 4:
            p10 = 1.0e16;
            break;
        case 5:
            p10 = 1.0e32;
            break;
#ifndef NOTE_FLOAT
        case 6:
            p10 = 1.0e64;
            break;
        case 7:
            p10 = 1.0e128;
            break;
        case 8:
            p10 = 1.0e256;
            break;
#endif
        }
        if (p10 == 0.0) {
            break;
        }
        if (exp & 01) {
            dblExp *= p10;
        }
    }
    if (expSign) {
        fraction /= dblExp;
    } else {
        fraction *= dblExp;
    }

done:
    if (endPtr != NULL) {
        *endPtr = (char *) p;
    }

    if (sign) {
        return -fraction;
    }
    return fraction;
}
