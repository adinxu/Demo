#include <windows.h>
#include <iostream>
#define use_hfile 0//使用文件（串口）句柄
#define use_hevent 1//使用事件   可用SetEvent(m_ov.hEvent)来查看GetOverlappedResult确实根据hevent判定

#define use_WaitForSingleObject 1
#define use_GetOverlappedResult 0
int com_open();
int com_read();
    HANDLE hCom;//串口句柄
    OVERLAPPED m_ov;//异步读写所需结构体
    char* buf=new char[24];//读取数据存储
    DWORD read=0;//读取字节数
    DWORD dwErrorFlags;
    COMSTAT ComStat;
int com_open()
{
            hCom=CreateFile(
                           "COM5",
                           GENERIC_READ|GENERIC_WRITE,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,
                           NULL
                           );
    if(hCom == INVALID_HANDLE_VALUE)  return -1;
    else std::cout<<"COM5 OPEN SUCCESS\n";
    if(!SetupComm(hCom,1024,1024)) return -2;
    else std::cout<<"COM5 setup SUCCESS\n";
    COMMTIMEOUTS cto = {0,0,0,0,0};//MAXDWORD,0,0,0,0};//第一个参数为0表示无限等待下一字节，为MAXDWORD表示立即激活相应句柄
    if(!SetCommTimeouts(hCom,&cto)) return -3;
    else std::cout<<"COM5 SetCommTimeouts SUCCESS\n";
    DCB dcb;
    memset(&dcb,0,sizeof(dcb));//DCB初始化
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hCom, &dcb);
    dcb.BaudRate =9600;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.ByteSize = 8;
    /*dcb.fBinary = 1;//指定是否允许二进制模式
    dcb.fOutxCtsFlow=false;
    dcb.fDtrControl=false;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX=false;
    dcb.fInX=false;
    dcb.XoffChar = 0x13;
    dcb.XonChar = 0x11;
    dcb.XonLim = (1024 >> 2) * 3;
    dcb.XoffLim = (1024 >> 2) * 3;*/
    if(!SetCommState(hCom,&dcb)) return -4;
    else std::cout<<"COM5 setCommState SUCCESS\n";
    PurgeComm(hCom,PURGE_RXCLEAR);

    return 0;
}
int com_read()
{
    memset(&m_ov,0,sizeof(OVERLAPPED));
    memset(buf,0,24*sizeof(char));
    #if use_hevent
    m_ov.hEvent = CreateEvent(NULL,// LPSECURITY_ATTRIBUTES lpsa不可被继承
						    TRUE, // BOOL fManualReset 是否人工复位
						    false,//TRUE, // BOOL fInitialState 初始状态 有无信号
						    NULL); // LPTSTR lpszEventName 匿名
    if(m_ov.hEvent == INVALID_HANDLE_VALUE) return -1;
    else std::cout<<"ovevent creat success\n";
    #endif
    if(!ReadFile(hCom,buf,1,&read,&m_ov))
    {
        if(GetLastError()==ERROR_IO_PENDING)
        {
            std::cout<<"reading...\n";
            return 0;
        }
        else
        {
          return -2;
        }
    }
    else return -3;
    return 0;
}
int main()
{
    int i=0;
    i=com_open();
    if(!i)
    {
        i=com_read();
        if(!i)
        {
            #if use_hfile
            #if use_WaitForSingleObject
            WaitForSingleObject(hCom,INFINITE);//通过串口句柄
            #endif

            #if use_GetOverlappedResult
            GetOverlappedResult(    hCom,
                                    &m_ov,
                                    &read,
                                    true
                                );
            #endif
            #endif

            #if use_hevent
            #if use_WaitForSingleObject
            WaitForSingleObject(m_ov.hEvent,INFINITE);//通过串口句柄
            #endif
            #if use_GetOverlappedResult
            GetOverlappedResult(
                                    hCom,
                                    &m_ov,
                                    &read,
                                    true
                                );
            #endif
            #endif // use_hevent
            std::cout<<buf;
            delete[] buf;
            return 0;
        }
        else std::cout<<"start read err\n";
        delete[] buf;
        return i;
    }
    else std::cout<<"open com5 err\n";
    delete[] buf;
    return i;
}

