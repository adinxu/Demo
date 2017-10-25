#include<windows.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<stdlib.h>

#define test 0
#if test
#define SERVIP "127.0.0.1"
#else
#define SERVIP "183.152.254.173"
#endif // 0

#define SERVPORT  3000//主机端口号
#define MAXDATASIZE   50
int main()
{
    SOCKET sockfd;
	char buf[MAXDATASIZE];

	struct sockaddr_in server_addr;//主机地址

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		printf("绑定socket库失败\n");
		getchar();
		exit(1);
	}
	else printf("绑定socket库成功\n");
	if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
	{
		printf("socket创建失败\n");
		getchar();
		exit(1);
	}
	else printf("socket创建成功\n");
	server_addr.sin_family = AF_INET;      //初始化各种参数
	server_addr.sin_port = htons(SERVPORT);//主机端口
	if ((server_addr.sin_addr.s_addr = inet_addr(SERVIP))== INADDR_NONE)//主机ip
	{
		printf("主机ip非法\n");
		getchar();
		exit(1);
	}
	else printf("主机ip合法\n");
	memset(&(server_addr.sin_zero), 0, 8);
	if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == SOCKET_ERROR)
	{
		printf("connect连接失败\n");
		getchar();
		exit(1);
	}
	printf("connect连接成功\n");

    int recvbytes;
	if ((recvbytes = recv(sockfd, buf, MAXDATASIZE, 0)) ==SOCKET_ERROR)
	{
		printf("接收失败\n");
        closesocket(sockfd);
        getchar();
        exit(1);
	}
	buf[recvbytes] = '\0';
	printf("%s", buf);

    getchar();
	closesocket(sockfd);
}
