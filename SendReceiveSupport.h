#ifndef _SEND_RECEIVE_SUPPORT_H
#define _SEND_RECEIVE_SUPPORT_H

//==============================
//---     Include Files     --- 
//==============================
//==============================
//---        Defines        --- 
//==============================
//==============================
//---         Enums         --- 
//==============================
//==============================
//---   Stucts and Unions   --- 
//==============================
//==============================
//---        Externs        --- 
//==============================
//==============================
//---       Prototypes      --- 
//==============================
int InitRequestSocket(void);
int AcceptConnection(void);
int getReceiverRequest(void);
int sendServerResponse(void);
void sendDataBuffer(void);
void FlushAllBuffers(void);
void CloseAllSockets(void);

#endif

