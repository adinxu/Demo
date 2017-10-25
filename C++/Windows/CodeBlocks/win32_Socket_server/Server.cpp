#include<windows.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<stdlib.h>

#define input 0

#define SERVPORT  3000//主机端口号
#define  BACKLOG   10//请求连接队列的最大排队数
int main()
{
	SOCKET sockfd,client_fd;//监听socket描述字，与客户端连接的soket描述字 fd--文件描述符
	struct sockaddr_in  local_addr;//主机地址
	struct sockaddr_in  remote_addr;//客户端地址
	WSADATA wsaData;//存储socket数据
	if (WSAStartup(MAKEWORD(2, 2), &wsaData))//socket库绑定
	{
		printf("绑定socket库失败\n");
		getchar();
		exit(1);
	}
	else printf("绑定socket库成功\n");
	if ((sockfd=socket(PF_INET,SOCK_STREAM,0))==INVALID_SOCKET)//SOCK_STREAM TCP,流式套接字  创建套接字
	{
		printf("socket创建失败\n");
		getchar();
		exit(1);
	}
	else printf("socket创建成功\n");
	local_addr.sin_family = AF_INET;//ipv4
	local_addr.sin_port = htons(SERVPORT);//端口号 若为0则随机分配
	local_addr.sin_addr.s_addr = INADDR_ANY;//绑定主机默认ip地址  htonl()高低字节序转换
	memset(&(local_addr.sin_zero), 0, 8);
	if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(struct sockaddr ))== SOCKET_ERROR)//绑定ip和端口号到soket
	{
		printf("bind绑定失败\n");
		getchar();
		exit(1);
	}
	else printf("bind绑定成功\n");
	if (listen(sockfd, BACKLOG) ==  SOCKET_ERROR)//转为被动的监听状态
	{
		printf("listen执行错误\n");
		getchar();
		exit(1);
	}
	else printf("listen执行成功\n");
	while (1)
	{
		int sin_size = sizeof(struct sockaddr_in);
		if ((client_fd=accept(sockfd, (struct sockaddr*)&remote_addr, &sin_size)) == INVALID_SOCKET)
		{
			printf("accept建立连接失败\n");
			continue;
		}
        else printf("accept建立连接成功\n");

        #if input
        char msg[50];
        printf("请输入：\n");
        scanf("%[^/n]",msg);
        #else
        char* msg="hello world";
        #endif // input
        if (send(client_fd, msg, strlen(msg), 0)==SOCKET_ERROR)
        {
            printf("发送失败\n");
            closesocket(client_fd);
            getchar();
            exit(0);
        }
        else printf("发送成功\n");


        getchar();
        closesocket(client_fd);
    }
}
