// Copyright (C) 2003-2008 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "WII_IPC_HLE_Device_usb.h"
#include "../Plugins/Plugin_Wiimote.h"

#include "../Debugger/Debugger_SymbolMap.h"
#include "../Host.h"

// ugly hacks for "SendEventNumberOfCompletedPackets"
int g_HCICount = 0;
int g_GlobalHandle = 0;

CWII_IPC_HLE_Device_usb_oh1_57e_305::CWII_IPC_HLE_Device_usb_oh1_57e_305(u32 _DeviceID, const std::string& _rDeviceName)
	: IWII_IPC_HLE_Device(_DeviceID, _rDeviceName)
	, m_pACLBuffer(NULL)
	, m_pHCIBuffer(NULL)
	, m_ScanEnable(0)
	, m_PINType(0)
	, m_EventFilterType(0)
	, m_EventFilterCondition(0)
	, m_HostMaxACLSize(0)
	, m_HostMaxSCOSize(0)
	, m_HostNumACLPackets(0)
	, m_HostNumSCOPackets(0)
{
	m_WiiMotes.push_back(CWII_IPC_HLE_WiiMote(this, 0));

	m_ControllerBD.b[0] = 0x11;
	m_ControllerBD.b[1] = 0x02;
	m_ControllerBD.b[2] = 0x19;
	m_ControllerBD.b[3] = 0x79;
	m_ControllerBD.b[4] = 0x00;
	m_ControllerBD.b[5] = 0xFF;

	m_ClassOfDevice[0] = 0x00;
	m_ClassOfDevice[1] = 0x00;
	m_ClassOfDevice[2] = 0x00;

	memset(m_LocalName, 0, HCI_UNIT_NAME_SIZE);

	Host_SetWiiMoteConnectionState(0);
}

CWII_IPC_HLE_Device_usb_oh1_57e_305::~CWII_IPC_HLE_Device_usb_oh1_57e_305()
{}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::Open(u32 _CommandAddress, u32 _Mode)
{
	Memory::Write_U32(GetDeviceID(), _CommandAddress+4);
	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::IOCtl(u32 _CommandAddress)
{
	return IOCtlV(_CommandAddress);	//hack
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::IOCtlV(u32 _CommandAddress) 
{	

/*	
	Memory::Write_U8(255, 0x80149950); // BTM LOG
									 // 3 logs L2Cap
									 // 4 logs l2_csm$

	Memory::Write_U8(255, 0x80149949);  // Security Manager

	Memory::Write_U8(255, 0x80149048);  // HID

	Memory::Write_U8(3, 0x80152058);  // low ??   // >= 4 and you will get a lot of event messages of the same type 
	
	Memory::Write_U8(1, 0x80152018);  // WUD

	Memory::Write_U8(1, 0x80151FC8);  // DEBUGPrint */



	// even it it wasn't very useful yet...
	// Memory::Write_U8(1, 0x80151488); // WPAD_LOG
	// Memory::Write_U8(1, 0x801514A8); // USB_LOG
	// Memory::Write_U8(1, 0x801514D8); // WUD_DEBUGPrint
	// Memory::Write_U8(1, 0x80148E09); // HID LOG

	SIOCtlVBuffer CommandBuffer(_CommandAddress);
	switch(CommandBuffer.Parameter)
	{
	case USB_IOCTL_HCI_COMMAND_MESSAGE:
		{
			SHCICommandMessage CtrlSetup;

			// the USB stuff is little endian.. 
			CtrlSetup.bRequestType = *(u8*)Memory::GetPointer(CommandBuffer.InBuffer[0].m_Address);
			CtrlSetup.bRequest = *(u8*)Memory::GetPointer(CommandBuffer.InBuffer[1].m_Address);
			CtrlSetup.wValue = *(u16*)Memory::GetPointer(CommandBuffer.InBuffer[2].m_Address);
			CtrlSetup.wIndex = *(u16*)Memory::GetPointer(CommandBuffer.InBuffer[3].m_Address);
			CtrlSetup.wLength = *(u16*)Memory::GetPointer(CommandBuffer.InBuffer[4].m_Address);
			CtrlSetup.m_PayLoadAddr = CommandBuffer.PayloadBuffer[0].m_Address;
			CtrlSetup.m_PayLoadSize = CommandBuffer.PayloadBuffer[0].m_Size;

			// check termination
			_dbg_assert_msg_(WII_IPC_WIIMOTE, *(u8*)Memory::GetPointer(CommandBuffer.InBuffer[5].m_Address) == 0, 
													"WIIMOTE: Termination != 0");

#if 0
			LOG(WII_IPC_WIIMOTE, "USB_IOCTL_CTRLMSG (0x%08x) - execute command", _CommandAddress);

			LOG(WII_IPC_WIIMOTE, "    bRequestType: 0x%x", CtrlSetup.bRequestType);
			LOG(WII_IPC_WIIMOTE, "    bRequest: 0x%x", CtrlSetup.bRequest);
			LOG(WII_IPC_WIIMOTE, "    wValue: 0x%x", CtrlSetup.wValue);
			LOG(WII_IPC_WIIMOTE, "    wIndex: 0x%x", CtrlSetup.wIndex);
			LOG(WII_IPC_WIIMOTE, "    wLength: 0x%x", CtrlSetup.wLength);
#endif

			ExecuteHCICommandMessage(CtrlSetup);

			// control message has been sent executed
			Memory::Write_U32(0, _CommandAddress + 0x4);

			return true;
		}
		break;

	case USB_IOCTL_BLKMSG:
		{
			u8 Command = Memory::Read_U8(CommandBuffer.InBuffer[0].m_Address);
			switch (Command)
			{
			case ACL_DATA_ENDPOINT_READ:
				{
					// write
					DumpAsync(CommandBuffer.BufferVector, _CommandAddress, CommandBuffer.NumberInBuffer, CommandBuffer.NumberPayloadBuffer);

					SIOCtlVBuffer pBulkBuffer(_CommandAddress);
					UACLHeader* pACLHeader = (UACLHeader*)Memory::GetPointer(pBulkBuffer.PayloadBuffer[0].m_Address);

					_dbg_assert_(WII_IPC_WIIMOTE, pACLHeader->BCFlag == 0);
					_dbg_assert_(WII_IPC_WIIMOTE, pACLHeader->PBFlag == 2);

					SendToDevice(pACLHeader->ConnectionHandle, Memory::GetPointer(pBulkBuffer.PayloadBuffer[0].m_Address + 4), pACLHeader->Size);
				}
				break;

			case ACL_DATA_ENDPOINT:
				{
					if (m_pACLBuffer)
						delete m_pACLBuffer;
					m_pACLBuffer = new SIOCtlVBuffer(_CommandAddress);

					LOG(WII_IPC_WIIMOTE, "ACL_DATA_ENDPOINT: 0x%08x ", _CommandAddress);
					return false;
				}
				break;

			default:
				{
					_dbg_assert_msg_(WII_IPC_WIIMOTE, 0, "Unknown USB_IOCTL_BLKMSG: %x", Command);		
				}
				break;
			}
		}
		break;


	case USB_IOCTL_INTRMSG:
		{ 
			u8 Command = Memory::Read_U8(CommandBuffer.InBuffer[0].m_Address);
			switch (Command)
			{
			case HCI_EVENT_ENDPOINT:
				{
					if (m_pHCIBuffer)
					{
						PanicAlert("Kill current hci buffer... there could be a comand inside");
						delete m_pHCIBuffer;
					}
					m_pHCIBuffer = new SIOCtlVBuffer(_CommandAddress);
					return false;							
				}
				break;

			default:
				{
					_dbg_assert_msg_(WII_IPC_WIIMOTE, 0, "Unknown USB_IOCTL_INTRMSG: %x", Command);		
				}
				break;
			}
		}
		break;

	default:
		{
			_dbg_assert_msg_(WII_IPC_WIIMOTE, 0, "Unknown CWII_IPC_HLE_Device_usb_oh1_57e_305: %x", CommandBuffer.Parameter);

			LOG(WII_IPC_WIIMOTE, "%s - IOCtlV:", GetDeviceName().c_str());
			LOG(WII_IPC_WIIMOTE, "    Parameter: 0x%x", CommandBuffer.Parameter);
			LOG(WII_IPC_WIIMOTE, "    NumberIn: 0x%08x", CommandBuffer.NumberInBuffer);
			LOG(WII_IPC_WIIMOTE, "    NumberOut: 0x%08x", CommandBuffer.NumberPayloadBuffer);
			LOG(WII_IPC_WIIMOTE, "    BufferVector: 0x%08x", CommandBuffer.BufferVector);
			LOG(WII_IPC_WIIMOTE, "    BufferSize: 0x%08x", CommandBuffer.BufferSize); 
			DumpAsync(CommandBuffer.BufferVector, _CommandAddress, CommandBuffer.NumberInBuffer, CommandBuffer.NumberPayloadBuffer);
		}
		break;
	}

	// write return value
	Memory::Write_U32(0, _CommandAddress + 0x4);

	return true;
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::SendToDevice(u16 _ConnectionHandle, u8* _pData, u32 _Size)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_ConnectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendToDevice: Cant find WiiMote by connection handle: %02x", _ConnectionHandle);
		return;
	}

	pWiiMote->SendACLFrame(_pData, _Size);
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::SendACLFrame(u16 _ConnectionHandle, u8* _pData, u32 _Size)
{
	LOG(WII_IPC_WIIMOTE, "Queing ACL frame.");

	//queue the packet
	ACLFrame frame;
	frame.ConnectionHandle = _ConnectionHandle;
	frame.data = new u8[_Size];
	memcpy(frame.data, _pData, _Size);
	frame.size = _Size;
	m_AclFrameQue.push(frame);
	
	g_HCICount++;
}

u32 CWII_IPC_HLE_Device_usb_oh1_57e_305::Update()
{
	if (!m_EventQueue.empty() && m_pHCIBuffer)
	{
		SIOCtlVBuffer* pHCIBuffer = m_pHCIBuffer;
		m_pHCIBuffer = NULL;

		// copy the event to memory
		const SQueuedEvent& rEvent = m_EventQueue.front();
		u8* pHCIEvent = Memory::GetPointer(pHCIBuffer->PayloadBuffer[0].m_Address);  
		memcpy(pHCIEvent, rEvent.m_buffer, rEvent.m_size);

		// return reply buffer size
		Memory::Write_U32((u32)rEvent.m_size, pHCIBuffer->m_Address + 0x4);

		if (rEvent.m_connectionHandle > 0)
		{
			g_HCICount++;
		}

		m_EventQueue.pop();

		u32 Addr = pHCIBuffer->m_Address;
		delete pHCIBuffer;

		return Addr;
	}
	
	// check if we can fill the aclbuffer
	if(!m_AclFrameQue.empty() && m_pACLBuffer) 
	{
		ACLFrame& frame = m_AclFrameQue.front();	

		LOG(WII_IPC_WIIMOTE, "Sending ACL frame.");
		UACLHeader* pHeader = (UACLHeader*)Memory::GetPointer(m_pACLBuffer->PayloadBuffer[0].m_Address);
		pHeader->ConnectionHandle = frame.ConnectionHandle;
		pHeader->BCFlag = 0;
		pHeader->PBFlag = 2;
		pHeader->Size = frame.size;

		memcpy(Memory::GetPointer(m_pACLBuffer->PayloadBuffer[0].m_Address + sizeof(UACLHeader)), frame.data , frame.size);

		// return reply buffer size
		Memory::Write_U32(sizeof(UACLHeader) + frame.size, m_pACLBuffer->m_Address + 0x4);

		delete frame.data;
		m_AclFrameQue.pop();

		u32 Addr = m_pACLBuffer->m_Address;
		delete m_pACLBuffer;
		m_pACLBuffer = NULL;

		return Addr;
	}

	if ((g_GlobalHandle != 0) && (g_HCICount > 0))
	{
		SendEventNumberOfCompletedPackets(g_GlobalHandle, g_HCICount*2);
		g_HCICount = 0;
	}

	if (m_AclFrameQue.empty())
	{
		for (size_t i=0; i<m_WiiMotes.size(); i++)
		{
			if (m_WiiMotes[i].Update())
				break;
		}
	}

	if (m_AclFrameQue.empty())
	{
		PluginWiimote::Wiimote_Update();
	}

#ifdef _WIN32

	static bool test = true;
	if (GetAsyncKeyState(VK_LBUTTON) && GetAsyncKeyState(VK_RBUTTON))
	{
		if (test)
		{
			for (size_t i=0; i<m_WiiMotes.size(); i++)
			{
				if (m_WiiMotes[i].EventPagingChanged(2))
				{
					Host_SetWiiMoteConnectionState(1);
					SendEventRequestConnection(m_WiiMotes[i]);
				}
			}
			test = false;
		}
	}
#endif

	return 0; 
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//
// --- events
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////

void CWII_IPC_HLE_Device_usb_oh1_57e_305::AddEventToQueue(const SQueuedEvent& _event)
{
	m_EventQueue.push(_event);
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventCommandStatus(u16 _Opcode)
{        
	SQueuedEvent Event(sizeof(SHCIEventStatus), 0);

	SHCIEventStatus* pHCIEvent = (SHCIEventStatus*)Event.m_buffer;
	pHCIEvent->EventType = 0x0F;
	pHCIEvent->PayloadLength = sizeof(SHCIEventStatus) - 2;
	pHCIEvent->Status = 0x0;
	pHCIEvent->PacketIndicator = 0x01;
	pHCIEvent->Opcode = _Opcode;

	AddEventToQueue(Event);

	LOG(WII_IPC_WIIMOTE, "Event: Command Status");
	LOG(WII_IPC_WIIMOTE, "  Opcode: 0x%04x", pHCIEvent->Opcode);

	return true;
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventCommandComplete(u16 _OpCode, void* _pData, u32 _DataSize)
{
	_dbg_assert_(WII_IPC_WIIMOTE, (sizeof(SHCIEventCommand) - 2 + _DataSize) < 256);

	SQueuedEvent Event(sizeof(SHCIEventCommand) + _DataSize, 0);

	SHCIEventCommand* pHCIEvent = (SHCIEventCommand*)Event.m_buffer;    
	pHCIEvent->EventType = 0x0E;          
	pHCIEvent->PayloadLength = (u8)(sizeof(SHCIEventCommand) - 2 + _DataSize);
	pHCIEvent->PacketIndicator = 0x01;     
	pHCIEvent->Opcode = _OpCode;

	// add the payload
	if ((_pData != NULL) && (_DataSize > 0))
	{
		u8* pPayload = Event.m_buffer + sizeof(SHCIEventCommand); 
		memcpy(pPayload, _pData, _DataSize);
	}

	AddEventToQueue(Event);

	LOG(WII_IPC_WIIMOTE, "Event: Command Complete");
	LOG(WII_IPC_WIIMOTE, "  Opcode: 0x%04x", pHCIEvent->Opcode);
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventInquiryResponse()
{    
	if (m_WiiMotes.empty())
		return false;

	_dbg_assert_(WII_IPC_WIIMOTE, sizeof(SHCIEventInquiryResult) - 2 + (m_WiiMotes.size() * sizeof(hci_inquiry_response)) < 256);

	SQueuedEvent Event(sizeof(SHCIEventInquiryResult) + m_WiiMotes.size()*sizeof(hci_inquiry_response), 0);

	SHCIEventInquiryResult* pInquiryResult = (SHCIEventInquiryResult*)Event.m_buffer;  

	pInquiryResult->EventType = 0x02;
	pInquiryResult->PayloadLength = (u8)(sizeof(SHCIEventInquiryResult) - 2 + (m_WiiMotes.size() * sizeof(hci_inquiry_response))); 
	pInquiryResult->num_responses = (u8)m_WiiMotes.size();

	for (size_t i=0; i<m_WiiMotes.size(); i++)
	{
		if (m_WiiMotes[i].IsConnected())
			continue;

		u8* pBuffer = Event.m_buffer + sizeof(SHCIEventInquiryResult) + i*sizeof(hci_inquiry_response);
		hci_inquiry_response* pResponse = (hci_inquiry_response*)pBuffer;   

		pResponse->bdaddr = m_WiiMotes[i].GetBD();
		pResponse->uclass[0]= m_WiiMotes[i].GetClass()[0];
		pResponse->uclass[1]= m_WiiMotes[i].GetClass()[1];
		pResponse->uclass[2]= m_WiiMotes[i].GetClass()[2];

		pResponse->page_scan_rep_mode = 1;
		pResponse->page_scan_period_mode = 0;
		pResponse->page_scan_mode = 0;
		pResponse->clock_offset = 0x3818;

		LOG(WII_IPC_WIIMOTE, "Event: Send Fake Inquriy of one controller");
		LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
			pResponse->bdaddr.b[0], pResponse->bdaddr.b[1], pResponse->bdaddr.b[2],
			pResponse->bdaddr.b[3], pResponse->bdaddr.b[4], pResponse->bdaddr.b[5]);
	}

	AddEventToQueue(Event);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventInquiryComplete()
{
	SQueuedEvent Event(sizeof(SHCIEventInquiryComplete), 0);

	SHCIEventInquiryComplete* pInquiryComplete = (SHCIEventInquiryComplete*)Event.m_buffer;    
	pInquiryComplete->EventType = 0x01;
	pInquiryComplete->PayloadLength = sizeof(SHCIEventInquiryComplete) - 2; 
	pInquiryComplete->Status = 0x00;

	AddEventToQueue(Event);

	LOG(WII_IPC_WIIMOTE, "Event: Inquiry complete");

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventRemoteNameReq(bdaddr_t _bd)
{    
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_bd);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventRemoteNameReq: Cant find WiiMote by bd: %02x:%02x:%02x:%02x:%02x:%02x",
			_bd.b[0], _bd.b[1], _bd.b[2], _bd.b[3], _bd.b[4], _bd.b[5]);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventRemoteNameReq), 0);

	SHCIEventRemoteNameReq* pRemoteNameReq = (SHCIEventRemoteNameReq*)Event.m_buffer;

	pRemoteNameReq->EventType = 0x07;
	pRemoteNameReq->PayloadLength = sizeof(SHCIEventRemoteNameReq) - 2;
	pRemoteNameReq->Status = 0x00;
	pRemoteNameReq->bdaddr = pWiiMote->GetBD();
	strcpy((char*)pRemoteNameReq->RemoteName, pWiiMote->GetName());

	AddEventToQueue(Event);

	LOG(WII_IPC_WIIMOTE, "Event: SendEventRemoteNameReq");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pRemoteNameReq->bdaddr.b[0], pRemoteNameReq->bdaddr.b[1], pRemoteNameReq->bdaddr.b[2],
		pRemoteNameReq->bdaddr.b[3], pRemoteNameReq->bdaddr.b[4], pRemoteNameReq->bdaddr.b[5]);
	LOG(WII_IPC_WIIMOTE, "  remotename: %s", pRemoteNameReq->RemoteName);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventRequestConnection(CWII_IPC_HLE_WiiMote& _rWiiMote)
{
	SQueuedEvent Event(sizeof(SHCIEventRequestConnection), 0);

	SHCIEventRequestConnection* pEventRequestConnection = (SHCIEventRequestConnection*)Event.m_buffer;

	pEventRequestConnection->EventType = 0x04;
	pEventRequestConnection->PayloadLength = sizeof(SHCIEventRequestConnection) - 2;
	pEventRequestConnection->bdaddr = _rWiiMote.GetBD(); 
	pEventRequestConnection->uclass[0] = _rWiiMote.GetClass()[0];
	pEventRequestConnection->uclass[1] = _rWiiMote.GetClass()[1];
	pEventRequestConnection->uclass[2] = _rWiiMote.GetClass()[2];
	pEventRequestConnection->LinkType = 0x01;

	AddEventToQueue(Event);

	// Log
#ifdef LOGGING
	static char LinkType[][128] =
	{	
		{ "HCI_LINK_SCO		0x00 - Voice"},	
		{ "HCI_LINK_ACL		0x01 - Data"},
		{ "HCI_LINK_eSCO	0x02 - eSCO"},
	};
#endif

	LOG(WII_IPC_WIIMOTE, "Event: SendEventRequestConnection");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x",
		pEventRequestConnection->bdaddr.b[0], pEventRequestConnection->bdaddr.b[1], pEventRequestConnection->bdaddr.b[2],
		pEventRequestConnection->bdaddr.b[3], pEventRequestConnection->bdaddr.b[4], pEventRequestConnection->bdaddr.b[5]);
	LOG(WII_IPC_WIIMOTE, "  COD[0]: 0x%02x", pEventRequestConnection->uclass[0]);
	LOG(WII_IPC_WIIMOTE, "  COD[1]: 0x%02x", pEventRequestConnection->uclass[1]);
	LOG(WII_IPC_WIIMOTE, "  COD[2]: 0x%02x", pEventRequestConnection->uclass[2]);
	LOG(WII_IPC_WIIMOTE, "  LinkType: %s", LinkType[pEventRequestConnection->LinkType]);

	return true;
};

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventRequestLinkKey(bdaddr_t _bd)
{
	const CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_bd);

	SQueuedEvent Event(sizeof(SHCIEventRequestLinkKey), 0);

	SHCIEventRequestLinkKey* pEventRequestLinkKey = (SHCIEventRequestLinkKey*)Event.m_buffer;

	pEventRequestLinkKey->EventType = 0x17;
	pEventRequestLinkKey->PayloadLength = sizeof(SHCIEventRequestLinkKey) - 2;
	pEventRequestLinkKey->bdaddr = _bd;

	AddEventToQueue(Event);

	LOG(WII_IPC_WIIMOTE, "Event: SendEventRequestLinkKey");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x",
		pEventRequestLinkKey->bdaddr.b[0], pEventRequestLinkKey->bdaddr.b[1], pEventRequestLinkKey->bdaddr.b[2],
		pEventRequestLinkKey->bdaddr.b[3], pEventRequestLinkKey->bdaddr.b[4], pEventRequestLinkKey->bdaddr.b[5]);

	return true;
};

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventLinkKeyNotification(const CWII_IPC_HLE_WiiMote& _rWiiMote)
{
	SQueuedEvent Event(sizeof(SHCIEventLinkKeyNotification), 0);

	SHCIEventLinkKeyNotification* pEventLinkKey = (SHCIEventLinkKeyNotification*)Event.m_buffer;

	pEventLinkKey->EventType = 0x15;
	pEventLinkKey->PayloadLength = sizeof(SHCIEventLinkKeyNotification) - 2;
	pEventLinkKey->numKeys = 1;
	pEventLinkKey->bdaddr = _rWiiMote.GetBD();
	memcpy(pEventLinkKey->LinkKey, _rWiiMote.GetLinkKey(), 16);

	AddEventToQueue(Event);

	LOG(WII_IPC_WIIMOTE, "Event: SendEventLinkKeyNotification");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x",
		pEventLinkKey->bdaddr.b[0], pEventLinkKey->bdaddr.b[1], pEventLinkKey->bdaddr.b[2],
		pEventLinkKey->bdaddr.b[3], pEventLinkKey->bdaddr.b[4], pEventLinkKey->bdaddr.b[5]);
	LOG_LinkKey(pEventLinkKey->LinkKey);

	return true;
};



bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventConnectionComplete(bdaddr_t _bd)
{    
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_bd);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventConnectionComplete: Cant find WiiMote by bd: %02x:%02x:%02x:%02x:%02x:%02x",
			_bd.b[0], _bd.b[1], _bd.b[2],
			_bd.b[3], _bd.b[4], _bd.b[5]);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventConnectionComplete), 0);

	SHCIEventConnectionComplete* pConnectionComplete = (SHCIEventConnectionComplete*)Event.m_buffer;    

	pConnectionComplete->EventType = 0x03;
	pConnectionComplete->PayloadLength = sizeof(SHCIEventConnectionComplete) - 2;
	pConnectionComplete->Status = 0x00;
	pConnectionComplete->Connection_Handle = pWiiMote->GetConnectionHandle();
	pConnectionComplete->bdaddr = pWiiMote->GetBD();
	pConnectionComplete->LinkType = 0x01; // ACL
	pConnectionComplete->EncryptionEnabled = 0x00;

	AddEventToQueue(Event);

	CWII_IPC_HLE_WiiMote* pWiimote = AccessWiiMote(_bd);
	if (pWiimote)
	{
		pWiimote->EventConnectionAccepted();
	}


	g_GlobalHandle = pConnectionComplete->Connection_Handle;
		
#ifdef LOGGING
	static char s_szLinkType[][128] =
	{	
		{ "HCI_LINK_SCO		0x00 - Voice"},	
		{ "HCI_LINK_ACL		0x01 - Data"},
		{ "HCI_LINK_eSCO	0x02 - eSCO"},
	};
#endif

	LOG(WII_IPC_WIIMOTE, "Event: SendEventConnectionComplete");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pConnectionComplete->Connection_Handle);
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pConnectionComplete->bdaddr.b[0], pConnectionComplete->bdaddr.b[1], pConnectionComplete->bdaddr.b[2],
		pConnectionComplete->bdaddr.b[3], pConnectionComplete->bdaddr.b[4], pConnectionComplete->bdaddr.b[5]);
	LOG(WII_IPC_WIIMOTE, "  LinkType: %s", s_szLinkType[pConnectionComplete->LinkType]);
	LOG(WII_IPC_WIIMOTE, "  EncryptionEnabled: %i", pConnectionComplete->EncryptionEnabled);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventRoleChange(bdaddr_t _bd, bool _master)
{    
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_bd);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventRoleChange: Cant find WiiMote by bd: %02x:%02x:%02x:%02x:%02x:%02x",
			_bd.b[0], _bd.b[1], _bd.b[2],
			_bd.b[3], _bd.b[4], _bd.b[5]);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventRoleChange), 0);

	SHCIEventRoleChange* pRoleChange = (SHCIEventRoleChange*)Event.m_buffer;    

	pRoleChange->EventType = 0x12;
	pRoleChange->PayloadLength = sizeof(SHCIEventRoleChange) - 2;
	pRoleChange->Status = 0x00;
	pRoleChange->bdaddr = pWiiMote->GetBD();
	pRoleChange->NewRole = _master ? 0x00 : 0x01;

	AddEventToQueue(Event);

	LOG(WII_IPC_WIIMOTE, "Event: SendEventRoleChange");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pRoleChange->bdaddr.b[0], pRoleChange->bdaddr.b[1], pRoleChange->bdaddr.b[2],
		pRoleChange->bdaddr.b[3], pRoleChange->bdaddr.b[4], pRoleChange->bdaddr.b[5]);
	LOG(WII_IPC_WIIMOTE, "  NewRole: %i", pRoleChange->NewRole);

	return true;
}



bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventReadClockOffsetComplete(u16 _connectionHandle)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_connectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventReadClockOffsetComplete: Cant find WiiMote by connection handle: %02x", _connectionHandle);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventReadClockOffsetComplete), _connectionHandle);

	SHCIEventReadClockOffsetComplete* pReadClockOffsetComplete = (SHCIEventReadClockOffsetComplete*)Event.m_buffer;
	pReadClockOffsetComplete->EventType = 0x1C;
	pReadClockOffsetComplete->PayloadLength = sizeof(SHCIEventReadClockOffsetComplete) - 2; 
	pReadClockOffsetComplete->Status = 0x00;
	pReadClockOffsetComplete->ConnectionHandle = pWiiMote->GetConnectionHandle();
	pReadClockOffsetComplete->ClockOffset = 0x3818;

	AddEventToQueue(Event);

	// Log
	LOG(WII_IPC_WIIMOTE, "Event: SendEventReadClockOffsetComplete");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pReadClockOffsetComplete->ConnectionHandle);
	LOG(WII_IPC_WIIMOTE, "  ClockOffset: 0x%04x", pReadClockOffsetComplete->ClockOffset);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventReadRemoteVerInfo(u16 _connectionHandle)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_connectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventReadRemoteVerInfo: Cant find WiiMote by connection handle: %02x", _connectionHandle);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventReadRemoteVerInfo), _connectionHandle);

	SHCIEventReadRemoteVerInfo* pReadRemoteVerInfo = (SHCIEventReadRemoteVerInfo*)Event.m_buffer;    
	pReadRemoteVerInfo->EventType = 0x0C;
	pReadRemoteVerInfo->PayloadLength = sizeof(SHCIEventReadRemoteVerInfo) - 2; 
	pReadRemoteVerInfo->Status = 0x00;
	pReadRemoteVerInfo->ConnectionHandle = pWiiMote->GetConnectionHandle();
	pReadRemoteVerInfo->lmp_version = pWiiMote->GetLMPVersion();
	pReadRemoteVerInfo->manufacturer = pWiiMote->GetManufactorID();
	pReadRemoteVerInfo->lmp_subversion = pWiiMote->GetLMPSubVersion();

	AddEventToQueue(Event);

	// Log
	LOG(WII_IPC_WIIMOTE, "Event: SendEventReadRemoteVerInfo");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pReadRemoteVerInfo->ConnectionHandle);
	LOG(WII_IPC_WIIMOTE, "  lmp_version: 0x%02x", pReadRemoteVerInfo->lmp_version);
	LOG(WII_IPC_WIIMOTE, "  manufacturer: 0x%04x", pReadRemoteVerInfo->manufacturer);
	LOG(WII_IPC_WIIMOTE, "  lmp_subversion: 0x%04x", pReadRemoteVerInfo->lmp_subversion);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventReadRemoteFeatures(u16 _connectionHandle)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_connectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventReadRemoteFeatures: Cant find WiiMote by connection handle: %02x", _connectionHandle);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventReadRemoteFeatures), _connectionHandle);

	SHCIEventReadRemoteFeatures* pReadRemoteFeatures = (SHCIEventReadRemoteFeatures*)Event.m_buffer;
	pReadRemoteFeatures->EventType = 0x0C;
	pReadRemoteFeatures->PayloadLength = sizeof(SHCIEventReadRemoteFeatures) - 2; 
	pReadRemoteFeatures->Status = 0x00;
	pReadRemoteFeatures->ConnectionHandle = pWiiMote->GetConnectionHandle();
	pReadRemoteFeatures->features[0] = pWiiMote->GetFeatures()[0];
	pReadRemoteFeatures->features[1] = pWiiMote->GetFeatures()[1];
	pReadRemoteFeatures->features[2] = pWiiMote->GetFeatures()[2];
	pReadRemoteFeatures->features[3] = pWiiMote->GetFeatures()[3];
	pReadRemoteFeatures->features[4] = pWiiMote->GetFeatures()[4];
	pReadRemoteFeatures->features[5] = pWiiMote->GetFeatures()[5];
	pReadRemoteFeatures->features[6] = pWiiMote->GetFeatures()[6];
	pReadRemoteFeatures->features[7] = pWiiMote->GetFeatures()[7];

	AddEventToQueue(Event);

	// Log
	LOG(WII_IPC_WIIMOTE, "Event: SendEventReadRemoteFeatures");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pReadRemoteFeatures->ConnectionHandle);
	LOG(WII_IPC_WIIMOTE, "  features: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",   
		pReadRemoteFeatures->features[0], pReadRemoteFeatures->features[1], pReadRemoteFeatures->features[2],
		pReadRemoteFeatures->features[3], pReadRemoteFeatures->features[4], pReadRemoteFeatures->features[5], 
		pReadRemoteFeatures->features[6], pReadRemoteFeatures->features[7]);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventNumberOfCompletedPackets(u16 _connectionHandle, u16 _count)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_connectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventNumberOfCompletedPackets: Cant find WiiMote by connection handle %02x", _connectionHandle);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventNumberOfCompletedPackets), 0); // zero, so this packet isnt counted

	SHCIEventNumberOfCompletedPackets* pNumberOfCompletedPackets = (SHCIEventNumberOfCompletedPackets*)Event.m_buffer;
	pNumberOfCompletedPackets->EventType = 0x13;
	pNumberOfCompletedPackets->PayloadLength = sizeof(SHCIEventNumberOfCompletedPackets) - 2; 
	pNumberOfCompletedPackets->NumberOfHandles = 1;
	pNumberOfCompletedPackets->Connection_Handle = _connectionHandle;
	pNumberOfCompletedPackets->Number_Of_Completed_Packets = _count;

	AddEventToQueue(Event);

	// Log
	LOG(WII_IPC_WIIMOTE, "Event: SendEventNumberOfCompletedPackets");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pNumberOfCompletedPackets->Connection_Handle);
	LOG(WII_IPC_WIIMOTE, "  Number_Of_Completed_Packets: %i", pNumberOfCompletedPackets->Number_Of_Completed_Packets);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventAuthenticationCompleted(u16 _connectionHandle)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_connectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventAuthenticationCompleted: Cant find WiiMote by connection handle %02x", _connectionHandle);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventAuthenticationCompleted), _connectionHandle);

	SHCIEventAuthenticationCompleted* pEventAuthenticationCompleted = (SHCIEventAuthenticationCompleted*)Event.m_buffer;
	pEventAuthenticationCompleted->EventType = 0x06;
	pEventAuthenticationCompleted->PayloadLength = sizeof(SHCIEventAuthenticationCompleted) - 2; 
	pEventAuthenticationCompleted->Status = 0;
	pEventAuthenticationCompleted->Connection_Handle = _connectionHandle;

	AddEventToQueue(Event);

	// Log
	LOG(WII_IPC_WIIMOTE, "Event: SendEventAuthenticationCompleted");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pEventAuthenticationCompleted->Connection_Handle);

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventModeChange(u16 _connectionHandle, u8 _mode, u16 _value)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_connectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventModeChange: Cant find WiiMote by connection handle %02x", _connectionHandle);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventModeChange), _connectionHandle);

	SHCIEventModeChange* pModeChange = (SHCIEventModeChange*)Event.m_buffer;
	pModeChange->EventType = 0x14;
	pModeChange->PayloadLength = sizeof(SHCIEventModeChange) - 2; 
	pModeChange->Status = 0;
	pModeChange->Connection_Handle = _connectionHandle;
	pModeChange->CurrentMode = _mode;
	pModeChange->Value = _value;

	AddEventToQueue(Event);

	// Log
	LOG(WII_IPC_WIIMOTE, "Event: SendEventModeChange");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pModeChange->Connection_Handle);
	LOG(WII_IPC_WIIMOTE, "  missing other paramter :)");

	return true;
}

bool CWII_IPC_HLE_Device_usb_oh1_57e_305::SendEventDisconnect(u16 _connectionHandle, u8 _Reason)
{
	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(_connectionHandle);
	if (pWiiMote == NULL)
	{
		PanicAlert("SendEventDisconnect: Cant find WiiMote by connection handle %02x", _connectionHandle);
		return false;
	}

	SQueuedEvent Event(sizeof(SHCIEventDisconnectCompleted), _connectionHandle);

	SHCIEventDisconnectCompleted* pDisconnect = (SHCIEventDisconnectCompleted*)Event.m_buffer;
	pDisconnect->EventType = 0x06;
	pDisconnect->PayloadLength = sizeof(SHCIEventDisconnectCompleted) - 2; 
	pDisconnect->Status = 0;
	pDisconnect->Connection_Handle = _connectionHandle;
	pDisconnect->Reason = _Reason;

	AddEventToQueue(Event);

	// Log
	LOG(WII_IPC_WIIMOTE, "Event: SendEventDisconnect");
	LOG(WII_IPC_WIIMOTE, "  Connection_Handle: 0x%04x", pDisconnect->Connection_Handle);
	LOG(WII_IPC_WIIMOTE, "  Reason: 0x%02x", pDisconnect->Reason);

	return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
//
// --- command dispacther
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////

void CWII_IPC_HLE_Device_usb_oh1_57e_305::ExecuteHCICommandMessage(const SHCICommandMessage& _rHCICommandMessage)
{
	u8* pInput = Memory::GetPointer(_rHCICommandMessage.m_PayLoadAddr + 3);
	SCommandMessage* pMsg = (SCommandMessage*)Memory::GetPointer(_rHCICommandMessage.m_PayLoadAddr);

	u16 ocf = HCI_OCF(pMsg->Opcode);
	u16 ogf = HCI_OGF(pMsg->Opcode);
	LOG(WII_IPC_WIIMOTE, "****************************** ExecuteHCICommandMessage(0x%04x)"
					"(ocf: 0x%02x, ogf: 0x%02x)", pMsg->Opcode, ocf, ogf);

	switch(pMsg->Opcode)
	{
		// 
		// --- read commandos ---
		//
	case HCI_CMD_RESET:
		CommandReset(pInput);
		break;

	case HCI_CMD_READ_BUFFER_SIZE:
		CommandReadBufferSize(pInput);
		break;

	case HCI_CMD_READ_LOCAL_VER:
		CommandReadLocalVer(pInput);
		break;

	case HCI_CMD_READ_BDADDR:
		CommandReadBDAdrr(pInput);
		break;

	case HCI_CMD_READ_LOCAL_FEATURES:
		CommandReadLocalFeatures(pInput);
		break;

	case HCI_CMD_READ_STORED_LINK_KEY:
		CommandReadStoredLinkKey(pInput);
		break;

	case HCI_CMD_WRITE_UNIT_CLASS:
		CommandWriteUnitClass(pInput);
		break;

	case HCI_CMD_WRITE_LOCAL_NAME:
		CommandWriteLocalName(pInput);
		break;

	case HCI_CMD_WRITE_PIN_TYPE:
		CommandWritePinType(pInput);
		break;

	case HCI_CMD_HOST_BUFFER_SIZE:
		CommandHostBufferSize(pInput);
		break;

	case HCI_CMD_WRITE_PAGE_TIMEOUT:
		CommandWritePageTimeOut(pInput);
		break;

	case HCI_CMD_WRITE_SCAN_ENABLE:
		CommandWriteScanEnable(pInput);
		break;

	case HCI_CMD_WRITE_INQUIRY_MODE:
		CommandWriteInquiryMode(pInput);
		break;

	case HCI_CMD_WRITE_PAGE_SCAN_TYPE:
		CommandWritePageScanType(pInput);
		break;

	case HCI_CMD_SET_EVENT_FILTER:
		CommandSetEventFilter(pInput);        
		break;

	case HCI_CMD_INQUIRY:
		CommandInquiry(pInput);        
		break;

	case HCI_CMD_WRITE_INQUIRY_SCAN_TYPE:
		CommandWriteInquiryScanType(pInput);        
		break;

		// vendor specific...
	case 0xFC4C:
		CommandVendorSpecific_FC4C(pInput, _rHCICommandMessage.m_PayLoadSize - 3); 
		break;

	case 0xFC4F:
		CommandVendorSpecific_FC4F(pInput, _rHCICommandMessage.m_PayLoadSize - 3); 
		break;

	case HCI_CMD_INQUIRY_CANCEL:
		CommandInquiryCancel(pInput);        
		break;

	case HCI_CMD_REMOTE_NAME_REQ:
		CommandRemoteNameReq(pInput);
		break;

	case HCI_CMD_CREATE_CON:
		CommandCreateCon(pInput);
		break;

	case HCI_CMD_ACCEPT_CON:
		CommandAcceptCon(pInput);
		break;

	case HCI_CMD_READ_CLOCK_OFFSET:
		CommandReadClockOffset(pInput);
		break;

	case HCI_CMD_READ_REMOTE_VER_INFO:
		CommandReadRemoteVerInfo(pInput);
		break;

	case HCI_CMD_READ_REMOTE_FEATURES:
		CommandReadRemoteFeatures(pInput);
		break;
		
	case HCI_CMD_WRITE_LINK_POLICY_SETTINGS:
		CommandWriteLinkPolicy(pInput);
		break;

	case HCI_CMD_AUTH_REQ:
		CommandAuthenticationRequested(pInput);
		break;

	case HCI_CMD_SNIFF_MODE:
		CommandSniffMode(pInput);
		break;

	case HCI_CMD_DISCONNECT:
		CommandDisconnect(pInput);
		break;

	case HCI_CMD_WRITE_LINK_SUPERVISION_TIMEOUT:
		CommandWriteLinkSupervisionTimeout(pInput);
		break;

	case HCI_CMD_LINK_KEY_NEG_REP:
		CommandLinkKeyNegRep(pInput);
		break;

	case HCI_CMD_LINK_KEY_REP:
		CommandLinkKeyRep(pInput);
		break;


		// 
		// --- default ---
		//
	default:
		{
			u16 ocf = HCI_OCF(pMsg->Opcode);
			u16 ogf = HCI_OGF(pMsg->Opcode);
			
			if (ogf == 0x3f)
			{
				PanicAlert("Vendor specific HCI command");
				LOG(WII_IPC_WIIMOTE, "Command: vendor specific: 0x%04X (ocf: 0x%x)", pMsg->Opcode, ocf);

				for (int i=0; i<pMsg->len; i++)
				{
					LOG(WII_IPC_WIIMOTE, "  0x02%x", pInput[i]);
				}
			}
			else
			{
				_dbg_assert_msg_(WII_IPC_WIIMOTE, 0, "Unknown USB_IOCTL_CTRLMSG: 0x%04X (ocf: 0x%x  ogf 0x%x)", pMsg->Opcode, ocf, ogf);
			}

			// send fake all is okay msg...
			SendEventCommandComplete(pMsg->Opcode, NULL, 0);
		}
		break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//
// --- command helper
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReset(u8* _Input)
{
	// reply
	hci_status_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_RESET");

	SendEventCommandComplete(HCI_CMD_RESET, &Reply, sizeof(hci_status_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadBufferSize(u8* _Input)
{
	// reply
	hci_read_buffer_size_rp Reply;
	Reply.status = 0x00;
	Reply.max_acl_size = 339;
	Reply.num_acl_pkts = 10;
	Reply.max_sco_size = 64;
	Reply.num_sco_pkts = 0;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_BUFFER_SIZE:");
	LOG(WII_IPC_WIIMOTE, "return:");
	LOG(WII_IPC_WIIMOTE, "  max_acl_size: %i", Reply.max_acl_size);
	LOG(WII_IPC_WIIMOTE, "  num_acl_pkts: %i", Reply.num_acl_pkts);
	LOG(WII_IPC_WIIMOTE, "  max_sco_size: %i", Reply.max_sco_size);
	LOG(WII_IPC_WIIMOTE, "  num_sco_pkts: %i", Reply.num_sco_pkts);

	SendEventCommandComplete(HCI_CMD_READ_BUFFER_SIZE, &Reply, sizeof(hci_read_buffer_size_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadLocalVer(u8* _Input)
{
	// reply
	hci_read_local_ver_rp Reply;
	Reply.status = 0x00;
	Reply.hci_version = 0x03;        // HCI version: 1.1
	Reply.hci_revision = 0x40a7;     // current revision (?)
	Reply.lmp_version = 0x03;        // LMP version: 1.1    
	Reply.manufacturer = 0x000F;     // manufacturer: reserved for tests
	Reply.lmp_subversion = 0x430e;   // LMP subversion

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_LOCAL_VER:");
	LOG(WII_IPC_WIIMOTE, "return:");
	LOG(WII_IPC_WIIMOTE, "  status:         %i", Reply.status);
	LOG(WII_IPC_WIIMOTE, "  hci_revision:   %i", Reply.hci_revision);
	LOG(WII_IPC_WIIMOTE, "  lmp_version:    %i", Reply.lmp_version);
	LOG(WII_IPC_WIIMOTE, "  manufacturer:   %i", Reply.manufacturer);
	LOG(WII_IPC_WIIMOTE, "  lmp_subversion: %i", Reply.lmp_subversion);

	SendEventCommandComplete(HCI_CMD_READ_LOCAL_VER, &Reply, sizeof(hci_read_local_ver_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadBDAdrr(u8* _Input)
{
	// reply
	hci_read_bdaddr_rp Reply;
	Reply.status = 0x00;
	Reply.bdaddr = m_ControllerBD;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_BDADDR:");
	LOG(WII_IPC_WIIMOTE, "return:");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		Reply.bdaddr.b[0], Reply.bdaddr.b[1], Reply.bdaddr.b[2],
		Reply.bdaddr.b[3], Reply.bdaddr.b[4], Reply.bdaddr.b[5]);

	SendEventCommandComplete(HCI_CMD_READ_BDADDR, &Reply, sizeof(hci_read_bdaddr_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadLocalFeatures(u8* _Input)
{
	// reply
	hci_read_local_features_rp Reply;    
	Reply.status = 0x00;
	Reply.features[0] = 0xFF;
	Reply.features[1] = 0xFF;
	Reply.features[2] = 0x8D;
	Reply.features[3] = 0xFE;
	Reply.features[4] = 0x9B;
	Reply.features[5] = 0xF9;
	Reply.features[6] = 0x00;
	Reply.features[7] = 0x80;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_LOCAL_FEATURES:");
	LOG(WII_IPC_WIIMOTE, "return:");
	LOG(WII_IPC_WIIMOTE, "  features: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",   
		Reply.features[0], Reply.features[1], Reply.features[2],
		Reply.features[3], Reply.features[4], Reply.features[5], 
		Reply.features[6], Reply.features[7]);

	SendEventCommandComplete(HCI_CMD_READ_LOCAL_FEATURES, &Reply, sizeof(hci_read_local_features_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadStoredLinkKey(u8* _Input)
{
	hci_read_stored_link_key_cp* ReadStoredLinkKey = (hci_read_stored_link_key_cp*)_Input;

	// reply
	hci_read_stored_link_key_rp Reply;    
	Reply.status = 0x00;

	Reply.max_num_keys = 255;
	if (ReadStoredLinkKey->read_all)
	{
		Reply.num_keys_read = (u16)m_WiiMotes.size();
	}
	else	
	{
		PanicAlert("CommandReadStoredLinkKey");
	}
	
	// logging
	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_STORED_LINK_KEY:");
	LOG(WII_IPC_WIIMOTE, "input:");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		ReadStoredLinkKey->bdaddr.b[0], ReadStoredLinkKey->bdaddr.b[1], ReadStoredLinkKey->bdaddr.b[2],
		ReadStoredLinkKey->bdaddr.b[3], ReadStoredLinkKey->bdaddr.b[4], ReadStoredLinkKey->bdaddr.b[5]);
	LOG(WII_IPC_WIIMOTE, "  read_all: %i", ReadStoredLinkKey->read_all);
	LOG(WII_IPC_WIIMOTE, "return:");
	LOG(WII_IPC_WIIMOTE, "  max_num_keys: %i", Reply.max_num_keys);
	LOG(WII_IPC_WIIMOTE, "  num_keys_read: %i", Reply.num_keys_read);

	// generate link key
	for (size_t i=0; i<m_WiiMotes.size(); i++)
	{
		SendEventLinkKeyNotification(m_WiiMotes[i]);
	}

	SendEventCommandComplete(HCI_CMD_READ_STORED_LINK_KEY, &Reply, sizeof(hci_read_stored_link_key_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWriteUnitClass(u8* _Input)
{
	// command parameters
	hci_write_unit_class_cp* pWriteUnitClass = (hci_write_unit_class_cp*)_Input;
	m_ClassOfDevice[0] = pWriteUnitClass->uclass[0];
	m_ClassOfDevice[1] = pWriteUnitClass->uclass[1];
	m_ClassOfDevice[2] = pWriteUnitClass->uclass[2];

	// reply
	hci_write_unit_class_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_UNIT_CLASS:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  COD[0]: 0x%02x", pWriteUnitClass->uclass[0]);
	LOG(WII_IPC_WIIMOTE, "  COD[1]: 0x%02x", pWriteUnitClass->uclass[1]);
	LOG(WII_IPC_WIIMOTE, "  COD[2]: 0x%02x", pWriteUnitClass->uclass[2]);

	SendEventCommandComplete(HCI_CMD_WRITE_UNIT_CLASS, &Reply, sizeof(hci_write_unit_class_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWriteLocalName(u8* _Input)
{
	// command parameters
	hci_write_local_name_cp* pWriteLocalName = (hci_write_local_name_cp*)_Input;
	memcpy(m_LocalName, pWriteLocalName->name, HCI_UNIT_NAME_SIZE);

	// reply
	hci_write_local_name_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_LOCAL_NAME:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  local_name: %s", pWriteLocalName->name);

	SendEventCommandComplete(HCI_CMD_WRITE_LOCAL_NAME, &Reply, sizeof(hci_write_local_name_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWritePinType(u8* _Input)
{
	// command parameters
	hci_write_pin_type_cp* pWritePinType = (hci_write_pin_type_cp*)_Input;
	m_PINType =  pWritePinType->pin_type;

	// reply
	hci_write_pin_type_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_PIN_TYPE:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  pin_type: %x", pWritePinType->pin_type);

	SendEventCommandComplete(HCI_CMD_WRITE_PIN_TYPE, &Reply, sizeof(hci_write_pin_type_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandHostBufferSize(u8* _Input)
{
	// command parameters
	hci_host_buffer_size_cp* pHostBufferSize = (hci_host_buffer_size_cp*)_Input;
	m_HostMaxACLSize = pHostBufferSize->max_acl_size;
	m_HostMaxSCOSize = pHostBufferSize->max_sco_size;
	m_HostNumACLPackets = pHostBufferSize->num_acl_pkts;
	m_HostNumSCOPackets = pHostBufferSize->num_sco_pkts;

	// reply
	hci_host_buffer_size_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_HOST_BUFFER_SIZE:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  max_acl_size: %i", pHostBufferSize->max_acl_size);
	LOG(WII_IPC_WIIMOTE, "  max_sco_size: %i", pHostBufferSize->max_sco_size);
	LOG(WII_IPC_WIIMOTE, "  num_acl_pkts: %i", pHostBufferSize->num_acl_pkts);
	LOG(WII_IPC_WIIMOTE, "  num_sco_pkts: %i", pHostBufferSize->num_sco_pkts);

	SendEventCommandComplete(HCI_CMD_HOST_BUFFER_SIZE, &Reply, sizeof(hci_host_buffer_size_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWritePageTimeOut(u8* _Input)
{
#ifdef LOGGING
	// command parameters
	hci_write_page_timeout_cp* pWritePageTimeOut = (hci_write_page_timeout_cp*)_Input;
#endif

	// reply
	hci_host_buffer_size_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_PAGE_TIMEOUT:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  timeout: %i", pWritePageTimeOut->timeout);

	SendEventCommandComplete(HCI_CMD_WRITE_PAGE_TIMEOUT, &Reply, sizeof(hci_host_buffer_size_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWriteScanEnable(u8* _Input)
{
	// command parameters
	hci_write_scan_enable_cp* pWriteScanEnable = (hci_write_scan_enable_cp*)_Input;
	m_ScanEnable = pWriteScanEnable->scan_enable;

	// reply
	hci_write_scan_enable_rp Reply;
	Reply.status = 0x00;

#ifdef LOGGING
	static char Scanning[][128] =
	{
		{ "HCI_NO_SCAN_ENABLE"},
		{ "HCI_INQUIRY_SCAN_ENABLE"},
		{ "HCI_PAGE_SCAN_ENABLE"},
		{ "HCI_INQUIRY_AND_PAGE_SCAN_ENABLE"},
	};
#endif

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_SCAN_ENABLE:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  scan_enable: %s", Scanning[pWriteScanEnable->scan_enable]);

	SendEventCommandComplete(HCI_CMD_WRITE_SCAN_ENABLE, &Reply, sizeof(hci_write_scan_enable_rp));

	return;

	// TODO: fix this ugly request connection hack :)
	//for homebrew works this	
	for (size_t i=0; i<m_WiiMotes.size(); i++)
	{
		if (m_WiiMotes[i].EventPagingChanged(pWriteScanEnable->scan_enable))
		{
			SendEventRequestConnection(m_WiiMotes[i]);
		}
	}
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWriteInquiryMode(u8* _Input)
{
#ifdef LOGGING
	// command parameters
	hci_write_inquiry_mode_cp* pInquiryMode = (hci_write_inquiry_mode_cp*)_Input;
#endif

	// reply
	hci_write_inquiry_mode_rp Reply;
	Reply.status = 0x00;

#ifdef LOGGING
	static char InquiryMode[][128] =
	{
		{ "Standard Inquiry Result event format (default)" },
		{ "Inquiry Result format with RSSI" },
		{ "Inquiry Result with RSSI format or Extended Inquiry Result format" }
	};
#endif
	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_INQUIRY_MODE:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  mode: %s", InquiryMode[pInquiryMode->mode]);

	SendEventCommandComplete(HCI_CMD_WRITE_INQUIRY_MODE, &Reply, sizeof(hci_write_inquiry_mode_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWritePageScanType(u8* _Input)
{
#ifdef LOGGING
	// command parameters
	hci_write_page_scan_type_cp* pWritePageScanType = (hci_write_page_scan_type_cp*)_Input;
#endif

	// reply
	hci_write_page_scan_type_rp Reply;
	Reply.status = 0x00;

#ifdef LOGGING
	static char PageScanType[][128] =
	{
		{ "Mandatory: Standard Scan (default)" },
		{ "Optional: Interlaced Scan" }
	};
#endif

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_PAGE_SCAN_TYPE:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  type: %s", PageScanType[pWritePageScanType->type]);

	SendEventCommandComplete(HCI_CMD_WRITE_PAGE_SCAN_TYPE, &Reply, sizeof(hci_write_page_scan_type_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandSetEventFilter(u8* _Input)
{
	// command parameters
	hci_set_event_filter_cp* pSetEventFilter = (hci_set_event_filter_cp*)_Input;
	m_EventFilterType = pSetEventFilter->filter_type;
	m_EventFilterCondition = pSetEventFilter->filter_condition_type;

	// reply
	hci_set_event_filter_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_SET_EVENT_FILTER:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  filter_type: %i", pSetEventFilter->filter_type);
	LOG(WII_IPC_WIIMOTE, "  filter_condition_type: %i", pSetEventFilter->filter_condition_type);

	SendEventCommandComplete(HCI_CMD_SET_EVENT_FILTER, &Reply, sizeof(hci_set_event_filter_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandInquiry(u8* _Input)
{
	// command parameters
	hci_inquiry_cp* pInquiry = (hci_inquiry_cp*)_Input;
	u8 lap[HCI_LAP_SIZE];

	memcpy(lap, pInquiry->lap, HCI_LAP_SIZE);

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_INQUIRY:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  LAP[0]: 0x%02x", pInquiry->lap[0]);
	LOG(WII_IPC_WIIMOTE, "  LAP[1]: 0x%02x", pInquiry->lap[1]);
	LOG(WII_IPC_WIIMOTE, "  LAP[2]: 0x%02x", pInquiry->lap[2]);
	LOG(WII_IPC_WIIMOTE, "  inquiry_length: %i (N x 1.28) sec", pInquiry->inquiry_length);
	LOG(WII_IPC_WIIMOTE, "  num_responses: %i (N x 1.28) sec", pInquiry->num_responses);       

	SendEventCommandStatus(HCI_CMD_INQUIRY);
	SendEventInquiryResponse();
	SendEventInquiryComplete(); 
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWriteInquiryScanType(u8* _Input)
{
#ifdef LOGGING
	// command parameters
	hci_write_inquiry_scan_type_cp* pSetEventFilter = (hci_write_inquiry_scan_type_cp*)_Input;
#endif
	// reply
	hci_write_inquiry_scan_type_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_INQUIRY_SCAN_TYPE:");
	LOG(WII_IPC_WIIMOTE, "write:");
	LOG(WII_IPC_WIIMOTE, "  type: %i", pSetEventFilter->type);

	SendEventCommandComplete(HCI_CMD_WRITE_INQUIRY_SCAN_TYPE, &Reply, sizeof(hci_write_inquiry_scan_type_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandVendorSpecific_FC4F(u8* _Input, u32 _Size)
{
	// callstack...
	// BTM_VendorSpecificCommad()
	// WUDiRemovePatch()
	// WUDiAppendRuntimePatch()
	// WUDiGetFirmwareVersion()
	// WUDiStackSetupComplete()

	// reply
	hci_status_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: CommandVendorSpecific_FC4F: (callstack WUDiRemovePatch)");
	LOG(WII_IPC_WIIMOTE, "input (size 0x%x):", _Size);

	Debugger::PrintDataBuffer(LogTypes::WII_IPC_WIIMOTE, _Input, _Size, "Data: ");

	SendEventCommandComplete(0xFC4F, &Reply, sizeof(hci_status_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandVendorSpecific_FC4C(u8* _Input, u32 _Size)
{
	// reply
	hci_status_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: CommandVendorSpecific_FC4C:");
	LOG(WII_IPC_WIIMOTE, "input (size 0x%x):", _Size);
	Debugger::PrintDataBuffer(LogTypes::WII_IPC_WIIMOTE, _Input, _Size, "Data: ");

	SendEventCommandComplete(0xFC4C, &Reply, sizeof(hci_status_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandInquiryCancel(u8* _Input)
{
	// reply
	hci_inquiry_cancel_rp Reply;
	Reply.status = 0x00;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_INQUIRY_CANCEL");

	SendEventCommandComplete(HCI_CMD_INQUIRY_CANCEL, &Reply, sizeof(hci_inquiry_cancel_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandRemoteNameReq(u8* _Input)
{
	// command parameters
	hci_remote_name_req_cp* pRemoteNameReq = (hci_remote_name_req_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_REMOTE_NAME_REQ");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pRemoteNameReq->bdaddr.b[0], pRemoteNameReq->bdaddr.b[1], pRemoteNameReq->bdaddr.b[2],
		pRemoteNameReq->bdaddr.b[3], pRemoteNameReq->bdaddr.b[4], pRemoteNameReq->bdaddr.b[5]);
	LOG(WII_IPC_WIIMOTE, "  page_scan_rep_mode: %i", pRemoteNameReq->page_scan_rep_mode);
	LOG(WII_IPC_WIIMOTE, "  page_scan_mode: %i", pRemoteNameReq->page_scan_mode);
	LOG(WII_IPC_WIIMOTE, "  clock_offset: %i", pRemoteNameReq->clock_offset);

	SendEventCommandStatus(HCI_CMD_REMOTE_NAME_REQ);
	SendEventRemoteNameReq(pRemoteNameReq->bdaddr);
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandCreateCon(u8* _Input)
{
	// command parameters
	hci_create_con_cp* pCreateCon = (hci_create_con_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_CREATE_CON");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pCreateCon->bdaddr.b[0], pCreateCon->bdaddr.b[1], pCreateCon->bdaddr.b[2],
		pCreateCon->bdaddr.b[3], pCreateCon->bdaddr.b[4], pCreateCon->bdaddr.b[5]);

	LOG(WII_IPC_WIIMOTE, "  pkt_type: %i", pCreateCon->pkt_type);
	LOG(WII_IPC_WIIMOTE, "  page_scan_rep_mode: %i", pCreateCon->page_scan_rep_mode);
	LOG(WII_IPC_WIIMOTE, "  page_scan_mode: %i", pCreateCon->page_scan_mode);
	LOG(WII_IPC_WIIMOTE, "  clock_offset: %i", pCreateCon->clock_offset);
	LOG(WII_IPC_WIIMOTE, "  accept_role_switch: %i", pCreateCon->accept_role_switch);

	SendEventCommandStatus(HCI_CMD_CREATE_CON);
	SendEventConnectionComplete(pCreateCon->bdaddr);
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandAcceptCon(u8* _Input)
{
	// command parameters
	hci_accept_con_cp* pAcceptCon = (hci_accept_con_cp*)_Input;

#ifdef LOGGING
	static char s_szRole[][128] =
	{	
		{ "Master (0x00)"},
		{ "Slave (0x01)"},
	};
#endif

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_ACCEPT_CON");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pAcceptCon->bdaddr.b[0], pAcceptCon->bdaddr.b[1], pAcceptCon->bdaddr.b[2],
		pAcceptCon->bdaddr.b[3], pAcceptCon->bdaddr.b[4], pAcceptCon->bdaddr.b[5]);
	LOG(WII_IPC_WIIMOTE, "  role: %s", s_szRole[pAcceptCon->role]);

	SendEventCommandStatus(HCI_CMD_ACCEPT_CON);

	// this connection wants to be the master
	if (pAcceptCon->role == 0)
	{
		SendEventRoleChange(pAcceptCon->bdaddr, true);
	}
	
	SendEventConnectionComplete(pAcceptCon->bdaddr);
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadClockOffset(u8* _Input)
{
	// command parameters
	hci_read_clock_offset_cp* pReadClockOffset = (hci_read_clock_offset_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_CLOCK_OFFSET");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  ConnectionHandle: 0x%02x", pReadClockOffset->con_handle);

	SendEventCommandStatus(HCI_CMD_READ_CLOCK_OFFSET);
	SendEventReadClockOffsetComplete(pReadClockOffset->con_handle);


//	CWII_IPC_HLE_WiiMote* pWiiMote = AccessWiiMote(pReadClockOffset->con_handle);
//	SendEventRequestLinkKey(pWiiMote->GetBD());
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadRemoteVerInfo(u8* _Input)
{
	// command parameters
	hci_read_remote_ver_info_cp* pReadRemoteVerInfo = (hci_read_remote_ver_info_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_REMOTE_VER_INFO");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  ConnectionHandle: 0x%02x", pReadRemoteVerInfo->con_handle);

	SendEventCommandStatus(HCI_CMD_READ_REMOTE_VER_INFO);
	SendEventReadRemoteVerInfo(pReadRemoteVerInfo->con_handle);
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandReadRemoteFeatures(u8* _Input)
{
	// command parameters
	hci_read_remote_features_cp* pReadRemoteFeatures = (hci_read_remote_features_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_READ_REMOTE_FEATURES");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  ConnectionHandle: 0x%04x", pReadRemoteFeatures->con_handle);

	SendEventCommandStatus(HCI_CMD_READ_REMOTE_FEATURES);
	SendEventReadRemoteFeatures(pReadRemoteFeatures->con_handle);
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWriteLinkPolicy(u8* _Input)
{
	// command parameters
	hci_write_link_policy_settings_cp* pLinkPolicy = (hci_write_link_policy_settings_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_WRITE_LINK_POLICY_SETTINGS");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  ConnectionHandle: 0x%04x", pLinkPolicy->con_handle);
	LOG(WII_IPC_WIIMOTE, "  Policy: 0x%04x", pLinkPolicy->settings);

	SendEventCommandStatus(HCI_CMD_WRITE_LINK_POLICY_SETTINGS);

	CWII_IPC_HLE_WiiMote* pWiimote = AccessWiiMote(pLinkPolicy->con_handle);
	if (pWiimote)
	{
		pWiimote->EventCommandWriteLinkPolicy();
	}	
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandAuthenticationRequested(u8* _Input)
{
	// command parameters
	hci_auth_req_cp* pAuthReq = (hci_auth_req_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_AUTH_REQ");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  ConnectionHandle: 0x%04x", pAuthReq->con_handle);

	SendEventCommandStatus(HCI_CMD_AUTH_REQ);
	SendEventAuthenticationCompleted(pAuthReq->con_handle);
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandSniffMode(u8* _Input)
{
	// command parameters
	hci_sniff_mode_cp* pSniffMode = (hci_sniff_mode_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_SNIFF_MODE");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  ConnectionHandle: 0x%04x", pSniffMode->con_handle);
	LOG(WII_IPC_WIIMOTE, "  max_interval: 0x%04x", pSniffMode->max_interval);
	LOG(WII_IPC_WIIMOTE, "  min_interval: 0x%04x", pSniffMode->min_interval);
	LOG(WII_IPC_WIIMOTE, "  attempt: 0x%04x", pSniffMode->attempt);
	LOG(WII_IPC_WIIMOTE, "  timeout: 0x%04x", pSniffMode->timeout);

	SendEventCommandStatus(HCI_CMD_SNIFF_MODE);
	SendEventModeChange(pSniffMode->con_handle, 0x02, pSniffMode->max_interval);  // 0x02 - sniff mode
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandDisconnect(u8* _Input)
{
	// command parameters
	hci_discon_cp* pDiscon = (hci_discon_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_DISCONNECT");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  ConnectionHandle: 0x%04x", pDiscon->con_handle);
	LOG(WII_IPC_WIIMOTE, "  Reason: 0x%02x", pDiscon->reason);

	SendEventCommandStatus(HCI_CMD_DISCONNECT);
	SendEventDisconnect(pDiscon->con_handle, pDiscon->reason);

	CWII_IPC_HLE_WiiMote* pWiimote = AccessWiiMote(pDiscon->con_handle);
	if (pWiimote)
	{
		pWiimote->EventDisconnect();
	}

	PanicAlert("disconnect");
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandWriteLinkSupervisionTimeout(u8* _Input)
{
	// command parameters
	hci_write_link_supervision_timeout_cp* pSuperVision = (hci_write_link_supervision_timeout_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_OCF_WRITE_LINK_SUPERVISION_TIMEOUT");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  con_handle: 0x%04x", pSuperVision->con_handle);
	LOG(WII_IPC_WIIMOTE, "  timeout: 0x%02x", pSuperVision->timeout);

	hci_write_link_supervision_timeout_rp Reply;
	Reply.status = 0x00;
	Reply.con_handle = pSuperVision->con_handle;

	SendEventCommandComplete(HCI_OCF_WRITE_LINK_SUPERVISION_TIMEOUT, &Reply, sizeof(hci_write_link_supervision_timeout_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandLinkKeyNegRep(u8* _Input)
{
	// command parameters
	hci_link_key_neg_rep_cp* pKeyNeg = (hci_link_key_neg_rep_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_LINK_KEY_NEG_REP");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pKeyNeg->bdaddr.b[0], pKeyNeg->bdaddr.b[1], pKeyNeg->bdaddr.b[2],
		pKeyNeg->bdaddr.b[3], pKeyNeg->bdaddr.b[4], pKeyNeg->bdaddr.b[5]);

	hci_link_key_neg_rep_rp Reply;
	Reply.status = 0x00;
	Reply.bdaddr = pKeyNeg->bdaddr;

	SendEventCommandComplete(HCI_OCF_WRITE_LINK_SUPERVISION_TIMEOUT, &Reply, sizeof(hci_link_key_neg_rep_rp));
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::CommandLinkKeyRep(u8* _Input)
{
	// command parameters
	hci_link_key_rep_cp* pKeyRep = (hci_link_key_rep_cp*)_Input;

	LOG(WII_IPC_WIIMOTE, "Command: HCI_CMD_LINK_KEY_REP");
	LOG(WII_IPC_WIIMOTE, "Input:");
	LOG(WII_IPC_WIIMOTE, "  bd: %02x:%02x:%02x:%02x:%02x:%02x", 
		pKeyRep->bdaddr.b[0], pKeyRep->bdaddr.b[1], pKeyRep->bdaddr.b[2],
		pKeyRep->bdaddr.b[3], pKeyRep->bdaddr.b[4], pKeyRep->bdaddr.b[5]);
	LOG_LinkKey(pKeyRep->key);


	hci_link_key_rep_rp Reply;
	Reply.status = 0x00;
	Reply.bdaddr = pKeyRep->bdaddr;

	SendEventCommandComplete(HCI_CMD_LINK_KEY_REP, &Reply, sizeof(hci_link_key_rep_rp));
}



////////////////////////////////////////////////////////////////////////////////////////////////////
//
//
// --- helper
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////

CWII_IPC_HLE_WiiMote* CWII_IPC_HLE_Device_usb_oh1_57e_305::AccessWiiMote(const bdaddr_t& _rAddr)
{
	for (size_t i=0; i<m_WiiMotes.size(); i++)
	{
		const bdaddr_t& BD = m_WiiMotes[i].GetBD();
		if ((_rAddr.b[0] == BD.b[0]) &&
			(_rAddr.b[1] == BD.b[1]) &&
			(_rAddr.b[2] == BD.b[2]) &&
			(_rAddr.b[3] == BD.b[3]) &&
			(_rAddr.b[4] == BD.b[4]) &&
			(_rAddr.b[5] == BD.b[5]))
			return &m_WiiMotes[i];
	}
	return NULL;
}

CWII_IPC_HLE_WiiMote* CWII_IPC_HLE_Device_usb_oh1_57e_305::AccessWiiMote(u16 _ConnectionHandle)
{
	for (size_t i=0; i<m_WiiMotes.size(); i++)
	{
		if (m_WiiMotes[i].GetConnectionHandle() == _ConnectionHandle)
			return &m_WiiMotes[i];
	}
	return NULL;
}

void CWII_IPC_HLE_Device_usb_oh1_57e_305::LOG_LinkKey(const u8* _pLinkKey)
{
	LOG(WII_IPC_WIIMOTE, "  link key: "
				 "0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
				 "0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
				 , _pLinkKey[0], _pLinkKey[1], _pLinkKey[2], _pLinkKey[3], _pLinkKey[4], _pLinkKey[5], _pLinkKey[6], _pLinkKey[7]
				 , _pLinkKey[8], _pLinkKey[9], _pLinkKey[10], _pLinkKey[11], _pLinkKey[12], _pLinkKey[13], _pLinkKey[14], _pLinkKey[15]);

}
