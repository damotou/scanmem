/*
 $Id: main.c,v 1.25 2007-06-07 15:16:46+01 taviso Exp taviso $

 Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
 Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
 Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <getopt.h>             /*lint -e537 */
#include <signal.h>
#include <stdbool.h>
#include<pthread.h>
#include <netinet/in.h>    // for sockaddr_in
#include <sys/types.h>    // for socket
#include <sys/socket.h>

#include "scanmem.h"
#include "commands.h"
#include "handlers.h"
#include "show_message.h"

#define HELLO_WORLD_SERVER_PORT    7777
#define LENGTH_OF_LISTEN_QUEUE 20
#define BUFFER_SIZE 1024
#define FILE_NAME_MAX_SIZE 512

static void printhelp(void);
static void sighandler(int n);
static void startserver();
/* global settings */
globals_t globals = { 0, /* exit flag */
0, /* pid target */
NULL, /* matches */
0, /* match count */
NULL, /* regions */
NULL, /* commands */
NULL, /* current_cmdline */
/* options */
{ 1, /* alignment */
0, /* debug */
0, /* backend */
ANYINTEGER, /* scan_data_type */
REGION_HEAP_STACK_EXECUTABLE_BSS, /* region_detail_level */
0, /* detect_reverse_change */
} };
globals_t *vars;
struct result {
	int size;
	char buffer[1020];
}*resultbuf;

int main(int argc, char **argv) {
	char *end;
	int optindex, ret = EXIT_SUCCESS;
	vars = &globals;

	struct option longopts[] = { { "pid", 1, NULL, 'p' }, /* target pid */
	{ "version", 0, NULL, 'v' }, /* print version */
	{ "help", 0, NULL, 'h' }, /* print help summary */
	{ "backend", 0, NULL, 'b' }, /* run as backend */
	{ "debug", 0, NULL, 'd' }, /* enable debug mode */
	{ NULL, 0, NULL, 0 }, };

	/* process command line */
	while (true) {
		switch (getopt_long(argc, argv, "vhbdp:", longopts, &optindex)) {
		case 'p':
			vars->target = (pid_t) strtoul(optarg, &end, 0);

			/* check if that parsed correctly */
			if (*end != '\0' || *optarg == '\0' || vars->target == 0) {
				show_error("invalid pid specified.\n");
				return EXIT_FAILURE;
			}
			break;
		case 'v':
			printversion(stderr);
			return EXIT_SUCCESS;
		case 'h':
			printhelp();
			return EXIT_SUCCESS;
		case 'd':
			vars->options.debug = 1;
			break;
		case -1:
			goto done;
		default:
			printhelp();
			return EXIT_FAILURE;
		}
	}
	done:
	vars->options.backend = 1;
	/* parse any pid specified after arguments */
	if (optind <= argc && argv[optind]) {
		vars->target = (pid_t) strtoul(argv[optind], &end, 0);

		/* check if that parsed correctly */
		if (*end != '\0' || argv[optind][0] == '\0' || vars->target == 0) {
			show_error("invalid pid specified.\n");
			return EXIT_FAILURE;
		}
	}
	resultbuf = malloc(sizeof(struct result));
	/* before attaching to target, install signal handler to detach on error */
	if (vars->options.debug == 0) /* in debug mode, let it crash and see the core dump */
	{
		(void) signal(SIGHUP, sighandler);
		(void) signal(SIGINT, sighandler);
		(void) signal(SIGSEGV, sighandler);
		(void) signal(SIGABRT, sighandler);
		(void) signal(SIGILL, sighandler);
		(void) signal(SIGFPE, sighandler);
		(void) signal(SIGTERM, sighandler);
	}

	/* linked list of commands, and function pointers to their handlers */
	if ((vars->commands = l_init()) == NULL) {
		show_error("sorry, there was a memory allocation error.\n");
		ret = EXIT_FAILURE;
		goto end;
	}

	/* NULL shortdoc means dont display this command in `help` listing */
	registercommand("set", handler__set, vars->commands, SET_SHRTDOC,
			SET_LONGDOC);
	registercommand("list", handler__list, vars->commands, LIST_SHRTDOC,
			LIST_LONGDOC);
	registercommand("delete", handler__delete, vars->commands, DELETE_SHRTDOC,
			DELETE_LONGDOC);
	registercommand("reset", handler__reset, vars->commands, RESET_SHRTDOC,
			RESET_LONGDOC);
	registercommand("pid", handler__pid, vars->commands, PID_SHRTDOC,
			PID_LONGDOC);
	registercommand("snapshot", handler__snapshot, vars->commands,
			SNAPSHOT_SHRTDOC, SNAPSHOT_LONGDOC);
	registercommand("dregion", handler__dregion, vars->commands,
			DREGION_SHRTDOC, DREGION_LONGDOC);
	registercommand("dregions", handler__dregion, vars->commands, NULL,
			DREGION_LONGDOC);
	registercommand("lregions", handler__lregions, vars->commands,
			LREGIONS_SHRTDOC, LREGIONS_LONGDOC);
	registercommand("version", handler__version, vars->commands,
			VERSION_SHRTDOC, VERSION_LONGDOC);
	registercommand("=", handler__decinc, vars->commands, NOTCHANGED_SHRTDOC,
			NOTCHANGED_LONGDOC);
	registercommand("!=", handler__decinc, vars->commands, CHANGED_SHRTDOC,
			CHANGED_LONGDOC);
	registercommand("<", handler__decinc, vars->commands, LESSTHAN_SHRTDOC,
			LESSTHAN_LONGDOC);
	registercommand(">", handler__decinc, vars->commands, GREATERTHAN_SHRTDOC,
			GREATERTHAN_LONGDOC);
	registercommand("+", handler__decinc, vars->commands, INCREASED_SHRTDOC,
			INCREASED_LONGDOC);
	registercommand("-", handler__decinc, vars->commands, DECREASED_SHRTDOC,
			DECREASED_LONGDOC);
	registercommand("\"", handler__string, vars->commands, STRING_SHRTDOC,
			STRING_LONGDOC);
	registercommand("update", handler__update, vars->commands, UPDATE_SHRTDOC,
			UPDATE_LONGDOC);
	registercommand("exit", handler__exit, vars->commands, EXIT_SHRTDOC,
			EXIT_LONGDOC);
	registercommand("quit", handler__exit, vars->commands, NULL, EXIT_LONGDOC);
	registercommand("q", handler__exit, vars->commands, NULL, EXIT_LONGDOC);
	registercommand("help", handler__help, vars->commands, HELP_SHRTDOC,
			HELP_LONGDOC);
	registercommand("shell", handler__shell, vars->commands, SHELL_SHRTDOC,
			SHELL_LONGDOC);
	registercommand("watch", handler__watch, vars->commands, WATCH_SHRTDOC,
			WATCH_LONGDOC);
	registercommand("show", handler__show, vars->commands, SHOW_SHRTDOC,
			SHOW_LONGDOC);
	registercommand("dump", handler__dump, vars->commands, DUMP_SHRTDOC,
			DUMP_LONGDOC);
	registercommand("write", handler__write, vars->commands, WRITE_SHRTDOC,
			WRITE_LONGDOC);
	registercommand("option", handler__option, vars->commands, OPTION_SHRTDOC,
			OPTION_LONGDOC);
	registercommand("read", handler__read, vars->commands, READ_SHRTDOC,
			READ_LONGDOC);
	/* commands beginning with __ have special meaning */
	registercommand("__eof", handler__eof, vars->commands, NULL, NULL);

	/* special value NULL means no other matches */
	registercommand(NULL, handler__default, vars->commands, DEFAULT_SHRTDOC,
			DEFAULT_LONGDOC);

	if (!(globals.options.backend)) {
		// show welcome message
		printversion(stderr);
	} else {
		// tell front-end our version
		printf("%s\n", PACKAGE_VERSION);
	}

	if (getuid() != 0) {
		show_error(
				"*** YOU ARE NOT RUNNING scanmem AS ROOT, IT MAY NOT WORK WELL. ***\n\n");
	}
	vars->options.region_scan_level = REGION_ALL;

	/* this will initialise matches and regions */
	if (execcommand(vars, "reset") == false) {
		vars->target = 0;
	}

	/* check if there is a target already specified */
	if (vars->target == 0) {
		show_user(
				"Enter the pid of the process to search using the \"pid\" command.\n");
		show_user("Enter \"help\" for other commands.\n");
	} else {
		show_user(
				"Please enter current value, or \"help\" for other commands.\n");
	}
	//建立socket 等待命令
	int err;
	pthread_t ntid;
	err = pthread_create(&ntid, NULL, startserver, NULL);
	if (err != 0)
		printf("can't create thread: %s\n", strerror(err));

	/* main loop, read input and process commands */

	while (!vars->exit) {

//		char * line;
//
//		/* reads in a commandline from the user, and returns a pointer to it in *line */
//		if (getcommand(vars, &line) == false) {
//			show_error("failed to read in a command.\n");
//			ret = EXIT_FAILURE;
//			break;
//		}
//
//		/* execcommand returning failure isnt fatal, just the a command couldnt complete. */
//		if (execcommand(vars, line) == false) {
//			if (vars->target == 0) {
//				show_user(
//						"Enter the pid of the process to search using the \"pid\" command.\n");
//				show_user("Enter \"help\" for other commands.\n");
//			} else {
//				show_user(
//						"Please enter current value, or \"help\" for other commands.\n");
//			}
//		}
//
//		free(line);
		sleep(2);
		printf("%d \n",vars->exit);
	}

	end:

	/* now free any allocated memory used */
	l_destroy(vars->regions);
	l_destroy(vars->commands);

	/* attempt to detach just in case */
	(void) detach(vars->target);
	return ret;
}

void sighandler(int n) {
	show_error("\nKilled by signal %d.\n", n);

	if (globals.target) {
		(void) detach(globals.target);
	}

	exit(EXIT_FAILURE);
}

/* print quick usage message to stderr */
void printhelp() {
	printversion(stderr);

	show_user(
			"Usage: scanmem [OPTION]... [PID]\n"
					"Interactively locate and modify variables in an executing process.\n"
					"\n"
					"-p, --pid=pid\t\tset the target process pid\n"
					"-b, --backend\t\trun as backend, used by frontend\n"
					"-h, --help\t\tprint this message\n"
					"-v, --version\t\tprint version information\n"
					"\n"
					"scanmem is an interactive debugging utility, enter `help` at the prompt\n"
					"for further assistance.\n"
					"\n" "Report bugs to <%s>.\n", PACKAGE_BUGREPORT);
	return;
}

void startserver() {
	struct sockaddr_in server_addr;
	bzero(&server_addr, sizeof(server_addr));
	//把一段内存区的内容全部设置为0
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htons(INADDR_ANY);
	server_addr.sin_port = htons(HELLO_WORLD_SERVER_PORT);
	int i = 0;
	//创建用于internet的流协议(TCP)socket,用server_socket代表服务器socket
	int server_socket = socket(PF_INET, SOCK_STREAM, 0);

	if (server_socket < 0) {
		printf("Create Socket Failed!");
		exit(1);
	}
	{
		int opt = 1;
		setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	}
	if (bind(server_socket, (struct sockaddr*) &server_addr,
			sizeof(server_addr))) {
		printf("Server Bind Port : %d Failed!", HELLO_WORLD_SERVER_PORT);
		exit(1);
	}

	//server_socket用于监听
	if (listen(server_socket, LENGTH_OF_LISTEN_QUEUE)) {
		printf("Server Listen Failed!");
		exit(1);
	}
	while (!vars->exit) //服务器端要一直运行
	{
		//定义客户端的socket地址结构client_addr
		struct sockaddr_in client_addr;
		socklen_t length = sizeof(client_addr);

		//接受一个到server_socket代表的socket的一个连接
		//如果没有连接请求,就等待到有连接请求--这是accept函数的特性
		//accept函数返回一个新的socket,这个socket(new_server_socket)用于同连接到的客户的通信
		//new_server_socket代表了服务器和客户端之间的一个通信通道
		//accept函数把连接到的客户端信息填写到客户端的socket地址结构client_addr中
		int new_server_socket = accept(server_socket,
				(struct sockaddr*) &client_addr, &length);
		if (new_server_socket < 0) {
			printf("Server Accept Failed!\n");
			break;
		}

		char buffer[BUFFER_SIZE];
		bzero(buffer, BUFFER_SIZE);
		length = recv(new_server_socket, buffer, BUFFER_SIZE, 0);
		if (length < 0) {
			printf("Server Recieve Data Failed!\n");
			break;
		} else {
			execcommand(vars, buffer);
			send(new_server_socket, resultbuf, sizeof(struct result), 0);
			close(server_socket);
			vars->exit = 1;
		}
	}
	//关闭监听用的socket

	return;
}
