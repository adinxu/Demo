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

#define SERVPORT  3000//�����˿ں�
#define MAXDATASIZE   50
int main()
{
    SOCKET sockfd;
	char buf[MAXDATASIZE];

	struct sockaddr_in server_addr;//������ַ

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		printf("��socket��ʧ��\n");
		getchar();
		exit(1);
	}
	else printf("��socket��ɹ�\n");
	if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
	{
		printf("socket����ʧ��\n");
		getchar();
		exit(1);
	}
	else printf("socket�����ɹ�\n");
	server_addr.sin_family = AF_INET;      //��ʼ�����ֲ���
	server_addr.sin_port = htons(SERVPORT);//�����˿�
	if ((server_addr.sin_addr.s_addr = inet_addr(SERVIP))== INADDR_NONE)//����ip
	{
		printf("����ip�Ƿ�\n");
		getchar();
		exit(1);
	}
	else printf("����ip�Ϸ�\n");
	memset(&(server_addr.sin_zero), 0, 8);
	if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == SOCKET_ERROR)
	{
		printf("connect����ʧ��\n");
		getchar();
		exit(1);
	}
	printf("connect���ӳɹ�\n");

    int recvbytes;
	if ((recvbytes = recv(sockfd, buf, MAXDATASIZE, 0)) ==SOCKET_ERROR)
	{
		printf("����ʧ��\n");
        closesocket(sockfd);
        getchar();
        exit(1);
	}
	buf[recvbytes] = '\0';
	printf("%s", buf);

    getchar();
	closesocket(sockfd);
}
