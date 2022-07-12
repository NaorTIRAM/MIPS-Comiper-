#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IF 0
#define ID 1
#define EX 2
#define MEM 3
#define WB 4
#define COMMAND_LEN 5
#define COMMANDS_NUM 13

typedef struct Control
{
	/*Executoion/ALU stage signals:*/
	int RegDst;
	int ALUOp0;
	int ALUOp1;
	int ALUSrc;
	/*Memory stage signals:*/
	int Branch;
	int MemRead;
	int MemWrite;
	/*Write back stage signals:*/
	int RegWrite;
	int MemToReg;
}Control;

typedef struct Command
{
	char type;			/*'R'=r type, 'I'=i type alu, 'L'=lw, 'S'=sw, 'B'=branch, 'J'=jump, '0'=bubble/stall*/
	char name[COMMAND_LEN];
	int address;
	int operand1;
	int operand2;
	int operand3;
	char command[30];
	Control signals;	/*control signals for the give command*/
}Command;

typedef struct Pipeline
{
	Command stage[5];
	int forward;
	int branch;
}Pipeline;

/*Global variables*/
char commands[][5] = { "add","sub","and","or","addi","subi","ori","andi","lw","sw","beq","bneq","j" }; //i=: 0-3 r type, 4-11 i type, 12-j type
Control rControl = { 1,1,0,0,0,0,0,1,0 };
Control lControl = { 0,0,0,1,0,1,0,1,1 };
Control sControl = { 0,0,0,1,0,0,1,0,0 };
Control bControl = { 0,0,1,0,1,0,0,0,0 };
Control iControl = { 0,0,0,1,0,0,0,1,0 };
Control stallControl = { 0,0,0,0,0,0,0,0,0 };
float cycle = 0, stalls = 0;

/*Managing functions*/
void nextCycle(FILE* traceFile, Pipeline* pipe);	//pipe[0]=pipe[1] in the end NEXT CLOCK CYCLE
char getCommandType(char* commandName);		//return 'R'=r type, 'I'=i type alu, 'L'=lw, 'S'=sw, 'B'=branch, 'J'=jump, '0'=bubble/stall
void stall(Pipeline* pipe, int location);	//pipe[location] will be stalled
Command addBubble();						//returns a bubble/stall
void printCPI();							//print CPI
void printfCycleInfo(Pipeline pipe);		//print current cycle pipeline stages and count stalls
void initPipe(Pipeline* pipe);				//initiate pipe to 'no commands'
int convertToBinary(int origin);			//convert given value to binary
void bStall(Pipeline* pipe, int location);	//pipe[location] will be stalled
void flush(Pipeline* pipe);					//flush for forward=0
void flush1(Pipeline* pipe);				//flush for forward=1
void endFile(Pipeline* pipe);				//close file - add 4 nops

/*File functions*/
Command readCommand(FILE* filePtr);			//read a command line
Command endOfFile();						//nop command generate

//The main.
void main(int argc, char* argv[])
{
	int option;
	Pipeline pipe;
	initPipe(&pipe);
	printf("Enter 1 for trace1.txt, 2 for trace2.txt\n");
	scanf("%d", &option);
	fseek(stdin, 0, SEEK_SET);
	switch (option)
	{
	case 1:
	{
		FILE* traceFile = fopen("trace1.txt", "r");
		break;
	}
	case 2:
	{
		FILE* traceFile = fopen("trace2.txt", "r");
		break;
	}
	default:
	{
		printf("Wrong input, the program will shut down!\n\n");
		exit(0);
	}
	}
	FILE* traceFile = fopen("trace1.txt", "r");
	pipe.forward = argv[1];
	pipe.branch = argv[2];
	nextCycle(traceFile, &pipe);
	printCPI();
	fclose(traceFile);
}

/*Pipeline functions*/
void nextCycle(FILE* traceFile, Pipeline* pipe)
{
	int i, stl;
	Command newCommand;
	Command tmp;
	while (1)
	{
		stl = 0;
		newCommand = readCommand(traceFile);
		if (!(strcmp(newCommand.name, "END OF FILE")))	//exit condition
		{
			endFile(pipe);
			return;
		}
		for (i = 4; i > 0; i--)
		{
			pipe->stage[i] = pipe->stage[i - 1];
		}
		pipe->stage[IF] = newCommand; //check memory allocation
		cycle++;
		printfCycleInfo(*pipe);
		if (pipe->forward == 0)	/*Forward defined as 0 branch (No forwarding)*/
		{
			if ((pipe->stage[EX].signals.RegWrite) && (pipe->stage[ID].operand2 == pipe->stage[EX].operand1 || pipe->stage[ID].operand3 == pipe->stage[EX].operand1))
			{
				stall(pipe, ID);	//example: [ID]=add $t0 $t1 $t2 , [EX]=sub $t1 $t2 $t3
				cycle++;
				printfCycleInfo(*pipe);
			}
			if ((pipe->stage[MEM].signals.RegWrite) && (pipe->stage[ID].operand2 == pipe->stage[MEM].operand1 || pipe->stage[ID].operand3 == pipe->stage[MEM].operand1))
			{
				stall(pipe, ID);	//example: [ID]=add $t0 $t1 $t2 , [MEM]=sub $t1 $t2 $t3
				cycle++;
				printfCycleInfo(*pipe);
			}
			if (pipe->branch == 0)	/*Branch in MEM stage*/
			{
				if ((pipe->stage[ID].signals.Branch) && pipe->stage[IF].address != pipe->stage[ID].address + 4)
				{
					flush1(pipe);
				}
			}
			else
			{
				if ((pipe->stage[ID].signals.Branch) && pipe->stage[IF].address != pipe->stage[ID].address + 4)
				{
					stall(pipe, ID);
					cycle++;
					printfCycleInfo(*pipe);
				}
			}
		}
		else
		{
			if ((pipe->stage[EX].signals.MemRead) && (pipe->stage[ID].operand2 == pipe->stage[EX].operand1 || pipe->stage[ID].operand3 == pipe->stage[EX].operand1))
			{
				stall(pipe, ID);	//example: [ID]=add $t0 $t1 $t2 , [EX]=lw $t1 10 $t3
				cycle++;
				printfCycleInfo(*pipe);
			}
			if (pipe->branch == 0)	/*Branch in MEM stage*/
			{
				if ((pipe->stage[ID].signals.Branch) && pipe->stage[IF].address != pipe->stage[ID].address + 4)
				{
					flush1(pipe);
				}
			}
			else         /*Branch in MEM stage*/
			{
				if ((pipe->stage[ID].signals.Branch) && pipe->stage[IF].address != pipe->stage[ID].address + 4)
				{
					stall(pipe, ID);
					cycle++;
					printfCycleInfo(*pipe);
				}
			}
		}
	}
}

void flush1(Pipeline* pipe)
{
	int i, j;
	for (j = 0; j < 2; j++)
	{
		for (i = 4; i > 1; i--)
		{
			pipe->stage[i] = pipe->stage[i - 1];
		}
		pipe->stage[i] = addBubble();			//check memory if works
		cycle++;
		//stalls++;
		printfCycleInfo(*pipe);
	}
}

void stall(Pipeline* pipe, int location)	//pipe[location] will be stalled
{
	int i;
	for (i = 4; i > location + 1; i--)
	{
		pipe->stage[i] = pipe->stage[i - 1];
	}
	pipe->stage[i] = addBubble();			//check memory if works
	//stalls++;
}

void flush(Pipeline* pipe)			//add 3 stalls
{
	int i, j;
	for (j = 0; j < 3; j++)
	{
		for (i = 4; i > 1; i--)
		{
			pipe->stage[i] = pipe->stage[i - 1];
		}
		pipe->stage[i] = addBubble();			//check memory if works
		cycle++;
		//stalls++;
		printfCycleInfo(*pipe);
	}
}

void bStall(Pipeline* pipe)	//pipe[location] will be stalled
{
	int i, j;
	for (j = 0; j < 3; j++)
	{
		for (i = 4; i > IF - 1; i--)
		{
			pipe->stage[i] = pipe->stage[i - 1];
		}
		pipe->stage[i] = addBubble();			//check memory if works
		cycle++;
		printfCycleInfo(*pipe);
	}
}


Command addBubble()	//returns a bubble/stall
{
	int i;
	Command bubble;
	bubble.type = '0';
	strcpy(bubble.name, "stll");
	strcpy(bubble.command, "stall");
	bubble.operand1 = 0;
	bubble.operand2 = 0;
	bubble.operand3 = 0;
	bubble.signals = stallControl;
	return bubble;
}

/*Assisting functions*/
char getCommandType(char* commandName)
{
	int i, j, t;
	for (i = 0; i < COMMANDS_NUM; i++)
	{
		t = 0;
		j = 0;
		do {
			if (commandName[t] != commands[i][j])
				break;
			t++;
			j++;
			if (commandName[t] == commands[i][j] && commandName[t] == '\0')
			{
				if (i > 11)
				{
					return 'J';
				}
				else if (i > 9)
				{
					return 'B';
				}
				else if (i > 8)
				{
					return 'S';
				}
				else if (i > 7)
				{
					return 'L';
				}
				else if (i > 3)
				{
					return 'I';
				}
				else return 'R';
			}
		} while (commandName[t]);
	}
}

int convertToBinary(int origin)
{
	int bin = 0, rem, t = 1;
	while (origin)
	{
		rem = origin % 2;
		bin = bin + rem * t;
		t *= 10;
		origin /= 2;
	}
	return bin;
}

long toBin(int dno)
{
	long bno = 0, remainder, f = 1;
	while (dno != 0)
	{
		remainder = dno % 2;
		bno = bno + remainder * f;
		f = f * 10;
		dno = dno / 2;
	}
	return bno;
}

void printCPI()
{
	float CPI = cycle / (cycle - stalls - 4);
	printf("CPI: %f\n", CPI);
}

void printfCycleInfo(Pipeline pipe)
{
	printf("Clock %d:\n", (int)cycle);
	printf("Fetch instruction: %s\n", pipe.stage[IF].command);
	printf("Decode instruction: %s\n", pipe.stage[ID].command);
	printf("Execute instruction: %s\n", pipe.stage[EX].command);
	printf("Memory instruction: %s\n", pipe.stage[MEM].command);
	printf("Writeback instruction: %s\n", pipe.stage[WB].command);
	if (!strcmp(pipe.stage[WB].command, "stall")) stalls++;
}

void endFile(Pipeline* pipe)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 4; j > 0; j--)
		{
			pipe->stage[j] = pipe->stage[j - 1];
		}
		pipe->stage[0] = addBubble();
		strcpy(&pipe->stage[j].name, "Empty");
		strcpy(&pipe->stage[j].command, "No Command");
		cycle++;
		printfCycleInfo(*pipe);
	}
}

/*File functions*/
Command readCommand(FILE* filePtr)
{
	int i = 0, init = ftell(filePtr), tmp;
	Command newCommand;
	char ch;
	fscanf(filePtr, "%d", &newCommand.address);		//check where is the pointer - if ' ' or after
	fseek(filePtr, 1, SEEK_CUR);					//if in space
	do {
		ch = fgetc(filePtr);
		if (ch != ' ' && ch != '$')
			newCommand.name[i++] = ch;
	} while (ch != '$' && ch != EOF);
	newCommand.name[i] = '\0';
	newCommand.type = getCommandType(newCommand.name);
	switch (newCommand.type)
	{
	case 'R':	//R type
	{
		newCommand.signals = rControl;
		//fseek(filePtr, 1, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand1 = convertToBinary(tmp);
		fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand2 = convertToBinary(tmp);
		fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand3 = convertToBinary(tmp);
		break;
	}

	case 'I':	//i alu type
	{
		newCommand.signals = iControl;
		//fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand1 = convertToBinary(tmp);
		fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand2 = convertToBinary(tmp);
		fseek(filePtr, 1, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand3 = convertToBinary(tmp);
		break;
	}

	case 'L':	//lw type
	{
		newCommand.signals = lControl;
		//fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand1 = convertToBinary(tmp);
		fseek(filePtr, 1, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand2 = convertToBinary(tmp);
		fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand3 = convertToBinary(tmp);
		break;
	}

	case 'S':	//sw type
	{
		newCommand.signals = sControl;
		//fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand2 = convertToBinary(tmp);
		fseek(filePtr, 1, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand3 = convertToBinary(tmp);
		fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand1 = convertToBinary(tmp);
		break;
	}

	case 'B':	//branch type
	{
		newCommand.signals = bControl;
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand1 = convertToBinary(tmp);
		fseek(filePtr, 1, SEEK_CUR);
		fscanf(filePtr, "%d", &tmp);
		newCommand.operand2 = convertToBinary(tmp);
		fseek(filePtr, 2, SEEK_CUR);
		fscanf(filePtr, "%s", &tmp);
		break;
	}

	/*case 'J':	//jump type
	{
		newCommand.signals = jControl;

	}*/
	}


	fseek(filePtr, init + 5, SEEK_SET);
	i = 0;
	while (ch != '\n' && ch != EOF)
	{
		ch = fgetc(filePtr);
		if (ch != '\n' && ch != EOF)
			newCommand.command[i++] = ch;		//check if need ='\0' and if last char is '\n' or new line already
	}
	newCommand.command[i] = '\0';
	if (ch == EOF) return endOfFile();
	return newCommand;
}

Command endOfFile()
{
	Command end = addBubble();
	strcpy(&end.name, "END OF FILE");
	return end;
}

void initPipe(Pipeline* pipe)
{
	int i = 0;
	for (; i < 5; i++)
	{
		pipe->stage[i] = addBubble();
		strcpy(&pipe->stage[i].name, "Empty");
		strcpy(&pipe->stage[i].command, "No Command");
	}
}