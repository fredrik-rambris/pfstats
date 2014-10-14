#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcre.h>
#include <signal.h>
#include <unistd.h>

#define BUFSIZE 1024
#define STATSFILE "/var/log/pfstats"
#define LOGFILE "/var/log/maillog"

enum
{
	SENT,
	DEFERRED,
	BOUNCED,
	RECEIVED
};

int stats[4]={0,0,0,0};

int cmp(char *a, char *b);
void *statsthread(void* bla);
void printstats();
void sighandler(int signal);
pid_t popen2(const char *command, int *infp, int *outfp);

int running=1;
pthread_t thread=0;
pid_t tail=0;

int main(int argc, char **argv)
{
	char buf[BUFSIZE];
	char process[64];
	pcre *re_process;
	pcre_extra *reextra_process;

	pcre *re_status;
	pcre_extra *reextra_status;

	const char *error;
	int erroffset;
	int rc;
	int ovector[10]={0,0,0,0,0,0,0,0,0,0};
	char *ptr;
	int threadid;
	FILE *in;

	in=fopen(LOGFILE, "r");
	if(!in)
	{
		fprintf(stderr, "Could not open log file\n");
		exit(2);
	}
	fclose(in);

	tail=popen2("/usr/bin/tail -F -n1 " LOGFILE, NULL, &rc);
	if(!tail)
	{
		fprintf(stderr, "Could not open log file\n");
		exit(2);
	}
	in=fdopen(rc,"r");
	if(!in)
	{
		fprintf(stderr, "Could not open log file\n");
		exit(2);
	}

	close(0);
	close(1);
	close(2);
	daemon(0,1);

	re_process = pcre_compile(" ([^ ]+)\\[\\d+\\]:", 0, &error, &erroffset, NULL);
	reextra_process = pcre_study(re_process, 0, &error);

	re_status = pcre_compile("status=(\\S+)", 0, &error, &erroffset, NULL);
	reextra_status = pcre_study(re_status, 0, &error);

	threadid=pthread_create(&thread, NULL, statsthread, NULL);

	signal(SIGINT, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);

	while(fgets(buf, BUFSIZE, in))
	{
		process[0]='\0';

		rc = pcre_exec(re_process, reextra_process, buf, strlen(buf), 0, 0, ovector, 10);
		if(rc>0)
		{
			strncpy(process,buf+ovector[2],ovector[3]-ovector[2]);
			process[ovector[3]-ovector[2]]='\0';
			printf("%s\n", process);
		}

		rc = pcre_exec(re_status, reextra_status, buf, strlen(buf), 0, 0, ovector, 10);
		if(rc>0)
		{
			buf[ovector[1]]='\0';
			ptr=buf+ovector[2];
			if(cmp(ptr,"sent"))
			{
				if(cmp(process,"postfix/local")) stats[RECEIVED]++;
				else if(cmp(process,"postfix/pipe")) stats[RECEIVED]++;
				else stats[SENT]++;
			}
			else if(cmp(ptr,"deferred")) stats[DEFERRED]++;
			else if(cmp(ptr,"bounced")) stats[BOUNCED]++;
		}
	}
	fclose(in);
	if(tail) kill(tail, SIGKILL);
	tail=0;
	running=0;
	if(thread) pthread_join(thread, NULL);
	printstats();
	return 0;
}

int cmp(char *a, char *b)
{
	while(*a && *b)
	{
		if(*a != *b) return 0;
		a++;
		b++;
	}
	return *a == *b;
}

void *statsthread(void* bla)
{
	int c=50;
	while(running)
	{
		usleep(100000);
		if(!c--)
		{
			c=50;
			printstats();
		}
	}
}

void printstats()
{
	FILE *sf=NULL;
	unlink(STATSFILE ".old");
	unlink(STATSFILE ".new");

	if((sf=fopen(STATSFILE ".new", "w")))
	{
		fprintf(sf, "sent:%d deferred:%d bounced:%d received:%d", stats[SENT], stats[DEFERRED], stats[BOUNCED], stats[RECEIVED]);
		fclose(sf);
		rename(STATSFILE, STATSFILE ".old");
		rename(STATSFILE ".new", STATSFILE);
		unlink(STATSFILE ".old");
	}

}

void sighandler(int signal)
{
	running=0;
	usleep(200000);
	if(tail)
	{
		kill(tail, signal);
		usleep(200000);
		tail=0;
	}
	exit(0);
}

#define READ 0
#define WRITE 1

pid_t popen2(const char *command, int *infp, int *outfp)
{
	int p_stdin[2], p_stdout[2];
	pid_t pid;

	if (pipe(p_stdin) != 0 || pipe(p_stdout) != 0)
		return -1;

	pid = fork();

	if (pid < 0)
		return pid;
	else if (pid == 0)
	{
		close(p_stdin[WRITE]);
		dup2(p_stdin[READ], READ);
		close(p_stdout[READ]);
		dup2(p_stdout[WRITE], WRITE);

		execl("/bin/sh", "sh", "-c", command, NULL);
		perror("execl");
		exit(1);
	}

	if (infp == NULL)
		close(p_stdin[WRITE]);
	else
		*infp = p_stdin[WRITE];

	if (outfp == NULL)
		close(p_stdout[READ]);
	else
		*outfp = p_stdout[READ];

	return pid;
}
