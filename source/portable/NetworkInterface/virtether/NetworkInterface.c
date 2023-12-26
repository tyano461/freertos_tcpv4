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
#include <stdbool.h>

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
#define TX_SIZE (uint32_t *)0x28000004
#define RX_SIZE (uint32_t *)0x28100004
#define RX_DATA_BASE (uint32_t *)0x28100010

/* functions */
static BaseType_t pfInitialise(struct xNetworkInterface *pxDescriptor);
static BaseType_t pfOutput(struct xNetworkInterface *pxDescriptor,
                           NetworkBufferDescriptor_t *const pxNetworkBuffer,
                           BaseType_t xReleaseAfterSend);
static BaseType_t pfGetPhyLinkStatus(struct xNetworkInterface *pxDescriptor);
static void ether_task(void *param);
static bool is_arp_request(uint8_t *data, size_t len);
static void arp_reply(uint8_t *data, size_t len);
static void mmio_write_on(void);
static void mmio_write_off(void);
static void mmio_write(uint32_t offset, uint8_t *data, uint32_t len);

/* variables */
TaskHandle_t ether_handle;
static volatile uint32_t rx_size = 0;
static uint8_t rx_buf[0x100000];
static uint8_t reply_buf[0x30];

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

uint32_t random_magic = 0x01010101;
uint32_t random_count = 0;
BaseType_t xApplicationGetRandomNumber(uint32_t *pulNumber)
{
    d("");

    *pulNumber = random_magic << (random_count++ % 8);
    return pdTRUE;
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

    xTaskCreate(ether_task, "ether_task", 100, NULL, tskIDLE_PRIORITY + 2, &ether_handle);
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
    static TaskHandle_t handle = NULL;

    (void)pxDescriptor;
    if (!handle)
    {
        *(uint32_t *)0x280E0000 = (uint32_t)ether_handle;
    }
    handle = (TaskHandle_t) * (uint32_t *)0x280E0000;

    mmio_write_on();
    *TX_SIZE = pxNetworkBuffer->xDataLength;
    mmio_write(0, (uint8_t *)pxNetworkBuffer->pucEthernetBuffer, pxNetworkBuffer->xDataLength);
    mmio_write_off();

    d("ip:%lx po:%x bp:%x len:%u r:%ld data:\n%s",
      pxNetworkBuffer->xIPAddress.ulIP_IPv4,
      pxNetworkBuffer->usPort,
      pxNetworkBuffer->usBoundPort,
      pxNetworkBuffer->xDataLength,
      xReleaseAfterSend,
      b2s(pxNetworkBuffer->pucEthernetBuffer, pxNetworkBuffer->xDataLength));
    if (xReleaseAfterSend)
    {
        vReleaseNetworkBufferAndDescriptor(pxNetworkBuffer);
    }

    return pdPASS;
}

static BaseType_t pfGetPhyLinkStatus(struct xNetworkInterface *pxDescriptor)
{
    (void)pxDescriptor;
    d("");
    return pdPASS;
}

SemaphoreHandle_t rx_ether_smph = NULL;

static void ether_task(void *param)
{
    static NetworkBufferDescriptor_t *descriptor;
    IPStackEvent_t ev;
    BaseType_t status;
    volatile uint32_t size;
    eFrameProcessingResult_t consider;

    (void)param;

    rx_ether_smph = xSemaphoreCreateBinary();
    d("smph:%p off:%x", rx_ether_smph, (uint8_t *)&descriptor->pucEthernetBuffer - (uint8_t *)descriptor);
    for (;;)
    {
        if (xSemaphoreTake(rx_ether_smph, portMAX_DELAY))
        {
            size = rx_size;
            d("be waken sz:%lu", size);
            if (!size)
                continue;

//            if (is_arp_request(rx_buf, size))
//            {
//                arp_reply(rx_buf, size);
//            }

            descriptor = pxGetNetworkBufferWithDescriptor(size, 0);
            if (descriptor)
            {
                descriptor->pucEthernetBuffer = (uint8_t *)rx_buf;
                descriptor->xDataLength = size;
                consider = eConsiderFrameForProcessing(descriptor->pucEthernetBuffer);
                if (consider == eProcessBuffer)
                {
                    ev.pvData = descriptor;
                    ev.eEventType = eNetworkRxEvent;
                    status = xSendEventStructToIPTask(&ev, 0);
                    rx_size = 0;
                    d("rx data to stack:%ld sz:%lu data:\n%s", status, size, b2s(rx_buf, size));
                }
                else
                {
                    d("consider:%d", consider);
                }
            }
            else
            {
                d("descriptor is null");
            }
            vReleaseNetworkBufferAndDescriptor(descriptor);
        }
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static bool is_arp_request(uint8_t *data, size_t len)
{
    (void)len;
    EthernetHeader_t *eth = (EthernetHeader_t *)data;
    ARPHeader_t *arp = (ARPHeader_t *)&eth[1];

    return eth->usFrameType == 0x0608 && arp->usOperation == 0x0100;
}

static void arp_reply(uint8_t *data, size_t len)
{
    EthernetHeader_t *eth = (EthernetHeader_t *)data;
    ARPHeader_t *arp = (ARPHeader_t *)&eth[1];
    MACAddress_t mac;
    IP_Address_t ip;
    MACAddress_t tomac = eth->xSourceAddress;
    IP_Address_t toip = *(IP_Address_t *)arp->ucSenderProtocolAddress;

    NetworkEndPoint_t *ep = FreeRTOS_FirstEndPoint(NULL);
    if (ep)
    {
        ip = *(IP_Address_t *)&ep->ipv4_defaults.ulIPAddress;
        mac = ep->xMACAddress;
        eth = (EthernetHeader_t *)reply_buf;
        arp = (ARPHeader_t *)&eth[1];

        eth->xDestinationAddress = tomac;
        eth->xSourceAddress = mac;
        arp->usOperation = 0x0200;
        arp->xSenderHardwareAddress = mac;
        *(IP_Address_t *)arp->ucSenderProtocolAddress = ip;
        arp->xTargetHardwareAddress = tomac;
        *(IP_Address_t*)&arp->ulTargetProtocolAddress = toip;
        mmio_write_on();
        *TX_SIZE = len;
        mmio_write(0, (uint8_t *)reply_buf, len);
        mmio_write_off();
    }
}
#pragma GCC diagnostic pop

/* MMIO */
#define WRITE_MMIO_REG 0x28000000
#define WRITE_DATA_BASE (uint8_t *)0x28000010
#define write_set(s) *(uint32_t *)WRITE_MMIO_REG = s

static void mmio_write_on(void)
{
    write_set(1);
}

static void mmio_write_off(void)
{
    write_set(0);
}

static void mmio_write(uint32_t offset, uint8_t *data, uint32_t len)
{
    memcpy(WRITE_DATA_BASE + offset, data, len);
}

void ether_rx_handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    rx_size = *RX_SIZE;
    if (rx_ether_smph)
    {
        memcpy(rx_buf, RX_DATA_BASE, rx_size);
        // d("rx smph:%p size:%lu data:\n%s", rx_ether_smph, rx_size, b2s(rx_buf, rx_size));
        xSemaphoreGiveFromISR(rx_ether_smph, &xHigherPriorityTaskWoken);
        d("waken sz:%lu", rx_size);
    }
    else
    {
        d("smph is null");
    }
}