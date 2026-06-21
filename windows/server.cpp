//服务端（服务器）：提供响应请求的机器

#include <stdio.h>
#include <Windows.h>//windows编程头文件
#include <atlimage.h>
#include <ShellScalingApi.h>

#pragma comment(lib,"ws2_32.lib")

#pragma pack(push,1)
struct PacketHeader
{
	int magic;//4字节包头
	int cmd;//4字节命令号
	int body_len;//数据长度
};

#pragma pack(pop)
struct Packet
{
	PacketHeader header;//包头
	char body[];//包数据，不固定
};

enum CMD//枚举就是方便定义整数常量的
{
	CMD_SCREEN = 1,
	CMD_MOUSE = 2,
	CMD_KEYBOARD = 4,
	CMD_TESTCONNECT = 2026
};

//鼠标信息
enum ENUM_MOUSE
{
	MOUSE_MOVE = 1,//鼠标移动
	MOUSE_LDOWN = 2,//鼠标左键按下
	MOUSE_LUP = 3,//鼠标左键抬起
	MOUSE_RDOWN = 4,//鼠标右键按下
	MOUSE_RUP = 5,//鼠标右键抬起
	MOUSE_MDOWN =6,//鼠标中间按下
	MOUSE_MUP = 7,//鼠标中间抬起
	MOUSE_LCLICK = 8, //鼠标左键双击
	MOUSE_RCLICK = 9,//鼠标右键双击
	MOUSE_MCLICK = 10,//鼠标中键双击
	MOUSE_LDCLICK = 11,//鼠标左键双击
	MOUSE_RDCLICK = 12,//鼠标右键双击
	MOUSE_MDCLICK = 13//鼠标中间双击
};
struct MOUSE
{
	int action;//鼠标行为
	POINT	ptXY;//鼠标的坐标x，y
};
struct KEYBOARD
{
	int vitrual_code;//虚拟码
	int key_status;//按下0/放开1
};

int Initserver();
int GetPacketLen(Packet* pck)
{
	if (pck != NULL) {
		return pck->header.body_len + sizeof(PacketHeader);
	}
}
Packet* ParsePacket(char* buffer, int len) {
	Packet pck;
	Packet* ppck;
	//4字节包头
	int index = 0;
	for (;index < len;index++) {
		//找包头
		if (*(int*)(buffer + index) == 0x55AA77CC) {
			pck.header.magic = *(int*)(buffer + index);
			index += 4;
			break;
		}
	}
	pck.header.cmd = *(int*)(buffer + index);index += 4;
	pck.header.body_len = *(int*)(buffer + index);index += 4;
	if (pck.header.body_len == 0) {
		ppck = (Packet*)malloc(sizeof(PacketHeader));
		memcpy(&ppck->header, &pck.header, sizeof(PacketHeader));
		return ppck;
	}
	//获取数据
	if (pck.header.body_len > 0) {
		//创建接受缓冲区
		ppck = (Packet*)malloc(sizeof(PacketHeader) + pck.header.body_len);
		//拷贝数据
		memcpy(ppck->body, buffer + index, pck.header.body_len);
		//拷贝包头
		memcpy(&ppck->header, &pck.header, sizeof(PacketHeader));
		return ppck;
	}
	return 0;
}
Packet* PackPacket(int cmd, char* buffer, int buffer_len);
int HandleScreen(Packet* packet);
int HandleMouse(Packet* packet);
int HandleKeyBoard(Packet* packet);
int HandleTestConnect(Packet* packet);
int HandleCommand(Packet* packet);
SOCKET g_server_socket;
SOCKADDR_IN g_server_addr;
SOCKET g_client_socket;
unsigned long g_screen_thread_id = 0;
unsigned long g_mouse_thread_id = 0;
unsigned long g_keyboard_thread_id = 0;
#define WM_HANDLE_SCREEN (WM_USER + 1)
#define WM_HANDLE_MOUSE (WM_USER + 2)
#define WM_HANDLE_KEYBOARD (WM_USER + 3)
#define WM_HANDLE_INVOKE_MSG_LOOP  (WM_USER + 4)
DWORD WINAPI HandleScreenThreadFuc(LPVOID lpThreadParameter)
{
	MSG msg;
	while (GetMessage(&msg, 0, 0,0) ){
		if (msg.message == WM_HANDLE_SCREEN)
		{
			Packet* packet = (Packet*)msg.lParam;
			HandleScreen(packet);
			free(packet);
		}
	}
	return 0;
}
DWORD WINAPI HandleMouseThreadFuc(LPVOID lpThreadParameter)
{
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		if (msg.message == WM_HANDLE_MOUSE)
		{
			Packet* packet = (Packet*)msg.lParam;
			HandleMouse(packet);
			free(packet);
		}
	}
	return 0;
}
DWORD WINAPI HandleKeyBoardThreadFuc(LPVOID lpThreadParameter)
{
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		if (msg.message == WM_HANDLE_KEYBOARD)
		{
			Packet* packet = (Packet*)msg.lParam;
			HandleKeyBoard(packet);
			free(packet);
		}
	}
	return 0;
}


//程序入口函数
int main() {
	if (Initserver() != 0) {
		printf("启动服务失败\r\n");
		return 0;
	}
	CreateThread(NULL,0,HandleScreenThreadFuc,NULL,0,&g_screen_thread_id);
	CreateThread(NULL, 0, HandleMouseThreadFuc, NULL, 0, &g_mouse_thread_id);
	CreateThread(NULL, 0, HandleKeyBoardThreadFuc, NULL, 0, &g_keyboard_thread_id);
	PostThreadMessage(g_screen_thread_id, WM_HANDLE_SCREEN, NULL, NULL);
	PostThreadMessage(g_mouse_thread_id, WM_HANDLE_MOUSE, NULL, NULL);
	PostThreadMessage(g_keyboard_thread_id, WM_HANDLE_KEYBOARD, NULL, NULL);
	Sleep(100);
	//5等待客户端连接，会返回客户端的socket
	SOCKADDR_IN client_addr;
	int client_addr_len = sizeof(SOCKADDR_IN);
	printf("等待客户端发送数据\r\n");
	 g_client_socket = accept(g_server_socket, (sockaddr*)&client_addr, &client_addr_len);
	printf("客户端连接成功\r\n");
	//6等待客户端发送数据
#define RECV_BUFFER_SIZE 1024 * 1024 * 1
	char* buffer = (char*)malloc(RECV_BUFFER_SIZE);
	static int index = 0;
	while (true) {
		//永久的接收数据
		int len = recv(g_client_socket, buffer + index, RECV_BUFFER_SIZE - index, 0);//返回接收到的数据
		if (len > 0) {
			index += len;
			printf("接收数据成功：%d\r\n", len);
		}
		else
		{
			printf("接收数据失败\r\n");
		}
		if (index > 0) {
			//按照协议解析数据
			printf("开始解析数据\r\n");
			Packet* packet = ParsePacket(buffer, index);
			while (packet != NULL && index > 0) {
					if (packet == NULL) {
						printf("解析数据失败\r\n");
				}
						printf("解析数据成功");
					if (packet != NULL) {
						index -= GetPacketLen(packet);
						memmove(buffer, buffer + GetPacketLen(packet), index);
						HandleCommand(packet);
						//free(packet);
			    }
			}
		}
	
	}
	closesocket(g_client_socket);
	closesocket(g_server_socket);
	//清除掉
	WSACleanup();
	HandleScreen(NULL);
	return 0;
}

Packet* PackPacket(int cmd, char* buffer, int buffer_len)
{
	Packet* pck = (Packet*)malloc(buffer_len + sizeof(PacketHeader));
	pck->header.magic = 0x55AA77CC;
	pck->header.cmd = cmd;
	pck->header.body_len = buffer_len;
	if (buffer_len > 0 && buffer != NULL)
	{
		memcpy(pck->body, buffer, buffer_len);
	}
	return pck;
}

int HandleCommand(Packet* packet) {
	printf("Handle cmd:\r\n", packet->header.cmd);
	int ret = 0;
	switch(packet->header.cmd)
	{
	case CMD_SCREEN://发送屏幕
		//向线程投递消息是异步的，瞬间执行不阻塞
		PostThreadMessage(g_screen_thread_id, WM_HANDLE_SCREEN, NULL,(LPARAM)packet);
		//ret = HandleScreen(packet);
		break;
	case CMD_MOUSE://鼠标事件
		PostThreadMessage(g_mouse_thread_id, WM_HANDLE_MOUSE, NULL, (LPARAM)packet);
		//ret = HandleMouse(packet);
		break;
	case CMD_KEYBOARD://键盘命令
		PostThreadMessage(g_keyboard_thread_id, WM_HANDLE_KEYBOARD, NULL, (LPARAM)packet);
		//ret = HandleKeyBoard(packet);
		break;
	case CMD_TESTCONNECT: //测试命令
		ret = HandleTestConnect(packet);
		break;
	default:
		break;
	}
	return ret;
}

int HandleScreen(Packet* packet) {
	//获取本地屏幕
	CImage image;//创建一个image对象
	//2拿到屏幕上下文（dc：device context）
	HDC hscreen = GetDC(NULL);
	//3拿到屏幕像素位宽
	int bitwidth = GetDeviceCaps(hscreen, BITSPIXEL);
	printf("bitwidth:%d\r\n", bitwidth);
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	//4 拿到屏幕的宽高(以像素为单位)
	int sWidth = GetSystemMetrics(SM_CXSCREEN);
	int sHieght = GetSystemMetrics(SM_CYSCREEN);
	printf("width:%d height:%d\r\n", sWidth, sHieght);
	image.Create(sWidth, sHieght, bitwidth);
	//5 把屏幕数据复制到image里面
	BitBlt(image.GetDC(), 0, 0, sWidth, sHieght, hscreen, 0, 0, SRCCOPY);	
	//释放屏幕的上下文
	ReleaseDC(NULL, hscreen);
	//转为网络流
	//image.Save("test.png", Gdiplus::ImageFormatPNG);
	HGLOBAL hMen = GlobalAlloc(GMEM_MOVEABLE,0);//从堆上申请一块可变化的空间
	if (hMen == NULL) {
		//分配空间失败
		return -1;
	}
	//创建一个内存流
	IStream* pStream = NULL;
	HRESULT ret = CreateStreamOnHGlobal(hMen, true, &pStream);
	if (ret == S_OK)
	{
		//将文件保持到内存流里面
		image.Save(pStream, ::Gdiplus::ImageFormatPNG);
		//将流指针放到开头 pSTREAM = schar
		LARGE_INTEGER lg = { 0 };
		pStream->Seek(lg,STREAM_SEEK_SET,NULL);
		//获取流指针
		char* pdata =  (char*)GlobalLock(hMen);
		//获取流的长度
		int len = GlobalSize(hMen);
		Packet* packet = PackPacket(CMD_SCREEN, pdata, len);
		int send_len = send(g_client_socket, (char*) & packet->header.magic, sizeof(PacketHeader) + len, 0);
		if (send_len > 0) {
			printf("发送成功:%d\r\n", send_len);
		}
		else if(send_len<0){
			printf("发送失败 :%d\r\n", send_len);
		}
		free(packet);
		GlobalUnlock(hMen);
	}
	//释放流指针
	pStream->Release();
	//释放全局指针
	GlobalFree(hMen);
	//释放图像DC
	image.ReleaseDC();
	//通过网络发送
	return 0;
}

int HandleMouse(Packet* packet) {
	//从这个结构体里提取出鼠标信息
	MOUSE mouse;
	memcpy(&mouse.action, packet->body, packet->header.body_len);
	printf("x = %d y = %d\r\n", mouse.ptXY.x, mouse.ptXY.y);
	//在模拟执行鼠标事件
	//设置鼠标位置
	SetCursorPos(mouse.ptXY.x, mouse.ptXY.y);
	switch (mouse.action)
	{
	case MOUSE_MOVE:
		SetCursorPos(mouse.ptXY.x, mouse.ptXY.y);
		break;
	case MOUSE_LDOWN:
		mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,GetMessageExtraInfo());
		break;
	case MOUSE_LUP:
		mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_RDOWN:
		mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_RUP:
		mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MDOWN:
		mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MUP:
		mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_LCLICK:
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_RCLICK:
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MCLICK:
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_LDCLICK:
		mouse_event(MOUSEEVENTF_LEFTDOWN| MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_RDCLICK:
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MDCLICK:
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	default:
		printf("未知鼠标操作%d\r\n", mouse.action);
		break;
	}
	return 0;
}

int HandleKeyBoard(Packet* packet) {
	KEYBOARD key_board;
	memcpy(&key_board.vitrual_code, packet->body, packet->header.body_len);
	INPUT input = {0};
	input.ki.wVk = key_board.vitrual_code;
	input.ki.wScan = 0;
	input.ki.time = 0;
	input.ki.dwFlags = key_board.key_status;//按钮状态0按下，1松开
	input.ki.dwExtraInfo = 0;
	input.type = INPUT_KEYBOARD;
	int ret = SendInput(1, &input,sizeof(INPUT));
	if (ret > 0) {
		printf("成功执行键盘事件:%d\r\n", key_board.vitrual_code);
	}
	return 0;
}

int HandleTestConnect(Packet* packet) {
	return 0;
}

int Initserver()
{
		//服务器网络编程步骤：
	//1初始化网络环境
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
	//2创建服务器socket AF_INET:使用ipv4协议，SOCK_STREAM：使用tcp协议 SOCK_DGRAM:UDP协议
	g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (g_server_socket == INVALID_SOCKET) {
		printf("创建服务器失败\r\n");
	}
		//3给服务器绑定地址
		// 准备一个地址;
	g_server_addr.sin_family = AF_INET;//ipv4的协议
	g_server_addr.sin_port = htons(9999);//0-65525(SHORT)监听的端口
	g_server_addr.sin_addr.S_un.S_addr = inet_addr("[REMOVED_PRIVATE_IP]");//监听所有服务器上的ip地址
	if ((bind(g_server_socket, (sockaddr*)&g_server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) ){
		printf("绑定服务器socket失败\r\n");
		return 0;
	}
		//4开启服务器监听
	if (listen(g_server_socket, 1) == SOCKET_ERROR) {
		printf("监听开启失败\r\n");
		return 0;
		}
	return 0;
}