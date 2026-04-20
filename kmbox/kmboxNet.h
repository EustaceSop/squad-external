#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <stdio.h>
#include <cstdlib>
#include <ctime>
#pragma warning(disable : 4996)
#pragma comment(lib, "ws2_32.lib")

// Commands
#define cmd_connect         0xaf3c2828
#define cmd_mouse_move      0xaede7345
#define cmd_mouse_left      0x9823AE8D
#define cmd_mouse_middle    0x97a3AE8D
#define cmd_mouse_right     0x238d8212
#define cmd_mouse_wheel     0xffeead38
#define cmd_mouse_automove  0xaede7346
#define cmd_keyboard_all    0x123c2c2f
#define cmd_reboot          0xaa8855aa
#define cmd_bazerMove       0xa238455a

typedef struct {
    unsigned int mac;
    unsigned int rand;
    unsigned int indexpts;
    unsigned int cmd;
} cmd_head_t;

typedef struct {
    unsigned char buff[1024];
} cmd_data_t;

typedef struct {
    unsigned short buff[512];
} cmd_u16_t;

typedef struct {
    int button;
    int x;
    int y;
    int wheel;
    int point[10];
} soft_mouse_t;

typedef struct {
    char ctrl;
    char resvel;
    char button[10];
} soft_keyboard_t;

typedef struct {
    cmd_head_t head;
    union {
        cmd_data_t      u8buff;
        cmd_u16_t       u16buff;
        soft_mouse_t    cmd_mouse;
        soft_keyboard_t cmd_keyboard;
    };
} client_tx;

int kmNet_init(char* ip, char* port, char* mac);
int kmNet_enc_mouse_move(short x, short y);
int kmNet_enc_mouse_left(int isdown);
int kmNet_enc_mouse_right(int isdown);
int kmNet_enc_mouse_middle(int isdown);
int kmNet_enc_keyboard(soft_keyboard_t* kb);
void kmNet_cleanup(); // cleanup socket/mutex/WSA
