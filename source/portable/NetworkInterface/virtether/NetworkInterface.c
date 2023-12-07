/*
 * FreeRTOS+TCP V3.1.0
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/*****************************************************************************
 * Note: This file is Not! to be used as is. The purpose of this file is to provide
 * a template for writing a network interface. Each network interface will have to provide
 * concrete implementations of the functions in this file.
 *
 * See the following URL for an explanation of this file and its functions:
 * https://freertos.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/Embedded_Ethernet_Porting.html
 *
 *****************************************************************************/

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "list.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "dlog.h"

/* If ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES is set to 1, then the Ethernet
 * driver will filter incoming packets and only pass the stack those packets it
 * considers need processing. */
#if (ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0)
#define ipCONSIDER_FRAME_FOR_PROCESSING(pucEthernetBuffer) eProcessBuffer
#else
#define ipCONSIDER_FRAME_FOR_PROCESSING(pucEthernetBuffer) eConsiderFrameForProcessing((pucEthernetBuffer))
#endif

/* functions */
static BaseType_t pfInitialise(struct xNetworkInterface *pxDescriptor);
static BaseType_t pfOutput(struct xNetworkInterface *pxDescriptor,
                           NetworkBufferDescriptor_t *const pxNetworkBuffer,
                           BaseType_t xReleaseAfterSend);
static BaseType_t pfGetPhyLinkStatus(struct xNetworkInterface *pxDescriptor);
static void ether_task(void *param);

/* variables */
TaskHandle_t ether_handle;
NetworkBufferDescriptor_t g_desc;
ARPPacket_t g_arp;
static MACAddress_t connectedMac = {.ucBytes = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};

BaseType_t xNetworkInterfaceInitialise(void)
{
    d("");
    return pdTRUE;
}

BaseType_t xNetworkInterfaceOutput(NetworkBufferDescriptor_t *const pxNetworkBuffer,
                                   BaseType_t xReleaseAfterSend)
{
    if (pxNetworkBuffer)
    {
        d("b:%p as:%ld", pxNetworkBuffer->pucEthernetBuffer, xReleaseAfterSend);
    }
    else
    {
        d("");
    }
    return pdTRUE;
}

void vNetworkInterfaceAllocateRAMToBuffers(NetworkBufferDescriptor_t pxNetworkBuffers[ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS])
{
    (void)pxNetworkBuffers;
    /* FIX ME. */
    d("");
}

BaseType_t xGetPhyLinkStatus(struct xNetworkInterface *pxInterface)
{
    /* FIX ME. */
    d("%p", pxInterface);
    return pdTRUE;
}

BaseType_t xApplicationGetRandomNumber(uint32_t *pulNumber)
{
    d("");
    return *pulNumber + 1;
}
uint32_t ulApplicationGetNextSequenceNumber(uint32_t ulSourceAddress,
                                            uint16_t usSourcePort,
                                            uint32_t ulDestinationAddress,
                                            uint16_t usDestinationPort)
{
    (void)ulSourceAddress;
    (void)usSourcePort;
    (void)ulDestinationAddress;
    (void)usDestinationPort;
    uint32_t pulNumber = 0;

    d("");
    xApplicationGetRandomNumber(&pulNumber);
    return pulNumber;
}

struct xNetworkInterface *pxFillInterfaceDescriptor(BaseType_t xEMACIndex,
                                                    struct xNetworkInterface *pxInterface)
{
    NetworkEndPoint_t *ep;
    static char eth_name[0x11];

    snprintf(eth_name, sizeof(eth_name), "eth%ld", xEMACIndex);
    d("mac:%lx", (uint32_t)xEMACIndex);

    xTaskCreate(ether_task, "ether_task", 100, NULL, configMAX_PRIORITIES - 1, &ether_handle);
    ep = FreeRTOS_FirstEndPoint(pxInterface);
    while (ep)
    {
        d("epmac:%s", b2s(ep->xMACAddress.ucBytes, 6));
        ep = FreeRTOS_NextEndPoint(pxInterface, ep);
    }
    pxInterface->pcName = eth_name;
    pxInterface->pvArgument = (void *)xEMACIndex;
    pxInterface->pfInitialise = pfInitialise;
    pxInterface->pfOutput = pfOutput;
    pxInterface->pfGetPhyLinkStatus = pfGetPhyLinkStatus;
    FreeRTOS_AddNetworkInterface(pxInterface);
    return pxInterface;
}

static BaseType_t pfInitialise(struct xNetworkInterface *pxDescriptor)
{
    (void)pxDescriptor;
    return pdPASS;
}

static BaseType_t pfOutput(struct xNetworkInterface *pxDescriptor,
                           NetworkBufferDescriptor_t *const pxNetworkBuffer,
                           BaseType_t xReleaseAfterSend)
{
    ARPPacket_t *arp;
    (void)xReleaseAfterSend;
    arp = (ARPPacket_t *)pxNetworkBuffer->pucEthernetBuffer;
    if (arp->xARPHeader.usOperation == ipARP_REQUEST)
    {
        memcpy(&g_desc, pxDescriptor, sizeof(NetworkBufferDescriptor_t));
        memcpy(&g_arp, arp, sizeof(ARPPacket_t));
        vTaskResume(ether_handle);
    }
    d("ip:%lx po:%x bp:%x len:%u data:%s r:%ld", pxNetworkBuffer->xIPAddress.ulIP_IPv4, pxNetworkBuffer->usPort, pxNetworkBuffer->usBoundPort, pxNetworkBuffer->xDataLength, b2s(pxNetworkBuffer->pucEthernetBuffer, pxNetworkBuffer->xDataLength), xReleaseAfterSend);
    return pdPASS;
}

static BaseType_t pfGetPhyLinkStatus(struct xNetworkInterface *pxDescriptor)
{
    (void)pxDescriptor;
    d("");
    return pdPASS;
}

static void ether_task(void *param)
{
    static NetworkBufferDescriptor_t *descriptor;
    ARPPacket_t *arp;
    ARPHeader_t *ah;
    EthernetHeader_t *eth;
    IPStackEvent_t ev;
    MACAddress_t mac;
    uint32_t from, addr;

    (void)param;
    d("");
    for (;;)
    {
        vTaskSuspend(NULL);
        descriptor = pxGetNetworkBufferWithDescriptor(sizeof(ARPPacket_t), 0);
        if (descriptor)
        {
            ev.eEventType = eNetworkRxEvent;
            memcpy(descriptor, &g_desc, sizeof(NetworkBufferDescriptor_t));
            descriptor->pucEthernetBuffer = (uint8_t *)&g_arp;
            ev.pvData = descriptor;
            arp = (ARPPacket_t *)descriptor->pucEthernetBuffer;
            ah = &arp->xARPHeader;
            eth = &arp->xEthernetHeader;
            if (ah->usOperation == ipARP_REQUEST)
            {
                mac = ah->xSenderHardwareAddress;
                from = *(uint32_t *)ah->ucSenderProtocolAddress;
                addr = ah->ulTargetProtocolAddress;

                ah->usOperation = ipARP_REPLY;
                ah->xSenderHardwareAddress = connectedMac;
                memcpy(ah->ucSenderProtocolAddress, &addr, sizeof(addr));
                ah->ulTargetProtocolAddress = from;
                ah->xTargetHardwareAddress = mac;

                eth->xDestinationAddress = mac;
                eth->xSourceAddress = connectedMac;

                d("send %s", b2s(arp, sizeof(ARPPacket_t)));
                xSendEventStructToIPTask(&ev, 0);
            }
            else
            {
                d("arp op:%d", arp->xARPHeader.usOperation);
            }

            descriptor = NULL;
        }
        else
        {
            d("descriptor is null");
        }
    }
}