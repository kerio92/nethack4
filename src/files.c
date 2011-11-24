/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "dlb.h"

#include <ctype.h>
#include <fcntl.h>

#include <errno.h>
#ifdef _MSC_VER	/* MSC 6.0 defines errno quite differently */
# if (_MSC_VER >= 600)
#  define SKIP_ERRNO
# endif
#else
# define SKIP_ERRNO
#endif
#ifndef SKIP_ERRNO
extern int errno;
#endif

#if defined(WIN32)
#include <sys/stat.h>
#endif
#ifndef O_BINARY
# define O_BINARY 0
#endif

#define FQN_NUMBUF 4
static char fqn_filename_buffer[FQN_NUMBUF][FQN_MAX_FILENAME];
char bones[] = "bonesnn.xxx";

#if defined(WIN32)
static int lockptr;
#define Close close
#define DeleteFile unlink
#endif

static const char *fqname(const char *, int, int);


void display_file(const char *fname, boolean complain)
{
	dlb *fp;
	char *buf;
	int fsize;

	fp = dlb_fopen(fname, "r");
	if (!fp) {
	    if (complain) {
		pline("Cannot open \"%s\".", fname);
	    } else if (program_state.something_worth_saving) doredraw();
	} else {
	    dlb_fseek(fp, 0, SEEK_END);
	    fsize = dlb_ftell(fp);
	    dlb_fseek(fp, 0, SEEK_SET);
	    
	    buf = malloc(fsize);
	    dlb_fread(buf, fsize, 1, fp);
	    
	    dlb_fclose(fp);
	    
	    display_buffer(buf, complain);
	    
	    free(buf);
	}
}


const char *fqname(const char *filename, int whichprefix, int buffnum)
{
	if (!filename || whichprefix < 0 || whichprefix >= PREFIX_COUNT)
		return filename;
	if (!fqn_prefix[whichprefix])
		return filename;
	if (buffnum < 0 || buffnum >= FQN_NUMBUF) {
		impossible("Invalid fqn_filename_buffer specified: %d",
								buffnum);
		buffnum = 0;
	}
	if (strlen(fqn_prefix[whichprefix]) + strlen(filename) >=
						    FQN_MAX_FILENAME) {
		impossible("fqname too long: %s + %s", fqn_prefix[whichprefix],
						filename);
		return filename;	/* XXX */
	}
	strcpy(fqn_filename_buffer[buffnum], fqn_prefix[whichprefix]);
	return strcat(fqn_filename_buffer[buffnum], filename);
}


/* fopen a file, with OS-dependent bells and whistles */
/* NOTE: a simpler version of this routine also exists in util/dlb_main.c */
FILE *fopen_datafile(const char *filename, const char *mode, int prefix)
{
	FILE *fp;

	filename = fqname(filename, prefix, prefix == TROUBLEPREFIX ? 3 : 0);
	fp = fopen(filename, mode);
	return fp;
}


/* ----------  BEGIN BONES FILE HANDLING ----------- */

int create_bonesfile(char *bonesid, char errbuf[])
{
	const char *file;
	char tempname[PL_NSIZ+32];
	int fd;

	if (errbuf) *errbuf = '\0';
	sprintf(bones, "bon%s", bonesid);
	sprintf(tempname, "%d%s.bn", (int)getuid(), plname);
	file = fqname(tempname, BONESPREFIX, 0);

#if defined(WIN32)
	/* Use O_TRUNC to force the file to be shortened if it already
	 * exists and is currently longer.
	 */
	fd = open(file, O_WRONLY |O_CREAT | O_TRUNC | O_BINARY, FCMASK);
#else
	fd = creat(file, FCMASK);
#endif
	if (fd < 0 && errbuf) /* failure explanation */
	    sprintf(errbuf, "Cannot create bones id %s (errno %d).",
		    bonesid, errno);

	return fd;
}


/* move completed bones file to proper name */
void commit_bonesfile(char *bonesid)
{
	const char *fq_bones, *tempname;
	char tempbuf[PL_NSIZ+32];
	int ret;

	sprintf(bones, "bon%s", bonesid);
	fq_bones = fqname(bones, BONESPREFIX, 0);
	sprintf(tempbuf, "%d%s.bn", (int)getuid(), plname);
	tempname = fqname(tempbuf, BONESPREFIX, 1);

	ret = rename(tempname, fq_bones);
	if (wizard && ret != 0)
		pline("couldn't rename %s to %s.", tempname, fq_bones);
}


int open_bonesfile(char *bonesid)
{
	const char *fq_bones;
	int fd;

	sprintf(bones, "bon%s", bonesid);
	fq_bones = fqname(bones, BONESPREFIX, 0);
	fd = open(fq_bones, O_RDONLY | O_BINARY, 0);
	return fd;
}


int delete_bonesfile(char *bonesid)
{
	sprintf(bones, "bon%s", bonesid);
	return !(unlink(fqname(bones, BONESPREFIX, 0)) < 0);
}

/* ----------  END BONES FILE HANDLING ----------- */


/* ----------  BEGIN FILE LOCKING HANDLING ----------- */

#if defined(UNIX)
/* lock any open file using fcntl */
boolean lock_fd(int fd, int retry)
{
    struct flock sflock;
    int ret;
    
    if (fd == -1)
	return FALSE;
    
    sflock.l_type = F_WRLCK;
    sflock.l_whence = SEEK_SET;
    sflock.l_start = 0;
    sflock.l_len = 0;
    
    while ((ret = fcntl(fd, F_SETLK, &sflock)) == -1 && retry--)
	sleep(1);
    
    return ret != -1;
}


void unlock_fd(int fd)
{
    struct flock sflock;
    
    if (fd == -1)
	return;
    
    sflock.l_type = F_UNLCK;
    sflock.l_whence = SEEK_SET;
    sflock.l_start = 0;
    sflock.l_len = 0;
    
    fcntl(fd, F_SETLK, &sflock);
}
#elif defined (WIN32) /* windows versionf of lock_fd(), unlock_fd() */

/* lock any open file using LockFileEx */
boolean lock_fd(int fd, int retry)
{
    HANDLE hFile;
    OVERLAPPED o;
    DWORD fileSize;
    BOOL ret;
    
    if (fd == -1)
	return FALSE;

    hFile = _get_osfhandle(fd);
    fileSize = GetFileSize(hFile, NULL);
    
    o.hEvent = 0;
    o.Offset = o.OffsetHigh = 0;
    while ((ret = LockFileEx(hFile, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
		      0 /* reserved */, fileSize, 0, &o)))
	sleep(1);
    return ret;
}


void unlock_fd(int fd)
{
    HANDLE hFile;
    OVERLAPPED o;
    DWORD fileSize;
    if (fd == -1)
	return;

    hFile = _get_osfhandle(fd);
    fileSize = GetFIleSize(hFile, NULL);
    
    o.hEvent = 0;
    o.Offset = o.OffsetHigh = 0;
    UnLockFileEx(hFile, 0 /* reserved */, fileSize, 0, &o);
}
#endif

/* ----------  END FILE LOCKING HANDLING ----------- */

/* ----------  BEGIN PANIC/IMPOSSIBLE LOG ----------- */

/*ARGSUSED*/
void paniclog(const char *type,   /* panic, impossible, trickery */
	      const char *reason) /* explanation */
{
#ifdef PANICLOG
	FILE *lfile;
	char buf[BUFSZ];

	if (!program_state.in_paniclog) {
		program_state.in_paniclog = 1;
		lfile = fopen_datafile(PANICLOG, "a", TROUBLEPREFIX);
		if (lfile) {
		    fprintf(lfile, "%s %08ld: %s %s\n",
				   version_string(buf), yyyymmdd((time_t)0L),
				   type, reason);
		    fclose(lfile);
		}
		program_state.in_paniclog = 0;
	}
#endif /* PANICLOG */
	return;
}

/* ----------  END PANIC/IMPOSSIBLE LOG ----------- */

/*files.c*/
