#include <stdio.h>
#include <Windows.h>
#include <atlimage.h>

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

struct KEYBOARD
{
	int vitrual_code;//虚拟码
	int key_status;//按下0/放开1
};

enum CMD//枚举就是方便定义整数常量的
{
	CMD_SCREEN = 1,
	CMD_MOUSE = 2,
	CMD_KEYBOARD = 4,
	CMD_TESTCONNECT = 2026
};

enum ENUM_MOUSE
{
	MOUSE_MOVE = 1,//鼠标移动
	MOUSE_LDOWN = 2,//鼠标左键按下
	MOUSE_LUP = 3,//鼠标左键抬起
	MOUSE_RDOWN = 4,//鼠标右键按下
	MOUSE_RUP = 5,//鼠标右键抬起
	MOUSE_MDOWN = 6,//鼠标中间按下
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

int GetPacketLen(Packet* pck)
{
	if (pck != NULL) {
		return pck->header.body_len + sizeof(PacketHeader);
	}
}
Packet* PackPacket(int cmd, char* buffer, int buffer_len);
Packet* ParsePacket(char* buffer, int len);
int InitWindow(HINSTANCE hInstance, int nCmdShow);
int InitSocket();
DWORD WINAPI SendScreenCallBack(LPVOID lpThreadParameter);
SOCKET g_server_socket;
SOCKADDR_IN g_server_addr;
HWND g_hwnd = NULL;
CImage g_image;
#define RECV_BUFFER_LEN 1024 * 1024 * 10

CRITICAL_SECTION g_cri_sec;
int g_remote_width = -1;
int g_remote_height = -1;

LRESULT CALLBACK winProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg)
	{
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			//拿到image
			if (!g_image.IsNull()) {
				RECT client_rect;
				GetClientRect(hwnd, &client_rect);
				int client_width = client_rect.right - client_rect.left;
				int client_height = client_rect.bottom - client_rect.top;
				
				int oldMode = SetStretchBltMode(hdc,HALFTONE);//设置拉伸模式
				SetBrushOrgEx(hdc, 0, 0,NULL);//设置画刷远点,x选择HALFTONE模式必须设置					
				EnterCriticalSection(&g_cri_sec);
				int remote_width = g_image.GetWidth();
				int remote_height = g_image.GetHeight();
				g_image.StretchBlt(hdc, 0, 0, client_width, client_height, 0, 0, remote_width, remote_height, SRCCOPY);
				LeaveCriticalSection(&g_cri_sec);
				SetStretchBltMode(hdc, oldMode);
			}
			EndPaint(hwnd, &ps);
		}
		break;
		case WM_MOUSEMOVE://鼠标移动
		{
			//拿到当前鼠标客户区的位置
			int xPos = LOWORD(lParam);//低两字节是x坐标
			int yPos = HIWORD(lParam);//高两字节是y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_remote_height != -1 && g_remote_width != -1) {
				int rxPos = xPos * g_remote_width / client_width;
				int ryPos = yPos * g_remote_height / client_height;
				MOUSE mouse;
				mouse.action = MOUSE_MOVE;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(MOUSE));
				//send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
				//free(packet);
			}
		}
			break;
		case WM_LBUTTONDOWN://鼠标左键按下
		{
			//拿到当前鼠标客户区的位置
			int xPos = LOWORD(lParam);//低两字节是x坐标
			int yPos = HIWORD(lParam);//高两字节是y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_remote_height != -1 && g_remote_width != -1) {
				int rxPos = xPos * g_remote_width / client_width;
				int ryPos = yPos * g_remote_height / client_height;
				MOUSE mouse;
				mouse.action = MOUSE_LDOWN;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(MOUSE));
				send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
				free(packet);
			}
		}
			break;
		case WM_LBUTTONUP://鼠标左键抬起
		{
			//拿到当前鼠标客户区的位置
			int xPos = LOWORD(lParam);//低两字节是x坐标
			int yPos = HIWORD(lParam);//高两字节是y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_remote_height != -1 && g_remote_width != -1) {
				int rxPos = xPos * g_remote_width / client_width;
				int ryPos = yPos * g_remote_height / client_height;
				MOUSE mouse;
				mouse.action = MOUSE_LUP;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(MOUSE));
				send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
				free(packet);
			}
		}
			break;
		case WM_LBUTTONDBLCLK://鼠标右键按下
		{
			//拿到当前鼠标客户区的位置
			int xPos = LOWORD(lParam);//低两字节是x坐标
			int yPos = HIWORD(lParam);//高两字节是y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_remote_height != -1 && g_remote_width != -1) {
				int rxPos = xPos * g_remote_width / client_width;
				int ryPos = yPos * g_remote_height / client_height;
				MOUSE mouse;
				mouse.action = MOUSE_LDCLICK;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(MOUSE));
				send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		break;
		case WM_RBUTTONDOWN://鼠标右键按下
		{
			//拿到当前鼠标客户区的位置
			int xPos = LOWORD(lParam);//低两字节是x坐标
			int yPos = HIWORD(lParam);//高两字节是y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_remote_height != -1 && g_remote_width != -1) {
				int rxPos = xPos * g_remote_width / client_width;
				int ryPos = yPos * g_remote_height / client_height;
				MOUSE mouse;
				mouse.action = MOUSE_RDOWN;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(MOUSE));
				send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
				free(packet);
			}
		}
			break;
		case WM_RBUTTONUP://鼠标右键抬起
		{
			//拿到当前鼠标客户区的位置
			int xPos = LOWORD(lParam);//低两字节是x坐标
			int yPos = HIWORD(lParam);//高两字节是y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_remote_height != -1 && g_remote_width != -1) {
				int rxPos = xPos * g_remote_width / client_width;
				int ryPos = yPos * g_remote_height / client_height;
				MOUSE mouse;
				mouse.action = MOUSE_RUP;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(MOUSE));
				send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
				free(packet);
			}
		}
			break;
		case WM_KEYDOWN:
		{
			KEYBOARD key_board;
			key_board.vitrual_code = (int)wParam;
			key_board.key_status = 0; // 按下

			Packet* packet = PackPacket(CMD_KEYBOARD, (char*)&key_board, sizeof(KEYBOARD));
			send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
			free(packet);
		}
		break;

		case WM_KEYUP:
		{
			KEYBOARD key_board;
			key_board.vitrual_code = (int)wParam;
			key_board.key_status = 1; // 抬起

			Packet* packet = PackPacket(CMD_KEYBOARD, (char*)&key_board, sizeof(KEYBOARD));
			send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
			free(packet);
		}
		break;
		case WM_SYSKEYDOWN:
		{
			KEYBOARD key_board;
			key_board.vitrual_code = wParam;
			key_board.key_status = 0;
			Packet* packet = PackPacket(CMD_KEYBOARD, (char*)&key_board.vitrual_code, sizeof(KEYBOARD));
			send(g_server_socket, (char*)&packet->header.magic, GetPacketLen(packet), 0);
			free(packet);
		}
			break;
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
		break;
	}
	return 0;
}
//1.创建窗口的入口函数
int WINAPI WinMain(
	HINSTANCE hIstance,//当前实例句柄
	HINSTANCE hPreventInstance,//前一个实例句柄（一般为null）
	PSTR pCmdLine,//命令行参数
	int nCmdShow//窗口显示方式
) {
	//初始化关键代码段
	InitializeCriticalSection(&g_cri_sec);
	InitWindow(hIstance, nCmdShow);
	InitSocket();
	//连接服务器
	if (connect(g_server_socket, (sockaddr*)&g_server_addr, sizeof(g_server_addr)) == SOCKET_ERROR) {
		printf("连接服务失败\r\n");
		return 0;
	}
	unsigned long send_screen_thread_id = 0;
	HANDLE handle_send_screen = CreateThread(NULL, 0, SendScreenCallBack, NULL, 0, &send_screen_thread_id);
	OutputDebugString("连接成功\r\n");
	//向服务器发送数据
	
	MSG msg = { 0 };
	while (GetMessage(&msg, NULL, 0, 0)) {
		//翻译消息
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}
DWORD WINAPI SendScreenCallBack(LPVOID lpThreadParameter){
	char* recv_buffer = (char*)malloc(RECV_BUFFER_LEN);
	while (true)//不停发送数据
	{
		Packet* pack = PackPacket(CMD_SCREEN, NULL, 0);
		int sen_len = send(g_server_socket, (char*)pack, GetPacketLen(pack), 0);
		if (sen_len > 0) {
			OutputDebugString("成功发送数据");
		}
		free(pack);
		int len = recv(g_server_socket, recv_buffer, RECV_BUFFER_LEN, 0);
		if (len > 0) {
			Packet* pack = ParsePacket(recv_buffer, len);
			if (pack != NULL) {//解析成功
				//拿到图片数据
				//绘制图片数据
				HGLOBAL hMen = GlobalAlloc(GMEM_MOVEABLE, 0);
				if (hMen == NULL) {
					continue;
				}
				IStream* pStream = NULL;
				HRESULT ret = CreateStreamOnHGlobal(hMen, true, &pStream);
				//h_bitmap == NULL;
				if (ret == S_OK) {
					ULONG lenght = 0;
					pStream->Write(pack->body, pack->header.body_len, &lenght);
					free(pack);
					EnterCriticalSection(&g_cri_sec);
					LARGE_INTEGER lg = { 0 };
					pStream->Seek(lg, STREAM_SEEK_SET, NULL);
					if (!g_image.IsNull()){
						g_image.Destroy();
					}
					g_image.Load(pStream);
					if (g_remote_width == -1 && g_remote_height == -1) {
						g_remote_width = g_image.GetWidth();
						g_remote_height = g_image.GetHeight();
					}
					LeaveCriticalSection(&g_cri_sec);
					InvalidateRect(g_hwnd, NULL, FALSE);
					UpdateWindow(g_hwnd);
				}
			}
		}
	}

}
	//注册一个窗口类

int InitWindow(HINSTANCE hInstance, int nCmdShow) {
	WNDCLASS ws = {0};
	LPCSTR CLASS_NAME = "MainWindow";
	ws.lpfnWndProc = winProc;//窗口消息的处理函数
	ws.hInstance = hInstance;//实例句柄
	ws.lpszClassName = CLASS_NAME;
	ws.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	ws.hCursor = LoadCursor(NULL, IDC_ARROW);//光标
	ws.style = CS_HREDRAW | CS_VREDRAW;//窗口大小发生变化时会重绘窗口
	if (!RegisterClass(&ws)) {
		MessageBox(NULL, "窗口注册失败", "错误", MB_OK | MB_ICONERROR);
		return 0;
	}
	//创建窗口
	g_hwnd = CreateWindow(
		CLASS_NAME,//窗口类目
		"远程控制",//窗口标题
		WS_OVERLAPPEDWINDOW,//窗口样式
		CW_USEDEFAULT, CW_USEDEFAULT,//窗口x，y的坐标
		600, 400,//窗口的宽高
		NULL,//父窗口的句柄
		NULL,//菜单句柄
		hInstance,
		NULL
	);
	if (g_hwnd == NULL) {
		MessageBox(NULL, "窗口创建失败", "错误", MB_OK | MB_ICONERROR);
		return 0;
	}
	//3 显示窗口
	ShowWindow(g_hwnd, nCmdShow);
	//4更新窗口
	UpdateWindow(g_hwnd);
}
	

Packet* PackPacket(int cmd, char* buffer, int buffer_len)
{
	Packet* pck = (Packet*)malloc(buffer_len + sizeof(PacketHeader));
	pck->header.magic = 0x55AA77CC;
	pck->header.cmd = cmd;
	pck->header.body_len = buffer_len;
	if(buffer_len >0 && buffer != NULL)
	{
		memcpy(pck->body, buffer, buffer_len);
	}
	return pck;
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
int InitSocket() {
	// 初始化socket环境
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	// 创建socket
	g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (g_server_socket == INVALID_SOCKET) {
		printf("创建socket失败\r\n");
		return 0;
	}

	// 设置服务器地址
	g_server_addr.sin_family = AF_INET;
	g_server_addr.sin_port = htons(9999); // 转为网络字节序
	g_server_addr.sin_addr.S_un.S_addr = inet_addr("[REMOVED_PRIVATE_IP]");
}