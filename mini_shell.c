#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h> 
#include <fcntl.h>


/**************************/
/* Name: Netanel Draiman  */
/*    ID: 200752152       */
/*       Home Ex3         */
/**************************/


#define true 1
#define false 0
#define MAX_CHARS_ARGS 511 //510 + 1 for \n
#define EXEC_FAILED 255 //exit status for failing command

char** readTokens(FILE*); //read input
int countTokens(const char*); //count tokens - seperated by space
int isAllSpaces(char*); //check if input is all spaces
void freeTokens (char**); //free tokens array from memory

void interruptHandler(int); //Intercept Ctrl+C for main program
int searchDollarSign(const char**); //search for argument with $ sign
int getNumOfArgs(const char**); //Returns number of arguments in Tokens array
int execCD (const char**); //Handles 'cd' command

int checkRedirection(const char**); //search for Redirection\Pipe tokens
int redirect(const char**, int, int); //redirects output\input
char* getPath(const char**, int); //gets path + filename for redirect method
char** removeSigns(char**, int, int); //remove Redirection\Pipe signs
int getNonSigns(const char**, int); //gets num of args != Redirection\Pipe signs

/****************************************/
/*********** Main Program ***************/
/****************************************/

//Global Variables to Handle Pipe.
int pipeFlag, pipeIndex;

int main () {
	char host[HOST_NAME_MAX];
	char* login = getlogin();
	gethostname(host, HOST_NAME_MAX);
	
	const char* QUIT = "exit"; //Quit Command
	int i, dollar, exitStatus;
	
	signal(SIGINT, interruptHandler); //Intercept Ctrl+C
	pid_t pid[2]; //Pid array
	int pipe_desc[2];
	
	exitStatus = 0;
	
	while(true) {
		pipeFlag = 0;
		pipeIndex = -1;
		//pid[1]=1;
		
		//Prompt
		printf("%d %s@%s: ", WEXITSTATUS(exitStatus), login, host); 
		
		//Read Input
		char** tokens = readTokens(stdin);
		if (tokens == NULL)
			continue;
			
		//check for QUIT command
		else if(!strcmp(tokens[0], QUIT) 
				&& getNumOfArgs((const char**)tokens) == 0) {
			freeTokens(tokens);
			printf("Quitting... \n");
			return 0;
		}
		//check for $ sign
		dollar = searchDollarSign((const char**)tokens);
		if(dollar) { //!=0
			fprintf(stderr, "ERROR: Our minishell cannot handle the $ sign for argument %d \n", dollar);
			freeTokens(tokens);
			continue;
		}
		
		//Handle CD command seperately using system call
		if(!strcmp(tokens[0], "cd")) {
			exitStatus = execCD((const char**)tokens);
			freeTokens(tokens);
			continue;
		}
		
		//Checking for Redirection
		int numOfRedirections = checkRedirection((const char**)tokens);
		if(numOfRedirections == -1) {
			fprintf(stderr, "ERROR: Syntax error - unexpected token...\n");
			freeTokens(tokens);
			continue;
		}
		
		
		//Handle rest of Commands using Child Processes
		
		//Create pipe if flag was raised.
		if(pipeFlag) {
			if(pipe(pipe_desc) == -1) {
				fprintf(stderr, "ERROR: opening Pipe Failed!\n");
				freeTokens(tokens);
				exit(EXEC_FAILED);
			}
		}

		pid[0] = fork();
		if(pid[0] == -1) {
			fprintf(stderr, "ERROR: Failed to Create Child Process...\n");
			freeTokens(tokens);
			exit(EXEC_FAILED);
		}
		else if(pid[0] && pipeFlag) {
			pid[1] = fork();
			if(pid[1] == -1) {
				fprintf(stderr, "ERROR: Failed to Create Child Process...\n");
				freeTokens(tokens);
				exit(EXEC_FAILED);
			}
		}
		if(!pid[0]) { //First Child Code
			if(pipeFlag) { //Redirect to Pipe
				close(pipe_desc[0]);
				if(dup2(pipe_desc[1], STDOUT_FILENO)==-1)
					fprintf(stderr, "ERROR: Failed to Redirect 1st Child Output to Pipe\n");
			}
			//Reset Signal Handler for Child Process
			signal(SIGINT, SIG_DFL); 
			
			//Redirection
			if(numOfRedirections) { //found redirection symbols in user input
				if(redirect((const char**)tokens, numOfRedirections, 0)) { //redirecting input\output
					fprintf(stderr, "ERROR: Redirection Failed...\n");
					freeTokens(tokens);
					exit(EXEC_FAILED);
				}	
			}
			//Remove Redirect\Pipe signs from tokens
			tokens = removeSigns(tokens, numOfRedirections, 0);	
			
			execvp(tokens[0], tokens);
			
			//Following Code is Reached only on execvp Failure
			fprintf(stderr, "ERROR: Cannot Execute Command...\n");
			freeTokens(tokens);
			exit(EXEC_FAILED);
		}
		else if(!pid[1]) { //Second Child Code - only used for commands with pipe
			close(pipe_desc[1]);
			if(dup2(pipe_desc[0], STDIN_FILENO)==-1)
				fprintf(stderr, "ERROR: Failed to Redirect 2nd Child Input to Pipe\n");
			
			signal(SIGINT, SIG_DFL);
			
			if(numOfRedirections) {
				if(redirect((const char**)tokens, numOfRedirections, pipeIndex+1)) {
					fprintf(stderr, "ERROR: Redirection Failed...\n");
					freeTokens(tokens);
					exit(EXEC_FAILED);
				}
			}
			
			tokens = removeSigns(tokens, numOfRedirections, pipeIndex+1);
			
			execvp(tokens[0], tokens);
			
			//Following Code is Reached only on execvp Failure
			fprintf(stderr, "ERROR: Cannot Execute Command...\n");
			freeTokens(tokens);
			exit(EXEC_FAILED);
		}
		
		else {
			//Parent doesnt use pipe - closing it.
			if(pipeFlag) {
				close(pipe_desc[0]);
				close(pipe_desc[1]);
			}
			
			//Main Prog wait for Children Processes to end.
			waitpid(pid[0], &exitStatus, 0);
			if(pipeFlag)
				waitpid(pid[1], &exitStatus, 0);
			
					
		}
		freeTokens(tokens);
	}
	return 0;
}

/**********************************************/
/*********** Additional Methods ***************/
/**********************************************/

/********** Methods for Interperting Input **********/

//Read input, divide to tokens in char** array
char** readTokens(FILE* stream) {
	int numOfArgs;
	char input[MAX_CHARS_ARGS];
	char* delimiters = " \n"; //used for strtok
	int i, length;
	
	//Receive user input
	fgets(input, MAX_CHARS_ARGS, stream);
	if(input == NULL) {
		fprintf(stderr, "ERROR: Failed to receive Input \n");
		return NULL;
	}
	//check for command line made of only spaces
	else if(isAllSpaces(input))
		return NULL;
	
	//count number of arguments entered
	numOfArgs = countTokens(input);
	if(!numOfArgs) //user entered blank line - no arguments
		return NULL;
	
	//initialize Tokens array
	char** tokens = (char**)malloc((numOfArgs+1) * sizeof(char*));
	if(tokens == NULL) {
		fprintf(stderr, "ERROR: Failed to allocate memory \n");
		return NULL;
	}
		
	
	//Dividing Input into tokens
	char* temp = strtok(input, delimiters);
	for(i=0; temp != NULL; i++) {
		length = strlen(temp);
		tokens[i] = (char*)malloc((length+1)*sizeof(char));
		if(tokens[i] == NULL) {
			fprintf(stderr, "ERROR: Failed to allocate memory \n");
			return NULL;
		}
		strcpy(tokens[i], temp);
		tokens[i][length] = '\0';
		temp = strtok(NULL, delimiters); //move temp to the next token
	}
	tokens[i] = NULL; //tokens ends in NULL
	return tokens;
}

//counting tokens seperate by space in the input
int countTokens(const char* input) {
	if(!strcmp(input, "\n"))
		return 0;

	int counter=1; //command counts as first argument
	int length = strlen(input);
	char* temp;
	temp = strchr(input, ' ');
	
	while(temp!=NULL) {
		temp = strchr(temp+1, ' ');
		counter++;
	}
	return counter;
}

//check if string is made of only white spaces
int isAllSpaces(char* input) {
	int i, length, counter=0;
	length = strlen(input);
	for(i=0; i < length; i++)
		if(input[i] == ' ')
			counter++;
	if(counter == length-1)
		return true;
	return false;
}

/**********************************/
/********** Misc Methods **********/

//Free Memory - tokens array
void freeTokens (char** tokens) {
	int i;
	for(i=0; tokens[i]!=NULL; i++)
		free(tokens[i]);
	free(tokens);
}

//Signal Handler for Ctrl+C - used in main program
void interruptHandler(int signum) {
	
}

/****************************************************/
/********** Methods for Executing Commands **********/

//Search Argument with $ sign
int searchDollarSign(const char** tokens) {
	int i, dollarIndex;
	dollarIndex=0;
	for(i=0; tokens[i] != NULL; i++)
		if(!strncmp(tokens[i], "$", 1))
			if(!dollarIndex) //== 0		
				dollarIndex = i;	
	return dollarIndex;
}

//get the number of arguments(not including executable) in Tokens array
int getNumOfArgs(const char** tokens) {
	int i, numOfArgs;
	numOfArgs=0;
	for(i=1; tokens[i] != NULL; i++)
		numOfArgs++;
	return numOfArgs;
}

//Handling 'cd' executable
int execCD (const char** tokens) {
	int numOfArgs;
	numOfArgs = getNumOfArgs(tokens);

	if(numOfArgs==0) {
		fprintf(stderr, 
			"ERROR: too few arguments for executable 'cd'\n");
		return EXEC_FAILED;
	}
	else if(numOfArgs > 1) {
		fprintf(stderr, 
			"ERROR: too many arguments for executable 'cd'\n");
		return EXEC_FAILED;	
	}
		
	if(chdir(tokens[1]) == -1) {
		fprintf(stderr, "ERROR: No such file or directory\n");
		return EXEC_FAILED;
	}
	return 0;
}

/*******************************************************************/
/********** Methods for Handling Input\Output Redirection **********/


//Check if input includes Redirection\Pipe tokens legally
int checkRedirection(const char** tokens) {
	//inputSign = <, outputSign = >, appendSign = >>
	int i, numOfSigns=0, inputSign=-1, outputSign=-1, appendSign=-1, pipeSign=-1;
	
	for(i=0; tokens[i] != NULL; i++) {
		if(!strncmp(tokens[i], "<", 1) && strlen(tokens[i])==1)
			//check there are no adjacent signs
			if(inputSign==i-1 || outputSign==i-1 || appendSign==i-1 || pipeSign==i-1)
				return -1;
			//check there are no >, >> before <
			else if((outputSign<i && outputSign!=-1) || (appendSign<i && appendSign!=-1))
				return -1;
			//check there is only 1 token after <, that isnt >, >>
			else if(tokens[i+2]!=NULL && (strncmp(tokens[i+2], ">>", 2) && strncmp(tokens[i+2], ">", 1) && strncmp(tokens[i+2], "|", 1)))
				return -1;
			else {
				inputSign = i;
				numOfSigns++;
			}
		else if(!strncmp(tokens[i], ">>", 2) && strlen(tokens[i])==2)
			if(inputSign==i-1 || outputSign==i-1 || appendSign==i-1 || pipeSign==i-1)
				return -1;
			else if(tokens[i+2] != NULL)
				return -1;
			else {
				appendSign = i;
				numOfSigns++;
			}
		else if(!strncmp(tokens[i], ">", 1) && strlen(tokens[i])==1)
			if(inputSign==i-1 || outputSign==i-1 || appendSign==i-1 || pipeSign==i-1)
				return -1;
			else if(tokens[i+2] != NULL)
				return -1;
			else {
				outputSign = i;
				numOfSigns++;
			}
		else if(!strncmp(tokens[i], "|", 1)) {
			//mini shell supports only 1 pipe
			if(pipeSign != -1)
				return -1;
			else if(inputSign==i-1 || outputSign==i-1 || appendSign==i-1 || pipeSign==i-1)
				return -1;
			else if((outputSign<i && outputSign!=-1) || (appendSign<i && appendSign!=-1))
				return -1;
			else {
				pipeSign = i;
				pipeIndex = i;
				pipeFlag = 1;
			}
		}
	}
	//check there are no signs as last token
	int numOfArgs = getNumOfArgs(tokens);
	if(inputSign==numOfArgs || outputSign==numOfArgs || appendSign==numOfArgs || pipeSign==numOfArgs)
		return -1;
	
	return numOfSigns;
}

//Handles Input\Output Redirection
int redirect(const char** tokens, int numOfSigns, int start) {
	int i, fd;
	char* path;
	
	//redirect output\input according to redirection sign
	for(i=start; tokens[i] != NULL && numOfSigns > 0 && strncmp(tokens[i], "|", 1); i++) {
		if(!strncmp(tokens[i], "<", 1)) {
			path = getPath(tokens, i);
			fd = open(path, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
			free(path);
			if(fd == -1) {
				fprintf(stderr, "ERROR: Opening File Failed!\n");
				return -1;
			}
			if(dup2(fd, STDIN_FILENO)==-1)
				fprintf(stderr, "ERROR: Couldnt Redirect '<'!\n");
			numOfSigns--;
		}
		else if(!strncmp(tokens[i], ">>", 2)) {
			path = getPath(tokens, i);
			fd = open(path, O_WRONLY | O_APPEND | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
			free(path);
			if(fd == -1) {
				fprintf(stderr, "ERROR: Opening File Failed!\n");
				return -1;
			}
			if(dup2(fd, STDOUT_FILENO)==-1)
				fprintf(stderr, "ERROR: Couldnt Redirect '>>'!\n");
			numOfSigns--;
		}
		else if(!strncmp(tokens[i], ">", 1)) {
			path = getPath(tokens, i);
			fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
			free(path);
			if(fd == -1) {
				fprintf(stderr, "ERROR: Opening File Failed!\n");
				return -1;
			}
			if(dup2(fd, STDOUT_FILENO)==-1) {
				fprintf(stderr, "ERROR: Couldnt Redirect '>'!\n");
				return -1;	
			}
			numOfSigns--;
		}
	
	
	}
	return 0;
}

//returns (path + filename) for Redirect method
char* getPath(const char** tokens, int i) {
	char* path = (char*)malloc((PATH_MAX+1)*sizeof(char));
	if(path == NULL)
		return NULL;
	if(getcwd(path, PATH_MAX+1) == NULL) {
		free(path);
		return NULL;
	}
	strcat(path, "/");
	const char* fileName = tokens[i+1];
	strcat(path, fileName);
	return path;
}

//Remove Redirection\Pipe signs from tokens array - execvp cant handle signs
char** removeSigns(char** tokens, int numOfSigns, int start) {
	//return if 1st Child & no Redirection\Pipe signs
	//1st Child gets start=0.
	if(!numOfSigns && !start & !pipeFlag)
		return tokens;

	int i,j, tempSize;
	tempSize = 1 + getNonSigns((const char**)tokens, start); //+1 for NULL
	
	char** temp = (char**)malloc((tempSize)*sizeof(char*));
	
	//free all tokens before start index
	for(i=0; i < start-1; i++)
		free(tokens[i]);
	for(i=start, j=0; tokens[i]!=NULL && strncmp(tokens[i], "|", 1); i++) {
		if(!strncmp(tokens[i], "<", 1) || !strncmp(tokens[i], ">>", 2) || !strncmp(tokens[i], ">", 1)) {
			//free sign and filename tokens
			free(tokens[i]);
			free(tokens[i+1]);
			i++;	
		}
		else {
			//copy non-sign tokens
			temp[j] = tokens[i];
			j++;
		}
	}
	temp[j] = NULL; //last token must be NULL
	
	if(pipeFlag) { //free pipe token
		free(tokens[pipeIndex]);
	}
	if(pipeFlag && !start) //1st child, free tokens after pipe sign
		for(i=pipeIndex+1; tokens[i]!=NULL; i++)
			free(tokens[i]);
	free(tokens);
	return temp;
}

//return number of args != Redirection\Pipe signs
//starting from 'start' index.
int getNonSigns(const char** tokens, int start) {
	int nonSigns=0;
	int i;
	
	for(i=start; tokens[i]!=NULL && strncmp(tokens[i], "|", 1); i++)
		if(!strncmp(tokens[i], "<", 1) || !strncmp(tokens[i], ">>", 2) || !strncmp(tokens[i], ">", 1))
			i++;
		else
			nonSigns++;
			
	return nonSigns;
}




