#define _GNU_SOURCE
#include <sys/mman.h>
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PORT 19735
#define BACKLOG 100000
#define STACK_SIZE 1024*1024
#define CONCURENT_HANDLES 1000
#define REQUEST_MAX_SIZE 1000
#define MAX_READ 90000


struct thread
{
	int socket;
	pid_t pid;
	void* stack;
};

void
init_threads (struct thread* threads)
{
	for (int i=0;i<CONCURENT_HANDLES;i++)
	{
		threads[i].socket=-1;
		threads[i].pid=-1;
	}
}

int
find_empty_socket_slot(struct thread* threads)
{
	for (int i=0;i<CONCURENT_HANDLES;i++)
	{
		if ( threads[i].socket == -1)
		{
			return i;
		}
	}
	return -1; // no current available sockets, all threads working
}

void
zombie_cleanup(struct thread* threads)
{
	pid_t pid;
	while( (pid=waitpid(-1, 0, WNOHANG))!=0 && pid!=-1)
	{
		for (int i=0;i<CONCURENT_HANDLES;i++)
		{
			if(pid==threads[i].pid)
			{
				threads[i].pid=-1;
				threads[i].socket=-1;
				munmap(threads[i].stack, STACK_SIZE);
				break;
			}
		}
	}
}

int
request_contains_quote (char* request, int length)
{
	for (int i=0; i<length;i++)
	{
		if (request[i] == '"')
		{
			return 1;
		}
	}
	return 0;
}

void
terminate_thread (int fd, int exit_status)
{
	close(fd);
	_exit(exit_status);
}

int
handle_connection(void *arg)
{
	int fd  =  *(int*)arg;
	char request[REQUEST_MAX_SIZE];
	read(fd, request, REQUEST_MAX_SIZE);
	int req_length = strlen(request);
	if(request_contains_quote(request,req_length))
	{
		write(fd,"Denied",6);
		terminate_thread(fd, -1);
	}

	char filename[25];
	sprintf(filename,"generated-files/%u.wav",fd);
	char exec[req_length+125]; //more than enought space
	sprintf(exec,"tts --text \"%s\" --out_path %s > /dev/null",request, filename);
	system(exec);
	int fd_of_wav = open(filename, O_RDONLY);
	struct stat st;
	fstat(fd_of_wav, &st);
	char file_buffer[MAX_READ];
	int b_read;
	do{
		b_read = read(fd_of_wav, file_buffer,MAX_READ);
		write(fd, file_buffer, b_read);
	}while(b_read == MAX_READ);
	close(fd_of_wav);
	sprintf(exec,"rm %s > /dev/null",filename);
	system(exec);
	terminate_thread(fd,0);
}

int 
main(void)
{
	int			server_socket;	 		
	struct sockaddr_in 	server_socket_address;
	server_socket = socket(AF_INET, SOCK_STREAM,0);
	if (server_socket<0)
	{
		printf("Error opening socket\n");
		_exit(-1);
	}
	memset(&server_socket_address,0, sizeof(server_socket_address));
	server_socket_address.sin_family=AF_INET;
	server_socket_address.sin_addr.s_addr=htonl(INADDR_ANY);
	server_socket_address.sin_port = htons(PORT);

	if(bind(server_socket, (struct sockaddr*)&server_socket_address, sizeof(server_socket_address)) != 0)
	{
		printf("Error binding socket\n");
		_exit(-1);
	}
	if (listen(server_socket,BACKLOG) != 0)
	{
		printf("Error listen\n");
		_exit(-1);	
	}
	struct thread threads[CONCURENT_HANDLES];
	init_threads(threads);
	while(1)
	{
		zombie_cleanup(threads);
		int t_index = find_empty_socket_slot(threads);
	        if (t_index == -1) //no free threads
		{
			sleep(2);
			continue;	
		}
		threads[t_index].socket = accept(server_socket, 0, 0);
		if(threads[t_index].socket < 0)
		{
			printf("error during accepting\n");
		}
		threads[t_index].stack = mmap(0,STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
		if ( threads[t_index].stack == MAP_FAILED)
		{
               		printf("error during mmap\n");
		}
		threads[t_index].pid=clone(handle_connection, threads[t_index].stack + STACK_SIZE,CLONE_FS | SIGCHLD | CLONE_FILES,
				&threads[t_index].socket);
		if (threads[t_index].pid == -1)
		{
			printf("Cloning failed\n");
		}
	}
	_exit(0);
}
