#include<windows.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<stdlib.h>

#define input 0

#define SERVPORT  3000//�����˿ں�
#define  BACKLOG   10//�������Ӷ��е�����Ŷ���
int main()
{
	SOCKET sockfd,client_fd;//����socket�����֣���ͻ������ӵ�soket������ fd--�ļ�������
	struct sockaddr_in  local_addr;//������ַ
	struct sockaddr_in  remote_addr;//�ͻ��˵�ַ
	WSADATA wsaData;//�洢socket����
	if (WSAStartup(MAKEWORD(2, 2), &wsaData))//socket���
	{
		printf("��socket��ʧ��\n");
		getchar();
		exit(1);
	}
	else printf("��socket��ɹ�\n");
	if ((sockfd=socket(PF_INET,SOCK_STREAM,0))==INVALID_SOCKET)//SOCK_STREAM TCP,��ʽ�׽���  �����׽���
	{
		printf("socket����ʧ��\n");
		getchar();
		exit(1);
	}
	else printf("socket�����ɹ�\n");
	local_addr.sin_family = AF_INET;//ipv4
	local_addr.sin_port = htons(SERVPORT);//�˿ں� ��Ϊ0���������
	local_addr.sin_addr.s_addr = INADDR_ANY;//������Ĭ��ip��ַ  htonl()�ߵ��ֽ���ת��
	memset(&(local_addr.sin_zero), 0, 8);
	if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(struct sockaddr ))== SOCKET_ERROR)//��ip�Ͷ˿ںŵ�soket
	{
		printf("bind��ʧ��\n");
		getchar();
		exit(1);
	}
	else printf("bind�󶨳ɹ�\n");
	if (listen(sockfd, BACKLOG) ==  SOCKET_ERROR)//תΪ�����ļ���״̬
	{
		printf("listenִ�д���\n");
		getchar();
		exit(1);
	}
	else printf("listenִ�гɹ�\n");
	while (1)
	{
		int sin_size = sizeof(struct sockaddr_in);
		if ((client_fd=accept(sockfd, (struct sockaddr*)&remote_addr, &sin_size)) == INVALID_SOCKET)
		{
			printf("accept��������ʧ��\n");
			continue;
		}
        else printf("accept�������ӳɹ�\n");

        #if input
        char msg[50];
        printf("�����룺\n");
        scanf("%[^/n]",msg);
        #else
        char* msg="hello world";
        #endif // input
        if (send(client_fd, msg, strlen(msg), 0)==SOCKET_ERROR)
        {
            printf("����ʧ��\n");
            closesocket(client_fd);
            getchar();
            exit(0);
        }
        else printf("���ͳɹ�\n");


        getchar();
        closesocket(client_fd);
    }
}
