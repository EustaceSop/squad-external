#include "kmboxNet.h"
#include "my_enc.h"

static SOCKET sockClientfd = 0;
static client_tx tx;
static client_tx rx;
static SOCKADDR_IN addrSrv;
static soft_mouse_t softmouse;
static HANDLE m_hMutex_lock = NULL;
static unsigned char key[16] = { 0 };
static bool wsaInitialized = false;

static unsigned int StrToHex(char* pbSrc, int nLen)
{
    char h1, h2;
    unsigned char s1, s2;
    unsigned int pbDest[16] = { 0 };
    for (int i = 0; i < nLen; i++) {
        h1 = pbSrc[2 * i];
        h2 = pbSrc[2 * i + 1];
        s1 = toupper(h1) - 0x30;
        if (s1 > 9) s1 -= 7;
        s2 = toupper(h2) - 0x30;
        if (s2 > 9) s2 -= 7;
        pbDest[i] = s1 * 16 + s2;
    }
    return pbDest[0] << 24 | pbDest[1] << 16 | pbDest[2] << 8 | pbDest[3];
}

int kmNet_init(char* ip, char* port, char* mac)
{
    // Cleanup previous connection if any
    kmNet_cleanup();

    WSADATA wsaData;
    int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != 0) {
       // printf("[kmbox] WSAStartup failed: %d\n", err);
        return -1;
    }
    wsaInitialized = true;

    if (m_hMutex_lock == NULL)
        m_hMutex_lock = CreateMutexA(NULL, FALSE, NULL);

    memset(key, 0, 16);
    srand((unsigned)time(NULL));
    sockClientfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockClientfd == INVALID_SOCKET) {
       // printf("[kmbox] socket() failed: %d\n", WSAGetLastError());
        return -2;
    }

    memset(&addrSrv, 0, sizeof(addrSrv));
    addrSrv.sin_addr.S_un.S_addr = inet_addr(ip);
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(atoi(port));

    //printf("[kmbox] Connecting to %s:%s mac=%s\n", ip, port, mac);

    tx.head.mac = StrToHex(mac, 4);
    tx.head.rand = rand();
    tx.head.indexpts = 0;
    tx.head.cmd = cmd_connect;
    memset(&softmouse, 0, sizeof(softmouse));

    key[0] = tx.head.mac >> 24;
    key[1] = tx.head.mac >> 16;
    key[2] = tx.head.mac >> 8;
    key[3] = tx.head.mac >> 0;

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sockClientfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    err = sendto(sockClientfd, (const char*)&tx, sizeof(cmd_head_t), 0, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    if (err <= 0) {
       // printf("[kmbox] sendto failed: %d (WSA: %d)\n", err, WSAGetLastError());
        return -3;
    }
  //  printf("[kmbox] Sent %d bytes, waiting for response...\n", err);

    Sleep(20);
    int clen = sizeof(addrSrv);
    err = recvfrom(sockClientfd, (char*)&rx, 1024, 0, (struct sockaddr*)&addrSrv, &clen);
    if (err <= 0) {
      //  printf("[kmbox] recvfrom failed: %d (WSA: %d)\n", err, WSAGetLastError());
        return -4;
    }
  //  printf("[kmbox] Connected! Received %d bytes\n", err);
    return 0;
}

int kmNet_enc_mouse_move(short x, short y)
{
    client_tx tx_enc = { 0 };
    if (sockClientfd <= 0) return -1;
    WaitForSingleObject(m_hMutex_lock, 2000);
    tx.head.indexpts++;
    tx.head.cmd = cmd_mouse_move;
    tx.head.rand = rand();
    softmouse.x = x;
    softmouse.y = y;
    memcpy(&tx.cmd_mouse, &softmouse, sizeof(soft_mouse_t));
    int length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    memcpy(&tx_enc, &tx, length);
    my_encrypt((unsigned char*)&tx_enc, key);
    sendto(sockClientfd, (const char*)&tx_enc, 128, 0, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    softmouse.x = 0;
    softmouse.y = 0;
    SOCKADDR_IN sclient;
    int clen = sizeof(sclient);
    recvfrom(sockClientfd, (char*)&rx, 1024, 0, (struct sockaddr*)&sclient, &clen);
    ReleaseMutex(m_hMutex_lock);
    return 0;
}

int kmNet_enc_mouse_left(int isdown)
{
    client_tx tx_enc = { 0 };
    if (sockClientfd <= 0) return -1;
    WaitForSingleObject(m_hMutex_lock, 2000);
    tx.head.indexpts++;
    tx.head.cmd = cmd_mouse_left;
    tx.head.rand = rand();
    softmouse.button = (isdown ? (softmouse.button | 0x01) : (softmouse.button & (~0x01)));
    memcpy(&tx.cmd_mouse, &softmouse, sizeof(soft_mouse_t));
    int length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    memcpy(&tx_enc, &tx, length);
    my_encrypt((unsigned char*)&tx_enc, key);
    sendto(sockClientfd, (const char*)&tx_enc, 128, 0, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    SOCKADDR_IN sclient;
    int clen = sizeof(sclient);
    recvfrom(sockClientfd, (char*)&rx, 1024, 0, (struct sockaddr*)&sclient, &clen);
    ReleaseMutex(m_hMutex_lock);
    return 0;
}

int kmNet_enc_mouse_right(int isdown)
{
    client_tx tx_enc = { 0 };
    if (sockClientfd <= 0) return -1;
    WaitForSingleObject(m_hMutex_lock, 2000);
    tx.head.indexpts++;
    tx.head.cmd = cmd_mouse_right;
    tx.head.rand = rand();
    softmouse.button = (isdown ? (softmouse.button | 0x02) : (softmouse.button & (~0x02)));
    memcpy(&tx.cmd_mouse, &softmouse, sizeof(soft_mouse_t));
    int length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    memcpy(&tx_enc, &tx, length);
    my_encrypt((unsigned char*)&tx_enc, key);
    sendto(sockClientfd, (const char*)&tx_enc, 128, 0, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    SOCKADDR_IN sclient;
    int clen = sizeof(sclient);
    recvfrom(sockClientfd, (char*)&rx, 1024, 0, (struct sockaddr*)&sclient, &clen);
    ReleaseMutex(m_hMutex_lock);
    return 0;
}

int kmNet_enc_mouse_middle(int isdown)
{
    client_tx tx_enc = { 0 };
    if (sockClientfd <= 0) return -1;
    WaitForSingleObject(m_hMutex_lock, 2000);
    tx.head.indexpts++;
    tx.head.cmd = cmd_mouse_middle;
    tx.head.rand = rand();
    softmouse.button = (isdown ? (softmouse.button | 0x04) : (softmouse.button & (~0x04)));
    memcpy(&tx.cmd_mouse, &softmouse, sizeof(soft_mouse_t));
    int length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    memcpy(&tx_enc, &tx, length);
    my_encrypt((unsigned char*)&tx_enc, key);
    sendto(sockClientfd, (const char*)&tx_enc, 128, 0, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    SOCKADDR_IN sclient;
    int clen = sizeof(sclient);
    recvfrom(sockClientfd, (char*)&rx, 1024, 0, (struct sockaddr*)&sclient, &clen);
    ReleaseMutex(m_hMutex_lock);
    return 0;
}

int kmNet_enc_keyboard(soft_keyboard_t* kb)
{
    client_tx tx_enc = { 0 };
    if (sockClientfd <= 0) return -1;
    WaitForSingleObject(m_hMutex_lock, 2000);
    tx.head.indexpts++;
    tx.head.cmd = cmd_keyboard_all;
    tx.head.rand = rand();
    memcpy(&tx.cmd_keyboard, kb, sizeof(soft_keyboard_t));
    int length = sizeof(cmd_head_t) + sizeof(soft_keyboard_t);
    memcpy(&tx_enc, &tx, length);
    my_encrypt((unsigned char*)&tx_enc, key);
    sendto(sockClientfd, (const char*)&tx_enc, 128, 0, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    SOCKADDR_IN sclient;
    int clen = sizeof(sclient);
    recvfrom(sockClientfd, (char*)&rx, 1024, 0, (struct sockaddr*)&sclient, &clen);
    ReleaseMutex(m_hMutex_lock);
    return 0;
}

void kmNet_cleanup()
{
    if (sockClientfd > 0) {
        closesocket(sockClientfd);
        sockClientfd = 0;
    }
    if (m_hMutex_lock) {
        CloseHandle(m_hMutex_lock);
        m_hMutex_lock = NULL;
    }
    if (wsaInitialized) {
        WSACleanup();
        wsaInitialized = false;
    }
}
