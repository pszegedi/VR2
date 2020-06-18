
#define _GNU_SOURCE

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>

#include <sched.h>
#include <stdlib.h>
#include <pthread.h>
#include <syslog.h>

#include <getopt.h>


#define BUFFER_SIZE 65536
#define FRAME_BODY_SIZE 8204

#define DIR_NAME "./"

#define LOGGING 0
#if LOGGING==1
#define LOG(x...)	syslog(1,x)
#else
#define LOG(x...)	printf(x)
#endif

struct vr2_frame{
	unsigned short SYNC;
	unsigned short MMYY;
	unsigned short hhdd;
	unsigned short ssmm;
	unsigned short HW_TLO;
	unsigned short HW_THI;
	unsigned short STATUS;
	unsigned short data[4096];
};

	struct vr2_frame frame;				// VR2 frame
    const char * defav2 = "solymar";	// default location string
    char * av2;							// location string
    volatile int RUN;					// 0: some error program should stop, 1: program running

    char hostname[1024];				// VR2 host name

	pthread_t commandsocket_id;			// command socket handling thread ID

    char channel[255];					// VR2 channels enabled

	int divider;						// VR2 clock divider

    char dummy[1024];					// dummy string to create

	FILE * logfile;
	
	int restart_cmd = 0;				// 1: command socket should be reconnected
	
	int raw;

	

void serial_close(int *);
    
void restart_vr2(int serial_fd) {
	int dummy;
    time_t lt;
    struct tm ltime;
    struct tm * pltime = &ltime;

	lt = time (NULL);
	pltime = localtime (&lt);

	ioctl (serial_fd, TIOCMGET, &dummy);
	dummy |= TIOCM_RTS;
	ioctl (serial_fd, TIOCMSET, &dummy);
	fprintf (logfile,"VR2 switched OFF and ON  %04d.%02d.%02d. %02d:%02d:%02d\n\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
	LOG("VR2 switched OFF and ON  %04d.%0d.%02d. %02d:%02d:%02d\n\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
	sleep(5);
	ioctl (serial_fd, TIOCMGET, &dummy);
	dummy &= ~TIOCM_RTS;
	ioctl (serial_fd, TIOCMSET, &dummy);
}

int serial_init( int * serial_fd, char* port ) {

    if ( *serial_fd != 0 ){
        serial_close(serial_fd);
    }

//    LOG ("serialopen\n");
    *serial_fd = open ( port, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (*serial_fd == 0)
    {
    	perror ("Error opening serial port\n");
    	return -1;
    }

	int dummy;
	ioctl (*serial_fd, TIOCMGET, &dummy);
	dummy &= ~TIOCM_RTS;
	ioctl (*serial_fd, TIOCMSET, &dummy);

    return 0;
}

void serial_close(int * serial_fd){
//    tcsetattr(serial_fd, TCSANOW, &oldops);
    close (* serial_fd);
    *serial_fd = 0;
}

int command_write_chars(int command_fd, char * data) {
    int n,i;
    
    i = 0;
    while ( data[i] != '\0' ) {
//	    LOG ("serial_write_char: %x\n", data[i]);
    	n = write(command_fd, &data[i++], 1);
    	if ( n < 0 ) {
        	LOG ("Error can't write 1 byte to command port");
        	return -1;
    	}
	}
	return 0;
}


void* commandsocket(void*arg){
	int n, disp=0;
	FILE * slogfile;
	char data;
	int output_fd;
	fd_set readfs;
//	int serport;
    int firstdata;
	struct timeval tmout;
    volatile int command_fd;
	struct  hostent  *cptrh;    /* pointer to a host table entry       */
	struct  protoent *cptrp;    /* point to a protocol table entry     */
	struct  sockaddr_in csad;   /* structure to hold sender's address  */
    time_t clt;
    struct tm cltime;
    struct tm * cpltime = &cltime;

    char cfilenamet [255];
    char * cfilename = (char *) &cfilenamet;
    char cfile_[1024];
    char * cfile_name = (char *)&cfile_ ;

    memset((char *)&csad,0,sizeof(csad));  /* clear sockaddr structure */
	csad.sin_family = AF_INET;            /* set family to Internet   */
	csad.sin_port = htons(6000);
	cptrh = gethostbyname(hostname);		// VR2 command socket
	if( ((char *)cptrh) == NULL){
		fprintf( stderr, "invalid host:  %s\n", hostname);
		RUN = 0;
		return NULL;
	}

	memcpy(&csad.sin_addr, cptrh->h_addr, cptrh->h_length);
	if ( ((int)(cptrp = getprotobyname("tcp"))) == 0){
		fprintf( stderr, "cannot map \"tcp\" to protocol number\n");
		RUN = 0;
		return NULL;
	}

l_csocket:
	LOG ("command_socket\n");
	command_fd = socket(PF_INET, SOCK_STREAM, cptrp->p_proto);
	if (command_fd < 0) {
		fprintf( stderr, "command socket creation failed\n");
		RUN = 0;
		return NULL;
	}

l_cconnect:
	if (RUN == 0)
		exit (2);
	LOG ("command_connect\n");
    if ( connect(command_fd, (struct sockaddr *)&csad, sizeof(csad)) < 0) {
    	fprintf( stderr, "command connect failed\n");
		sleep (5);
        goto l_cconnect;
	}

    
    clt = time (NULL);
   	cpltime = localtime (&clt);
    strftime (cfilename, 255, "%Y-%m-%dUT%H:%M:%S", cpltime);
    snprintf (cfile_name, 1024, "%s.%s.vr2.commanddata.log", cfilename,av2);
    output_fd = open (cfile_name, O_RDWR | O_NOCTTY | O_CREAT | O_APPEND, 0666);
   	if (output_fd < 0){
		perror ("Error: opening output file\n");
		RUN = 0;
		return NULL;
   	}
   	
    clt = time (NULL);
    cpltime = localtime (&clt);
    strftime (cfilename, 255, "%Y-%m-%dUT%H:%M:%S", cpltime);
    snprintf (cfile_name, 1024, "%s.%s.vr2.commanderror.log", cfilename, av2);

	if ( (slogfile = fopen(cfile_name, "a+")) == NULL )
	{
		perror("Error opening command logfile");
		RUN = 0;
		return NULL;
	}

	firstdata = 0;
	while (RUN){
		if (firstdata == 0) {
		    snprintf((char*)&dummy, 1024, ".%s\n", channel);
		    LOG("sent command:%s", dummy);
		    command_write_chars(command_fd,(char*)&dummy);
		    snprintf((char*)&dummy, 1024, ".S%x\n", divider);
		    LOG("sent command:%s", dummy);
		    command_write_chars(command_fd,(char*)&dummy);
		    firstdata = 1;
		}

		if (restart_cmd == 1){
			restart_cmd = 0;
			close(command_fd);
			goto l_csocket;
		}

		FD_SET(command_fd, &readfs);
		tmout.tv_sec = 2;
		tmout.tv_usec = 0;
		n = select (command_fd+1, &readfs, NULL, NULL, &tmout);
		if (n < 0){
			LOG("error com select\n");
			sleep(10);
			continue;
		}
		if (FD_ISSET(command_fd, &readfs)){
			disp = 0;
read_more_comreply:			
			n = recv (command_fd, &data, 1,MSG_DONTWAIT);
			if ( n == 1) {
				write(output_fd, &data, 1);
				fsync(output_fd);
				goto read_more_comreply;
			}
			if ( n<=0 ){
				continue;
			}
		}
 //		perror("\n");
		if (n == 0){
			sleep(1);
			disp = 0;
		} else {
			if (disp == 0){
				disp++;
			    clt = time (NULL);
   				cpltime = localtime (&clt);
				fprintf (slogfile,"Error reading from command port: %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+cpltime->tm_year, cpltime->tm_mon+1, cpltime->tm_mday, cpltime->tm_hour, cpltime->tm_min, cpltime->tm_sec);
				fflush(slogfile);
				close(command_fd);
				sleep(2);
				goto l_csocket;
			}
		}
	}
	close(output_fd);
    close (command_fd);
	
	fclose(slogfile);
	return NULL;
}

int read_data(int fd, void* addr, int size){
	int count;
	void * address = addr;
	int data_read = size;
	int n;
	fd_set input;
	struct timeval timeout;

	while (RUN){
		FD_ZERO(&input);
		FD_SET(fd, &input);
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;
		n = select(fd+1, &input, NULL, NULL, &timeout);
		if (n<0) return -1;		// error when select
		if (n == 0) return 0;	// timeout
		if (n>0){
			if ((count = recv (fd, address, data_read, MSG_DONTWAIT)) != data_read ){
				if (count > 0){
					address += count;
					data_read -= count;
					if (data_read > 0)
						continue;
				} else return -1;
			}
			return size;
		}
	}
	return 0;
}


int findsync(int sd){
	unsigned char c;
	int s = 0;
	int cnt;
	int bcount;
	fd_set input;
	struct timeval timeout;
	int n;

//	LOG("findsync\n");
	bcount=0;
	while (RUN){
		FD_ZERO(&input);
		FD_SET(sd, &input);
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;
		n = select(sd+1, &input, NULL, NULL, &timeout);
		if (n<0) return -1;		// error when select
		if (n == 0) return 0;	// timeout
		if (n>0){
			cnt = recv(sd, &c, 1, MSG_DONTWAIT);
			if ( cnt <= 0 ) return -1;
			bcount++;
//			LOG("select ok, 0x%01x, cnt:%d\n", c, cnt);
			if (cnt == 1){
				switch (s){
				case 0:
					if (c == 0x16){
						s=1;
					}
					break;
				case 1:
					if (c == 0xA1){
						return bcount;
					} else {
						s=0;
					}
					break;
				}
			} else {
				return 0;
			}
		}
	}
	return -1;
}

int printhelp(){
	printf ("* Syntax:         driver [option] [dir] [location]\n*\n* dir  - directory name to write files default is ./"
			"\n* location - recording location added to filenames"
			"\n* option:"
			"\n* 	-r - real-time mode starting whd_rt.sh"
			"\n*	-h - print this help\n");
	return 0;
}

int main(int argc, char * argv[]) {
    int input_fd;
    int output_fd=-1;
    int raw_fd=0;
    int frame_fd=0;

    time_t lt;
    struct tm ltime;
    struct tm * pltime = &ltime;

    fd_set input;
    struct timeval timeout;
    int n;
    int zz=0;
    char * dirname ;
    int err_dispd = 1;

    int today;
    char filenamet [255];
    char * filename = (char *) &filenamet;

    char file_[1024];
    char * file_name = (char *)&file_ ;

    int serial_fd;
    char serial_fn[255];

	struct  hostent  *ptrh;    /* pointer to a host table entry       */
	struct  protoent *ptrp;    /* point to a protocol table entry     */
	struct  sockaddr_in sad;   /* structure to hold sender's address  */
	int ec = 0;
	int realtime = 0;
	int opt;
    FILE * propsfile;
	
    av2 = (char*)defav2;
    dirname = DIR_NAME;
    
    LOG ("*** VR2 filewriter V6 started ***\n");
//    signal (SIGCHLD, SIG_IGN);

    while((opt = getopt(argc,argv,"rh")) != -1){
    	switch(opt){
    	case 'r':
    		realtime = 1;
    		break;
    	case 'h':
    		printhelp();
    		return 0;
    		break;
    	}
    }
    if (realtime==1)
    	LOG("real-time mode on\n");
    else
    	LOG("real-time mode off\n");

    if (optind < argc){
    	dirname = argv[optind++];
    }
    if (optind < argc){
		av2 = argv[optind++];
    }

    lt = time (NULL);
    pltime = localtime (&lt);
//    strftime (filename, 255, "%Y_%m_%d_%H_%M_%S", pltime);
    strftime (filename, 255, "%Y-%m-%dUT%H:%M:%S", pltime);
    snprintf (file_name, 1024, "%s.%s.vr2.store.log", filename,av2);
	
	if ( (logfile = fopen(file_name, "a+")) == NULL ){
		LOG("Error opening logfile");
		return -1;
	}

	if ( (propsfile = fopen("vr2.conf", "rb")) != NULL){
 		clearerr(propsfile);
 		while (!feof(propsfile)){
 			fscanf( propsfile, "%[^\n]\n", (char*)&dummy );
 			if (dummy[0] != '#'){
       			if (sscanf( (char *)&dummy, "serial_port %[^\n]\n", (char*)&serial_fn ) != 0){
		       		LOG("serial_port %s\n", serial_fn);
		      	 	break;
       			}
 			}
 		}
 		if (feof(propsfile)){
			perror ("Error: missing parameter serial_port from vr2.conf\n ");
			serial_close(&serial_fd);
			return 1; 			
 		}       	
 		fseek(propsfile, 0, SEEK_SET);
 		clearerr(propsfile);
 		while (!feof(propsfile)){
 			fscanf( propsfile, "%[^\n]\n", (char*)&dummy );
 			if (dummy[0] != '#'){
	 			if (sscanf( (char *)&dummy, "channel %[^\n]\n", (char*)&channel ) != 0){
        	      	LOG("channel %s\n", channel);
            	  	break;
 				}
 			}
 		}
 		if (feof(propsfile)){
			perror ("Error: missing parameter chanel from vr2.conf\n ");
			serial_close(&serial_fd);
			return 1; 			
 		}      

 		fseek(propsfile, 0, SEEK_SET);
 		clearerr(propsfile);

 		while (!feof(propsfile)){
 			fscanf( propsfile, "%[^\n]\n", (char*)&dummy );
 			if (dummy[0] != '#'){
		       	if ( sscanf( (char *)&dummy , "divider %d\n", &divider ) != 0){
		       		if ( (divider > 15) || (divider < 0) ){
		       			LOG("wrong divider setting, divider set to 3\n");
		       			divider = 3;
		       		}
 			       	LOG("divider %d\n", divider);
 		    	   	break;
	       		}
 			}
 		}
  		if (feof(propsfile)){
			perror ("Error: missing parameter divider from vr2.conf\n ");
			serial_close(&serial_fd);
			return 1; 			
 		}      

 		fseek(propsfile, 0, SEEK_SET);
 		clearerr(propsfile);

 		while (!feof(propsfile)){
 			fscanf( propsfile, "%[^\n]\n", (char*)&dummy );
 			if (dummy[0] != '#'){
      			if (sscanf( (char *)&dummy, "hostname %[^\n]\n", (char*)&hostname ) != 0){
 			       	LOG("hostname %s\n", hostname);
 		    	   	break;
	       		}
 			}
 		}
  		if (feof(propsfile)){
			perror ("Error: missing parameter hostname from vr2.conf\n ");
			serial_close(&serial_fd);
			return 1; 			
 		}      

		fseek(propsfile, 0, SEEK_SET);
 		clearerr(propsfile);

 		while (!feof(propsfile)){
 			fscanf( propsfile, "%[^\n]\n", (char*)&dummy );
 			if (dummy[0] != '#'){
      			if (sscanf( (char *)&dummy, "raw %d\n", &raw ) != 0){
 			       	LOG("raw %d\n", raw);
 		    	   	break;
	       		}
 			}
 		}
  		if (feof(propsfile)){
 			raw = 1;
 		}      
      
    	fclose (propsfile);
    	
	} else {
		perror ("Error: missing parameter file vr2.conf\n ");
		return -1;
	}
   	LOG("data dir: %s\n", dirname);
   	LOG("location: %s\n", av2);

   	if ( (raw&0x2) > 0){
		mkfifo("./raw_data",S_IWUSR | S_IRUSR);
   		raw_fd = open("./raw_data", O_CREAT | O_TRUNC | O_RDWR | O_NONBLOCK, 0666 );
   		if (raw_fd < 0)
   			raw &= ~0x02;
   	}

   	if ( (raw&0x4) > 0){
		mkfifo("./frame_data",S_IWUSR | S_IRUSR);
   		frame_fd = open("./frame_data", O_CREAT | O_TRUNC | O_RDWR | O_NONBLOCK, 0666 );
   		if (frame_fd < 0)
   			raw &= ~0x04;
   	}

    lt = time (NULL);
    pltime = localtime (&lt);
//    strftime (filename, 255, "%Y_%m_%d_%H_%M_%S", pltime);
    strftime (filename, 255, "%Y-%m-%dUT%H:%M:%S", pltime);
    snprintf (file_name, 1024, "%s//%s.%s.vr2", dirname, filename,av2);

    today = pltime->tm_hour;
//    LOG ("pltime->tm_min = %d\n", pltime->tm_min);
    if (raw&0x01 != 0){
    	output_fd = open (file_name, O_RDWR | O_NOCTTY | O_CREAT | O_APPEND | O_LARGEFILE, 0666);
    
    	if (output_fd < 0) {
    		perror ("Error: opening output file\n");
    		return 1;
    	}
    }

    if (realtime==1){
        // whd_rt start at the first time
        // "whd_rt.sh file_name"
    	if ( 0 == fork() ) {
    		// forked child process
    		signal (SIGCHLD, SIG_DFL);
    		char arg1buf [64];
    		snprintf(arg1buf,sizeof(arg1buf),"%s.%s.vr2", filename,av2);
    		execl("./whd_rt.sh","whd_rt.sh",arg1buf,NULL);
    		perror("./whd_rt.sh exec error");
    		exit(-1);
    	}
    }
    
    RUN = 1;

	ec=0;
	while ((n = pthread_create(&commandsocket_id, NULL, commandsocket, NULL))!=0){
		LOG("Driver:pthread_create error commandsocket\n");
		sleep(1);
		ec++;
		if (ec>10) return -1;
	}

	memset((char *)&sad,0,sizeof(sad));  /* clear sockaddr structure */
	sad.sin_family = AF_INET;            /* set family to Internet   */
	sad.sin_port = htons(5000);				// data port

	ptrh = gethostbyname(hostname);
	if( ((char *)ptrh) == NULL){
		fprintf( stderr, "invalid host:  %s\n", hostname);
		RUN = 0;
		return -1;
	}

	memcpy(&sad.sin_addr, ptrh->h_addr, ptrh->h_length);

	if ( ((int)(ptrp = getprotobyname("tcp"))) == 0){
		fprintf( stderr, "cannot map \"tcp\" to protocol number\n");
		RUN = 0;
		return -1;
	}

l_socket:
	LOG ("data_socket\n");
	input_fd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
	if (input_fd < 0) {
		fprintf( stderr, "socket creation failed\n");
		RUN = 0;
		return -1;
	}
	    
l_connect:
	if (RUN == 0)
		return -1;
	LOG ("data_connect\n");
    if ( connect(input_fd, (struct sockaddr *)&sad, sizeof(sad)) < 0) {
    	fprintf( stderr, "data connect failed\n");
		sleep (10);
        goto l_connect;
	}

// halozat connect

	lt = time (NULL);
	pltime = localtime (&lt);
	fprintf (logfile,"Connection established at %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
	fflush(logfile);
	LOG("Connection established at %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);


	err_dispd = 0;
    while (RUN) {
		lt = time (NULL);
		pltime = localtime (&lt);
	
//    	LOG ("pltime->tm_min = %d, today = %d\n", pltime->tm_min, today);
		
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;
	
		FD_ZERO ( &input);
		FD_SET (input_fd, &input);

//		usleep (1);		
	
		n = select (input_fd+1, &input, NULL, NULL, &timeout);
//		LOG ("%d\n", n);
		if (n < 0) {
			perror ("Error: select failed");
			LOG ("Connection terminated by VR2  %04d.%02d.%02d. %02d:%02d:%02d at select\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
			fprintf (logfile,"Connection terminated by VR2  %04d.%02d.%02d. %02d:%02d:%02d at select\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
			fflush(logfile);
			today = -1;
			close(input_fd);
			restart_cmd = 1;
			goto l_socket;
//			puts("select1");
		} else if (n == 0) {
//			puts("Timeout: select2");
			if (zz > 2) {
				if (err_dispd == 0) {
					fprintf (logfile,"No incoming data in last 2s  %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
					fflush(logfile);
					LOG("No incoming data in last 2s  %04d.%0d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
					err_dispd++;
					restart_vr2(serial_fd);
				}
				
				today = -1;
				close(input_fd);
				restart_cmd = 1;
				goto l_socket;
			}
			zz++;
			
		} else {
			zz = 0;
			if (today != pltime->tm_hour)
			{
				if (output_fd>0){
					close (output_fd);
		        	strftime (filename, 255, "%Y-%m-%dUT%H:%M:%S", pltime);
		        	snprintf (file_name, 1024, "%s//%s.%s.vr2", dirname, filename,av2);
		        	today = pltime->tm_hour;
	    
		        	output_fd = open (file_name, O_RDWR | O_NOCTTY | O_CREAT | O_APPEND | O_LARGEFILE, 0666);
	    
		        	if (output_fd < 0){
		        		perror ("Error: opening output file\n");
		        		//return 1;
		        		break;
		        	}
				}

			    if (realtime==1){
			    	// whd_rt start in every hour
			    	// "whd_rt.sh file_name"
			    	if ( 0 == fork() ) {
			    		// forked child process
			    		signal (SIGCHLD, SIG_DFL);
			    		char arg1buf [64];
			    		snprintf(arg1buf,sizeof(arg1buf),"%s.%s.vr2", filename,av2);
			    		execl("./whd_rt.sh","whd_rt.sh",arg1buf,NULL);
			    		perror("./whd_rt.sh exec error");
			    		exit(-1);
			    	}
		        }
			}
			
			
			if (err_dispd == 1) {
				err_dispd = 0;
				fprintf (logfile,"Data receiving restarted at  %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
				fflush(logfile);
				LOG("Data receiving restarted at  %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
			}

			
			if (FD_ISSET (input_fd, &input)) {
				n = findsync(input_fd);
				if (n < 0) {
					close(input_fd);
					restart_cmd = 1;
					today = -1;
					LOG ("Connection terminated by VR2  %04d.%02d.%02d. %02d:%02d:%02d at findsync\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
					fprintf (logfile,"Connection terminated by VR2  %04d.%02d.%02d. %02d:%02d:%02d at findsync\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
					fflush(logfile);
					goto l_socket;
				}
				if (n==0) {
					if (err_dispd == 0) {
						fprintf (logfile,"No incoming data in last 2s  %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
						fflush(logfile);
						LOG("No incoming data in last 2s  %04d.%0d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
						err_dispd = 1;
					}
					restart_vr2(serial_fd);
					today = -1;
					close(input_fd);
					restart_cmd = 1;
					goto l_socket;
				}
				if (n!=2){
					LOG("missed bytes before sync: %d bytes\n",n);
				}

				frame.SYNC = 0xa116;
				
				void * address = &frame.MMYY;
				int data_read = 8204;		// size of vr2 frame without sync

				n=read_data(input_fd, address, data_read);
				if (n<=0){
					close(input_fd);
					restart_cmd = 1;
					today = -1;
					restart_vr2(serial_fd);
					if (n<0){
						LOG ("Connection terminated by VR2  %04d.%02d.%02d. %02d:%02d:%02d at recv\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
						fprintf (logfile,"Connection terminated by VR2  %04d.%02d.%02d. %02d:%02d:%02d at recv\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
						fflush(logfile);
					}
					if (n==0){
						if (err_dispd == 0) {
							fprintf (logfile,"No incoming data in last 2s  %04d.%02d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
							fflush(logfile);
							LOG("No incoming data in last 2s  %04d.%0d.%02d. %02d:%02d:%02d\n", 1900+pltime->tm_year, pltime->tm_mon+1, pltime->tm_mday, pltime->tm_hour, pltime->tm_min, pltime->tm_sec);
							err_dispd = 1;
						}
					}
					goto l_socket;
				}
				
				if ((raw&0x01) != 0){
					write( output_fd, &frame, 8206);
				}
				if ((raw&0x02) != 0){
					write (raw_fd, &frame.data, 8192);
				}
				if ((raw&0x04) != 0){
					write( frame_fd, &frame, 8206);
				}
			}
		}
    }
	LOG("exit at end\n");
    
	serial_close(&serial_fd);
    close (input_fd);
    if (output_fd>0)
    	close (output_fd);
    if (raw_fd>0){
    	close (raw_fd);
    }
    if (frame_fd>0){
    	close (frame_fd);
    }
    fclose(logfile);
	return 0;
}
