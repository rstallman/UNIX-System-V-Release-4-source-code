/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
**	High resolution version of strftime.c
**
 */
#ident	"@(#)libc-port:gen/hrtstrftime.c	1.2"

#ifdef __STDC__
	#pragma weak hrtstrftime = _hrtstrftime
#endif
#include	"synonyms.h"
#include	<fcntl.h>
#include	<time.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<locale.h>
#include	<string.h>
#include	<stddef.h>
#include	<ctype.h>
#include 	<sys/dl.h>
#include 	<sys/evecb.h>
#include 	<sys/hrtcntl.h>
#include 	<osfcn.h>
#include 	<stdlib.h>
#include	"_locale.h"

#define	LC_NAMELEN	15

extern	char	_cur_locale[LC_ALL][LC_NAMELEN];
static void  settime();
static char *itoa();
static char *gettz();
static char *tz();
enum {
	   aJan, aFeb, aMar, aApr, aMay, aJun, aJul, aAug, aSep, aOct, aNov, aDec,
	   Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec,
	   aSun, aMon, aTue, aWed, aThu, aFri, aSat,
	   Sun, Mon, Tue, Wed, Thu, Fri, Sat,
	   Local_time, Local_date, DFL_FMT,
	   AM, PM,
	   LAST
};
static char * _time[] = {
	"Jan","Feb","Mar","Apr","May","Jun","Jul", "Aug", "Sep","Oct", "Nov", "Dec",
	"January", "February", "March","April",
	"May","June", "July", "August", "September",
	"October", "November", "December",
	"Sun","Mon", "Tue", "Wed","Thu", "Fri","Sat",
	"Sunday","Monday","Tuesday","Wednesday", "Thursday","Friday","Saturday",
	"%H:%M:%S","%m/%d/%y", "%a %b %e %T %Z %Y",
	"AM", "PM", NULL
};


size_t
hrtstrftime(s, maxsize, format, tm, rem, res)
char *s;
size_t maxsize;
const char *format;
const struct tm *tm;
ulong rem;
ulong res;
{
	register char	*p;
	register int	i;
	register char	c;
	ulong		result = 1;
	size_t		size = 0;
	char		nstr[30];
	size_t		n;
	dl_t	llog;
	dl_t	lfrac;
	dl_t	lres;
	dl_t	lrem;
	static char	dflcase[] = "%?";

	/*
         * Verify that the requested resolution is legal
         */
	if (res <= 0 || rem < 0 || res > NANOSEC || rem >= res)
		return(-1);

	lrem.dl_hop = 0;
	lrem.dl_lop = rem;
	lres.dl_hop = 0;
	lres.dl_lop = res;
	llog = llog10(lres);

	for(i = 1; i <= llog.dl_lop; ++i)
		result = result * 10;
	if (result != res)	
		llog.dl_lop++;

	lfrac = ldivide(lmul(lrem, lexp10(llog)), lres);

	settime();

	/* Set format string, if not already set */
	if (format == NULL)
		format = _time[DFL_FMT];

	/* Build date string by parsing format string */
	while ((c = *format++) != '\0') {
		if (c != '%') {
			if (++size >= maxsize)
				return(0);
			*s++ = c;
			continue;
		}
		switch (*format++) {
		case '%':	/* Percent sign */
			p = "%";
			break;
		case 'a':	/* Abbreviated weekday name */
			p = _time[aSun + tm->tm_wday];
			break;
		case 'A':	/* Weekday name */
			p = _time[Sun + tm->tm_wday];
			break;
		case 'b':	/* Abbreviated month name */
		case 'h':
			p = _time[aJan + tm->tm_mon];
			break;
		case 'B':	/* Month name */
			p = _time[Jan + tm->tm_mon];
			break;
		case 'c':	/* Localized date & time format */
			p = _time[DFL_FMT];
			goto recur;
		case 'd':	/* Day number */
			p = itoa(tm->tm_mday, nstr, 2);
			break;
		case 'D':
			p = "%m/%d/%y";
			goto recur;
		case 'e':
			p = itoa(tm->tm_mday, nstr, 2);
			if (tm->tm_mday < 10)
				p[0] = ' ';
			break;
		case 'H':	/* Hour (24 hour version) */
			p = itoa(tm->tm_hour, nstr, 2);
			break;
		case 'I':	/* Hour (12 hour version) */
			if ((i = tm->tm_hour % 12) == 0)
				i = 12;
			p = itoa(i, nstr, 2);
			break;
		case 'j':	/* Julian date */
			p = itoa(tm->tm_yday + 1, nstr, 3);
			break;
		case 'm':	/* Month number */
			p = itoa(tm->tm_mon + 1, nstr, 2);
			break;
		case 'M':	/* Minute */
			p = itoa(tm->tm_min, nstr, 2);
			break;
		case 'n':	/* Newline */
			p = "\n";
			break;
		case 'p':	/* AM or PM */
			if (tm->tm_hour >= 12)
				p = _time[PM];
			else
				p = _time[AM];
			break;
		case 'r':
			p = "%I:%M:%S %p";
			goto recur;
		case 'R':
			p = "%H:%M";
			goto recur;
		case 'S':	/* Seconds */
			p = itoa(tm->tm_sec, nstr, 2);
			if (res == 1)
				break;
			n = strlen(p);
			if ((size += n) >= maxsize)
				return(0);
			(void)strcpy(s, p);
			s += n;
			p = nstr;
			sprintf(p , ".%0*d", llog.dl_lop, lfrac.dl_lop);
			break;
		case 't':	/* Tab */
			p = "\t";
			break;
		case 'T':
			p = "%H:%M:%S";
			goto recur;
		case 'U':	/* Week number of year, taking Sunday as
				 * the first day of the week */
			p = itoa((tm->tm_yday - tm->tm_wday + 7)/7, nstr, 2);
			break;
		case 'w':	/* Weekday number */
			p = itoa(tm->tm_wday, nstr, 1);
			break;
		case 'W':	/* Week number of year, taking Monday as
				 * first day of week */
			if ((i = 8 - tm->tm_wday) == 8)
				i = 1;
			p = itoa((tm->tm_yday + i)/7, nstr, 2);
			break;
		case 'x':	/* Localized date format */
			p = _time[Local_date];
			goto recur;
		case 'X':	/* Localized time format */
			p = _time[Local_time];
			goto recur;
		case 'y':	/* Year in the form yy */
			p = itoa(tm->tm_year, nstr, 2);
			break;
		case 'Y':	/* Year in the form ccyy */
			p = itoa(1900 + tm->tm_year, nstr, 4);
			break;
		case 'Z':	/* Timezone */
			p = gettz(tm->tm_isdst);
			break;
		default:
			dflcase[1] = *(format - 1);
			p = dflcase;
			break;
		recur:;
			if ((n = hrtstrftime(s, maxsize-size, p, tm, rem, res)) == 0)
				return(0);
			s += n;
			size += n;
			continue;
		}
		n = strlen(p);
		if ((size += n) >= maxsize)
			return(0);
		(void)strcpy(s, p);
		s += n;
	}
	*s = '\0';
	return(size);
}

static char *
itoa(i, ptr, dig)
register int i;
register char *ptr;
register int dig;
{
	ptr += dig;
	*ptr = '\0';
	while (--dig >= 0) {
		*(--ptr) = i % 10 + '0';
		i /= 10;
	}
	return(ptr);
}

static char saved_locale[LC_NAMELEN] = "C";

static void settime()
{
	register char *p;
	register int  j;
	char *locale;
	char *my_time[LAST];
	static char *ostr = (char *)0;
	char *str;
	int  fd;
	struct stat buf;

	locale = _cur_locale[LC_TIME];

	if (strcmp(locale, saved_locale) == 0)
		return;

	if ( (fd = open(_fullocale(locale, "LC_TIME"), O_RDONLY)) == -1)
		goto err1;

	if( (fstat(fd, &buf)) != 0 || (str = malloc(buf.st_size + 2)) == NULL)
		goto err2;

	if ( (read(fd, str, buf.st_size)) != buf.st_size)
		goto err3;

	/* Set last character of str to '\0' */
	p = &str[buf.st_size];
	p[0] = '\n';
	p[1] = '\0';

	/* p will "walk thru" str */
	p = str;  	

	j = -1;
	while (*p != '\0')
	{ 
		/* "Look for a newline, i.e. end of sub-string
		 * and  change it to a '\0'. If LAST pointers
		 * have been set in my_time, but the newline hasn't been seen
		 * yet, keep going thru the string leaving my_time alone.
		 */
		if (++j < LAST) 
			my_time[j] = p;
		p = strchr(p,'\n');
		*p++ = '\0';
	}
	if (j == LAST)
	{
		memcpy(_time, my_time, sizeof(my_time)); 
		strcpy(saved_locale, locale);
		if (ostr != 0)	 /* free the previoulsy allocated local array*/
			free(ostr);
		ostr = str;
		(void)close(fd);
		return;
	}

err3:	free(str);
err2:	(void)close(fd);
err1:	(void)strcpy(_cur_locale[LC_TIME], saved_locale);
	return;
}

#define MAXTZNAME	3

static char *
gettz(isdst)
int isdst;
{
	static char	tznm[MAXTZNAME + 1];
	register char	*p;

	if ((p = getenv("TZ")) == 0 || *p == '\0')
		return(isdst ? "" : "GMT");
	p = tz(p, tznm);
	if (isdst)
		p = tz(p, tznm);
	return(tznm);
}

static char *
tz(p, t)
register char *p;
register char *t;
{
	register int	n = MAXTZNAME;

	while (!isalpha(*p)) {
		if (*p++ == '\0')
			goto err;
	}
	do {
		if (--n >= 0)
			*t++ = *p;
	} while (isalpha(*++p));
	while (--n >= 0)
		*t++ = ' ';
err:
	*t = '\0';
	return(p);
}
