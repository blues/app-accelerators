/*
 * Modified by Dave Hart for integration into NTP 4.2.7 <hart@ntp.org>
 *
 * Copyright (c) 1995 Patrick Powell.
 *
 * This code is based on code written by Patrick Powell <papowell@astart.com>.
 * It may be used for any purpose as long as this notice remains intact on all
 * source code distributions.
 *
 * Copyright (c) 2008 Holger Weiss.
 *
 * This version of the code is maintained by Holger Weiss <holger@jhweiss.de>.
 * My changes to the code may freely be used, modified and/or redistributed for
 * any purpose.  It would be nice if additions and fixes to this file (including
 * trivial code cleanups) would be sent back in order to let me include them in
 * the version available at <http://www.jhweiss.de/software/snprintf.html>.
 * However, this is not a requirement for using or redistributing (possibly
 * modified) versions of this file, nor is leaving this notice intact mandatory.
 */


#include "n_lib.h"
#include <stdint.h>
#include <math.h>

#define	PRINT_F_QUOTE		0x0001
#define	PRINT_F_TYPE_E		0x0002
#define	PRINT_F_TYPE_G		0x0004
#define	PRINT_F_NUM			0x0008
#define	PRINT_F_PLUS		0x0010
#define	PRINT_F_MINUS		0x0020
#define	PRINT_F_ZERO		0x0040
#define	PRINT_F_SPACE		0x0080
#define	PRINT_F_UP			0x0100

static void fmtstr(char *, size_t *, size_t, const char *, int, int, int);
static void fmtflt(char *, size_t *, size_t, JNUMBER, int, int, int, int *);
static void printsep(char *, size_t *, size_t);
static int getnumsep(int);
static int getexponent(JNUMBER);
static int convert(uintmax_t, char *, size_t, int, int);
static uintmax_t cast(JNUMBER);
static uintmax_t myround(JNUMBER);
static JNUMBER mypow10(int);
#define OUTCHAR(str, len, size, ch) \
do { \
	if (len + 1 < size) \
		str[len] = ch; \
	(len)++; \
} while (0)

// Convert a JNUMBER into a null-terminated text string.  Note that buf must
// be pointing at a buffer of JNTOA_MAX length, which is defined so that it
// includes enough space for the null terminator, so there's no need to
// have a buffer of JNTOA_MAX+1.
char * JNtoA(JNUMBER f, char * buf, int precision)
{
    int overflow = 0;
    size_t len = 0;
    int flags = PRINT_F_TYPE_G;
    if (precision < 0) {
        precision = JNTOA_PRECISION;
    }
    fmtflt(buf, &len, JNTOA_MAX, f, -1, precision, flags, &overflow);
    if (overflow) {
        strcpy(buf, "*");
    }
    buf[len] = '\0';
    return buf;
}

static void
fmtflt(char *str, size_t *len, size_t size, JNUMBER fvalue, int width,
       int precision, int flags, int *overflow)
{
    JNUMBER ufvalue;
    uintmax_t intpart;
    uintmax_t fracpart;
    uintmax_t mask;
    const char *infnan = NULL;
    char iconvert[JNTOA_MAX];
    char fconvert[JNTOA_MAX];
    char econvert[4];	/* "e-12" (without nul-termination). */
    char esign = 0;
    char sign = 0;
    int leadfraczeros = 0;
    int exponent = 0;
    int emitpoint = 0;
    int omitzeros = 0;
    int omitcount = 0;
    int padlen = 0;
    int epos = 0;
    int fpos = 0;
    int ipos = 0;
    int separators = (flags & PRINT_F_QUOTE);
    int estyle = (flags & PRINT_F_TYPE_E);

    /*
     * AIX' man page says the default is 0, but C99 and at least Solaris'
     * and NetBSD's man pages say the default is 6, and sprintf(3) on AIX
     * defaults to 6.
     */
    if (precision == -1) {
        precision = 6;
    }

    if (fvalue < 0.0) {
        sign = '-';
    } else if (flags & PRINT_F_PLUS) {	/* Do a sign. */
        sign = '+';
    } else if (flags & PRINT_F_SPACE) {
        sign = ' ';
    }

    if (isnan(fvalue)) {
        infnan = (flags & PRINT_F_UP) ? "NAN" : "nan";
    } else if (isinf(fvalue)) {
        infnan = (flags & PRINT_F_UP) ? "INF" : "inf";
    }

    if (infnan != NULL) {
        if (sign != 0) {
            iconvert[ipos++] = sign;
        }
        while (*infnan != '\0') {
            iconvert[ipos++] = *infnan++;
        }
        fmtstr(str, len, size, iconvert, width, ipos, flags);
        return;
    }

    /* "%e" (or "%E") or "%g" (or "%G") conversion. */
    if (flags & PRINT_F_TYPE_E || flags & PRINT_F_TYPE_G) {
        if (flags & PRINT_F_TYPE_G) {
            /*
             * For "%g" (and "%G") conversions, the precision
             * specifies the number of significant digits, which
             * includes the digits in the integer part.  The
             * conversion will or will not be using "e-style" (like
             * "%e" or "%E" conversions) depending on the precision
             * and on the exponent.  However, the exponent can be
             * affected by rounding the converted value, so we'll
             * leave this decision for later.  Until then, we'll
             * assume that we're going to do an "e-style" conversion
             * (in order to get the exponent calculated).  For
             * "e-style", the precision must be decremented by one.
             */
            precision--;
            /*
             * For "%g" (and "%G") conversions, trailing zeros are
             * removed from the fractional portion of the result
             * unless the "#" flag was specified.
             */
            if (!(flags & PRINT_F_NUM)) {
                omitzeros = 1;
            }
        }
        exponent = getexponent(fvalue);
        estyle = 1;
    }

again:
    /*
     * Sorry, we only support 9, 19, or 38 digits (that is, the number of
     * digits of the 32-bit, the 64-bit, or the 128-bit UINTMAX_MAX value
     * minus one) past the decimal point due to our conversion method.
     */
    switch (sizeof(uintmax_t)) {
    case 16:
        if (precision > 38) {
            precision = 38;
        }
        break;
    case 8:
        if (precision > 19) {
            precision = 19;
        }
        break;
    default:
        if (precision > 9) {
            precision = 9;
        }
        break;
    }

    ufvalue = (fvalue >= 0.0) ? fvalue : -fvalue;
    if (estyle) {	/* We want exactly one integer digit. */
        ufvalue /= mypow10(exponent);
    }

    if ((intpart = cast(ufvalue)) == UINTMAX_MAX) {
        *overflow = 1;
        return;
    }

    /*
     * Factor of ten with the number of digits needed for the fractional
     * part.  For example, if the precision is 3, the mask will be 1000.
     */
    mask = (uintmax_t)mypow10(precision);
    /*
     * We "cheat" by converting the fractional part to integer by
     * multiplying by a factor of ten.
     */
    if ((fracpart = myround(mask * (ufvalue - intpart))) >= mask) {
        /*
         * For example, ufvalue = 2.99962, intpart = 2, and mask = 1000
         * (because precision = 3).  Now, myround(1000 * 0.99962) will
         * return 1000.  So, the integer part must be incremented by one
         * and the fractional part must be set to zero.
         */
        intpart++;
        fracpart = 0;
        if (estyle && intpart == 10) {
            /*
             * The value was rounded up to ten, but we only want one
             * integer digit if using "e-style".  So, the integer
             * part must be set to one and the exponent must be
             * incremented by one.
             */
            intpart = 1;
            exponent++;
        }
    }

    /*
     * Now that we know the real exponent, we can check whether or not to
     * use "e-style" for "%g" (and "%G") conversions.  If we don't need
     * "e-style", the precision must be adjusted and the integer and
     * fractional parts must be recalculated from the original value.
     *
     * C99 says: "Let P equal the precision if nonzero, 6 if the precision
     * is omitted, or 1 if the precision is zero.  Then, if a conversion
     * with style `E' would have an exponent of X:
     *
     * - if P > X >= -4, the conversion is with style `f' (or `F') and
     *   precision P - (X + 1).
     *
     * - otherwise, the conversion is with style `e' (or `E') and precision
     *   P - 1." (7.19.6.1, 8)
     *
     * Note that we had decremented the precision by one.
     */
    if (flags & PRINT_F_TYPE_G && estyle &&
            precision + 1 > exponent && exponent >= -4) {
        precision -= exponent;
        estyle = 0;
        goto again;
    }

    if (estyle) {
        if (exponent < 0) {
            exponent = -exponent;
            esign = '-';
        } else {
            esign = '+';
        }

        /*
         * Convert the exponent.  The sizeof(econvert) is 4.  So, the
         * econvert buffer can hold e.g. "e+99" and "e-99".  We don't
         * support an exponent which contains more than two digits.
         * Therefore, the following stores are safe.
         */
        epos = convert(exponent, econvert, 2, 10, 0);
        /*
         * C99 says: "The exponent always contains at least two digits,
         * and only as many more digits as necessary to represent the
         * exponent." (7.19.6.1, 8)
         */
        if (epos == 1) {
            econvert[epos++] = '0';
        }
        econvert[epos++] = esign;
        econvert[epos++] = (flags & PRINT_F_UP) ? 'E' : 'e';
    }

    /* Convert the integer part and the fractional part. */
    ipos = convert(intpart, iconvert, sizeof(iconvert), 10, 0);
    if (fracpart != 0) {	/* convert() would return 1 if fracpart == 0. */
        fpos = convert(fracpart, fconvert, sizeof(fconvert), 10, 0);
    }

    leadfraczeros = precision - fpos;

    if (omitzeros) {
        if (fpos > 0)	/* Omit trailing fractional part zeros. */
            while (omitcount < fpos && fconvert[omitcount] == '0') {
                omitcount++;
            }
        else {	/* The fractional part is zero, omit it completely. */
            omitcount = precision;
            leadfraczeros = 0;
        }
        precision -= omitcount;
    }

    /*
     * Print a decimal point if either the fractional part is non-zero
     * and/or the "#" flag was specified.
     */
    if (precision > 0 || flags & PRINT_F_NUM) {
        emitpoint = 1;
    }
    if (separators) {	/* Get the number of group separators we'll print. */
        separators = getnumsep(ipos);
    }

    padlen = width                  /* Minimum field width. */
             - ipos                      /* Number of integer digits. */
             - epos                      /* Number of exponent characters. */
             - precision                 /* Number of fractional digits. */
             - separators                /* Number of group separators. */
             - (emitpoint ? 1 : 0)       /* Will we print a decimal point? */
             - ((sign != 0) ? 1 : 0);    /* Will we print a sign character? */

    if (padlen < 0) {
        padlen = 0;
    }

    /*
     * C99 says: "If the `0' and `-' flags both appear, the `0' flag is
     * ignored." (7.19.6.1, 6)
     */
    if (flags & PRINT_F_MINUS) {	/* Left justifty. */
        padlen = -padlen;
    } else if (flags & PRINT_F_ZERO && padlen > 0) {
        if (sign != 0) {	/* Sign. */
            OUTCHAR(str, *len, size, sign);
            sign = 0;
        }
        while (padlen > 0) {	/* Leading zeros. */
            OUTCHAR(str, *len, size, '0');
            padlen--;
        }
    }
    while (padlen > 0) {	/* Leading spaces. */
        OUTCHAR(str, *len, size, ' ');
        padlen--;
    }
    if (sign != 0) {	/* Sign. */
        OUTCHAR(str, *len, size, sign);
    }
    while (ipos > 0) {	/* Integer part. */
        ipos--;
        OUTCHAR(str, *len, size, iconvert[ipos]);
        if (separators > 0 && ipos > 0 && ipos % 3 == 0) {
            printsep(str, len, size);
        }
    }
    if (emitpoint) {	/* Decimal point. */
        OUTCHAR(str, *len, size, '.');
    }
    while (leadfraczeros > 0) {	/* Leading fractional part zeros. */
        OUTCHAR(str, *len, size, '0');
        leadfraczeros--;
    }
    while (fpos > omitcount) {	/* The remaining fractional part. */
        fpos--;
        OUTCHAR(str, *len, size, fconvert[fpos]);
    }
    while (epos > 0) {	/* Exponent. */
        epos--;
        OUTCHAR(str, *len, size, econvert[epos]);
    }
    while (padlen < 0) {	/* Trailing spaces. */
        OUTCHAR(str, *len, size, ' ');
        padlen++;
    }
}

static void fmtstr(char *str, size_t *len, size_t size, const char *value, int width,
                   int precision, int flags)
{
    int padlen, strln;	/* Amount to pad. */
    int noprecision = (precision == -1);

    if (value == NULL) {	/* We're forgiving. */
        value = "(null)";
    }

    /* If a precision was specified, don't read the string past it. */
    for (strln = 0; value[strln] != '\0' &&
            (noprecision || strln < precision); strln++) {
        continue;
    }

    if ((padlen = width - strln) < 0) {
        padlen = 0;
    }
    if (flags & PRINT_F_MINUS) {	/* Left justify. */
        padlen = -padlen;
    }

    while (padlen > 0) {	/* Leading spaces. */
        OUTCHAR(str, *len, size, ' ');
        padlen--;
    }
    while (*value != '\0' && (noprecision || precision-- > 0)) {
        OUTCHAR(str, *len, size, *value);
        value++;
    }
    while (padlen < 0) {	/* Trailing spaces. */
        OUTCHAR(str, *len, size, ' ');
        padlen++;
    }
}

static void printsep(char *str, size_t *len, size_t size)
{
    OUTCHAR(str, *len, size, ',');
}

static int getnumsep(int digits)
{
    int separators = (digits - ((digits % 3 == 0) ? 1 : 0)) / 3;
    return separators;
}

static int getexponent(JNUMBER value)
{
    JNUMBER tmp = (value >= 0.0) ? value : -value;
    int exponent = 0;

    /*
     * We check for 99 > exponent > -99 in order to work around possible
     * endless loops which could happen (at least) in the second loop (at
     * least) if we're called with an infinite value.  However, we checked
     * for infinity before calling this function using our ISINF() macro, so
     * this might be somewhat paranoid.
     */
    while (tmp < 1.0 && tmp > 0.0 && --exponent > -99) {
        tmp *= 10;
    }
    while (tmp >= 10.0 && ++exponent < 99) {
        tmp /= 10;
    }

    return exponent;
}

static int convert(uintmax_t value, char *buf, size_t size, int base, int caps)
{
    const char *digits = caps ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t pos = 0;

    /* We return an unterminated buffer with the digits in reverse order. */
    do {
        buf[pos++] = digits[value % base];
        value /= base;
    } while (value != 0 && pos < size);

    return (int)pos;
}

static uintmax_t cast(JNUMBER value)
{
    uintmax_t result;

    /*
     * We check for ">=" and not for ">" because if UINTMAX_MAX cannot be
     * represented exactly as an JNUMBER value (but is less than LDBL_MAX),
     * it may be increased to the nearest higher representable value for the
     * comparison (cf. C99: 6.3.1.4, 2).  It might then equal the JNUMBER
     * value although converting the latter to uintmax_t would overflow.
     */
    if (value >= UINTMAX_MAX) {
        return UINTMAX_MAX;
    }

    result = (uintmax_t)value;
    /*
     * At least on NetBSD/sparc64 3.0.2 and 4.99.30, casting long double to
     * an integer type converts e.g. 1.9 to 2 instead of 1 (which violates
     * the standard).  Sigh.
     */
    return (result <= value) ? result : result - 1;
}

static uintmax_t myround(JNUMBER value)
{
    uintmax_t intpart = cast(value);

    if (intpart == UINTMAX_MAX) {
        return UINTMAX_MAX;
    }

    return ((value -= intpart) < 0.5) ? intpart : intpart + 1;
}

static JNUMBER mypow10(int exponent)
{
    JNUMBER result = 1;

    while (exponent > 0) {
        result *= 10;
        exponent--;
    }
    while (exponent < 0) {
        result /= 10;
        exponent++;
    }
    return result;
}
