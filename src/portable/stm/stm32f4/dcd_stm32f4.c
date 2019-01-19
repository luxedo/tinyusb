/**************************************************************************/
/*!
    @file     dcd_nrf5x.c
    @author   hathach

    @section LICENSE

    Software License Agreement (BSD License)

    Copyright (c) 2018, Scott Shawcroft for Adafruit Industries
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holders nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    This file is part of the tinyusb stack.
*/
/**************************************************************************/

#include "tusb_option.h"

#if TUSB_OPT_DEVICE_ENABLED && CFG_TUSB_MCU == OPT_MCU_STM32F4

#include "device/dcd.h"
#include "stm32f4xx.h"

/*------------------------------------------------------------------*/
/* MACRO TYPEDEF CONSTANT ENUM
 *------------------------------------------------------------------*/
#define DEVICE_BASE (USB_OTG_DeviceTypeDef *) (USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE)
#define OUT_EP_BASE (USB_OTG_OUTEndpointTypeDef *) (USB_OTG_FS_PERIPH_BASE + USB_OTG_OUT_ENDPOINT_BASE)
#define IN_EP_BASE (USB_OTG_INEndpointTypeDef *) (USB_OTG_FS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE)


//static ATTR_ALIGNED(4) uint8_t _setup_packet[8];

// Setup the control endpoint 0.
static void bus_reset(void) {
  USB_OTG_DeviceTypeDef * dev = DEVICE_BASE;
  USB_OTG_OUTEndpointTypeDef * out_ep = OUT_EP_BASE;
  // USB_OTG_INEndpointTypeDef * in_ep = IN_EP_BASE;

  for(int n = 0; n < 4; n++) {
    out_ep[n].DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
  }

  dev->DAINTMSK |= (1 << USB_OTG_DAINTMSK_OEPM_Pos) | (1 << USB_OTG_DAINTMSK_IEPM_Pos);
  dev->DOEPMSK |= USB_OTG_DOEPMSK_STUPM | USB_OTG_DOEPMSK_XFRCM;
  dev->DIEPMSK |= USB_OTG_DIEPMSK_TOM | USB_OTG_DIEPMSK_XFRCM;

  // FIFO sizes are set up by the following rules:
  // OUT FIFO uses:
  // * 10 locations in hardware for setup packets + setup control words
  // (up to 3 setup packets).
  // * 2 locations for OUT endpoint control words.
  // * 64 bytes for maximum control packet size.
  // IN FIFO uses 64 words for maximum control packet size.
  USB_OTG_FS->GRXFSIZ = 19; // 10 + 2 + 64 = 19 32-bit words
  USB_OTG_FS->DIEPTXF0_HNPTXFSIZ = 16; // 16 32-bit words = 64 bytes

  out_ep[0].DOEPTSIZ |= (3 << USB_OTG_DOEPTSIZ_STUPCNT_Pos);
}

static void end_of_reset(void) {
  USB_OTG_DeviceTypeDef * dev = DEVICE_BASE;
  USB_OTG_INEndpointTypeDef * in_ep = IN_EP_BASE;
  // On current silicon on the Full Speed core, speed is fixed to Full Speed.
  // However, keep for debugging and in case Low Speed is ever supported.
  uint32_t enum_spd = (dev->DSTS & USB_OTG_DSTS_ENUMSPD_Msk) >> USB_OTG_DSTS_ENUMSPD_Pos;
  in_ep[0].DIEPCTL |= enum_spd;
}


/*------------------------------------------------------------------*/
/* Controller API
 *------------------------------------------------------------------*/
bool dcd_init (uint8_t rhport)
{
  (void) rhport;

  // Programming model begins on page 1336 of Rev 17 of reference manual.
  USB_OTG_FS->GAHBCFG |= USB_OTG_GAHBCFG_TXFELVL | USB_OTG_GAHBCFG_GINT;

  // No HNP/SRP (no OTG support), program timeout later, turnaround
  // programmed for 18 MHz.
  USB_OTG_FS->GUSBCFG |= (0x0C << USB_OTG_GUSBCFG_TRDT_Pos);

  // Clear all used interrupts
  USB_OTG_FS->GINTSTS |= USB_OTG_GINTSTS_OTGINT | USB_OTG_GINTSTS_MMIS | \
    USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE | \
    USB_OTG_GINTSTS_ESUSP | USB_OTG_GINTSTS_USBSUSP | USB_OTG_GINTSTS_SOF;

  // Required as part of core initialization.
  USB_OTG_FS->GINTMSK |= USB_OTG_GINTMSK_OTGINT | USB_OTG_GINTMSK_MMISM;

  USB_OTG_DeviceTypeDef * dev = ((USB_OTG_DeviceTypeDef *) (USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE));

  // If USB host misbehaves during status portion of control xfer
  // (non zero-length packet), send STALL back and discard. Full speed.
  dev->DCFG |=  USB_OTG_DCFG_NZLSOHSK | (3 << USB_OTG_DCFG_DSPD_Pos);
  /* USB_OTG_FS->GINTMSK |= USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_ENUMDNEM | \
    USB_OTG_GINTMSK_ESUSPM | USB_OTG_GINTMSK_USBSUSPM | \
    USB_OTG_GINTMSK_SOFM; */
  USB_OTG_FS->GINTMSK |= USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_ENUMDNEM;

  // Enable pullup, enable peripheral.
  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_VBUSBSEN | USB_OTG_GCCFG_PWRDWN;

  return true;
}

void dcd_int_enable (uint8_t rhport)
{
  (void) rhport;
  NVIC_EnableIRQ(OTG_FS_IRQn);
}

void dcd_int_disable (uint8_t rhport)
{
  (void) rhport;
  NVIC_DisableIRQ(OTG_FS_IRQn);
}

void dcd_connect (uint8_t rhport)
{

}

void dcd_disconnect (uint8_t rhport)
{

}

void dcd_set_address (uint8_t rhport, uint8_t dev_addr)
{
  (void) rhport;
}

void dcd_set_config (uint8_t rhport, uint8_t config_num)
{
  (void) rhport;
  (void) config_num;
  // Nothing to do
}

/*------------------------------------------------------------------*/
/* DCD Endpoint port
 *------------------------------------------------------------------*/

bool dcd_edpt_open (uint8_t rhport, tusb_desc_endpoint_t const * desc_edpt)
{
  (void) rhport;

  // uint8_t const epnum = edpt_number(desc_edpt->bEndpointAddress);
  // uint8_t const dir   = edpt_dir(desc_edpt->bEndpointAddress);
  //
  // UsbDeviceDescBank* bank = &sram_registers[epnum][dir];
  // uint32_t size_value = 0;
  // while (size_value < 7) {
  //   if (1 << (size_value + 3) == desc_edpt->wMaxPacketSize.size) {
  //     break;
  //   }
  //   size_value++;
  // }
  //
  // // unsupported endpoint size
  // if ( size_value == 7 && desc_edpt->wMaxPacketSize.size != 1023 ) return false;
  //
  // bank->PCKSIZE.bit.SIZE = size_value;
  //
  // UsbDeviceEndpoint* ep = &USB->DEVICE.DeviceEndpoint[epnum];
  //
  // if ( dir == TUSB_DIR_OUT )
  // {
  //   ep->EPCFG.bit.EPTYPE0 = desc_edpt->bmAttributes.xfer + 1;
  //   ep->EPINTENSET.bit.TRCPT0 = true;
  // }else
  // {
  //   ep->EPCFG.bit.EPTYPE1 = desc_edpt->bmAttributes.xfer + 1;
  //   ep->EPINTENSET.bit.TRCPT1 = true;
  // }

  // return true;
  return false;
}

bool dcd_edpt_xfer (uint8_t rhport, uint8_t ep_addr, uint8_t * buffer, uint16_t total_bytes)
{
  (void) rhport;

  // uint8_t const epnum = edpt_number(ep_addr);
  // uint8_t const dir   = edpt_dir(ep_addr);
  //
  // UsbDeviceDescBank* bank = &sram_registers[epnum][dir];
  // UsbDeviceEndpoint* ep = &USB->DEVICE.DeviceEndpoint[epnum];
  //
  // // A setup token can occur immediately after an OUT STATUS packet so make sure we have a valid
  // // buffer for the control endpoint.
  // if (epnum == 0 && dir == 0 && buffer == NULL) {
  //   buffer = _setup_packet;
  // }
  //
  // bank->ADDR.reg = (uint32_t) buffer;
  // if ( dir == TUSB_DIR_OUT )
  // {
  //   bank->PCKSIZE.bit.MULTI_PACKET_SIZE = total_bytes;
  //   bank->PCKSIZE.bit.BYTE_COUNT = 0;
  //   ep->EPSTATUSCLR.reg |= USB_DEVICE_EPSTATUSCLR_BK0RDY;
  //   ep->EPINTFLAG.reg |= USB_DEVICE_EPINTFLAG_TRFAIL0;
  // } else
  // {
  //   bank->PCKSIZE.bit.MULTI_PACKET_SIZE = 0;
  //   bank->PCKSIZE.bit.BYTE_COUNT = total_bytes;
  //   ep->EPSTATUSSET.reg |= USB_DEVICE_EPSTATUSSET_BK1RDY;
  //   ep->EPINTFLAG.reg |= USB_DEVICE_EPINTFLAG_TRFAIL1;
  // }

  // return true;
  return false;
}

bool dcd_edpt_stalled (uint8_t rhport, uint8_t ep_addr)
{
  (void) rhport;

  // control is never got halted
  if ( ep_addr == 0 ) {
      return false;
  }

  // uint8_t const epnum = edpt_number(ep_addr);
  // UsbDeviceEndpoint* ep = &USB->DEVICE.DeviceEndpoint[epnum];
  // return (edpt_dir(ep_addr) == TUSB_DIR_IN ) ? ep->EPINTFLAG.bit.STALL1 : ep->EPINTFLAG.bit.STALL0;
  return true;
}

void dcd_edpt_stall (uint8_t rhport, uint8_t ep_addr)
{
  (void) rhport;

  // uint8_t const epnum = edpt_number(ep_addr);
  // UsbDeviceEndpoint* ep = &USB->DEVICE.DeviceEndpoint[epnum];
  //
  // if (edpt_dir(ep_addr) == TUSB_DIR_IN) {
  //     ep->EPSTATUSSET.reg = USB_DEVICE_EPSTATUSSET_STALLRQ1;
  // } else {
  //     ep->EPSTATUSSET.reg = USB_DEVICE_EPSTATUSSET_STALLRQ0;
  //
  //     // for control, stall both IN & OUT
  //     if (ep_addr == 0) {
  //       ep->EPSTATUSSET.reg = USB_DEVICE_EPSTATUSSET_STALLRQ1;
  //     }
  // }
}

void dcd_edpt_clear_stall (uint8_t rhport, uint8_t ep_addr)
{
  (void) rhport;

  // uint8_t const epnum = edpt_number(ep_addr);
  // UsbDeviceEndpoint* ep = &USB->DEVICE.DeviceEndpoint[epnum];
  //
  // if (edpt_dir(ep_addr) == TUSB_DIR_IN) {
  //   ep->EPSTATUSCLR.reg = USB_DEVICE_EPSTATUSCLR_STALLRQ1;
  // } else {
  //   ep->EPSTATUSCLR.reg = USB_DEVICE_EPSTATUSCLR_STALLRQ0;
  // }
}

bool dcd_edpt_busy (uint8_t rhport, uint8_t ep_addr)
{
  (void) rhport;

  // // USBD shouldn't check control endpoint state
  // if ( 0 == ep_addr ) return false;
  //
  // uint8_t const epnum = edpt_number(ep_addr);
  // UsbDeviceEndpoint* ep = &USB->DEVICE.DeviceEndpoint[epnum];
  //
  // if (edpt_dir(ep_addr) == TUSB_DIR_IN) {
  //   return ep->EPINTFLAG.bit.TRCPT1 == 0 && ep->EPSTATUS.bit.BK1RDY == 1;
  // }
  // return ep->EPINTFLAG.bit.TRCPT0 == 0 && ep->EPSTATUS.bit.BK0RDY == 1;
  return true;
}

/*------------------------------------------------------------------*/

// static bool maybe_handle_setup_packet(void) {
    // if (USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.bit.RXSTP)
    // {
    //     USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_RXSTP;
    //
    //     // This copies the data elsewhere so we can reuse the buffer.
    //     dcd_event_setup_received(0, (uint8_t*) sram_registers[0][0].ADDR.reg, true);
    //     return true;
    // }
    // return false;
// }
/*
 *------------------------------------------------------------------*/
/* USB_EORSM_DNRSM, USB_EORST_RST, USB_LPMSUSP_DDISC, USB_LPM_DCONN,
USB_MSOF, USB_RAMACER, USB_RXSTP_TXSTP_0, USB_RXSTP_TXSTP_1,
USB_RXSTP_TXSTP_2, USB_RXSTP_TXSTP_3, USB_RXSTP_TXSTP_4,
USB_RXSTP_TXSTP_5, USB_RXSTP_TXSTP_6, USB_RXSTP_TXSTP_7,
USB_STALL0_STALL_0, USB_STALL0_STALL_1, USB_STALL0_STALL_2,
USB_STALL0_STALL_3, USB_STALL0_STALL_4, USB_STALL0_STALL_5,
USB_STALL0_STALL_6, USB_STALL0_STALL_7, USB_STALL1_0, USB_STALL1_1,
USB_STALL1_2, USB_STALL1_3, USB_STALL1_4, USB_STALL1_5, USB_STALL1_6,
USB_STALL1_7, USB_SUSPEND, USB_TRFAIL0_TRFAIL_0, USB_TRFAIL0_TRFAIL_1,
USB_TRFAIL0_TRFAIL_2, USB_TRFAIL0_TRFAIL_3, USB_TRFAIL0_TRFAIL_4,
USB_TRFAIL0_TRFAIL_5, USB_TRFAIL0_TRFAIL_6, USB_TRFAIL0_TRFAIL_7,
USB_TRFAIL1_PERR_0, USB_TRFAIL1_PERR_1, USB_TRFAIL1_PERR_2,
USB_TRFAIL1_PERR_3, USB_TRFAIL1_PERR_4, USB_TRFAIL1_PERR_5,
USB_TRFAIL1_PERR_6, USB_TRFAIL1_PERR_7, USB_UPRSM, USB_WAKEUP */
void OTG_FS_IRQHandler(void) {
  uint32_t int_status = USB_OTG_FS->GINTSTS;

  if(int_status & USB_OTG_GINTSTS_USBRST) {
    // USBRST is start of reset.
    USB_OTG_FS->GINTSTS = USB_OTG_GINTSTS_USBRST;
    bus_reset();
  }

  if(int_status & USB_OTG_GINTSTS_ENUMDNE) {
    // ENUMDNE detects speed of the link. For full-speed, we
    // always expect the same value. This interrupt is considered
    // the end of reset.
    USB_OTG_FS->GINTSTS = USB_OTG_GINTSTS_ENUMDNE;
    end_of_reset();
    dcd_event_bus_signal(0, DCD_EVENT_BUS_RESET, true);
  }


  // uint32_t int_status = USB->DEVICE.INTFLAG.reg;
  //
  // /*------------- Interrupt Processing -------------*/
  // if ( int_status & USB_DEVICE_INTFLAG_EORST )
  // {
  //   USB->DEVICE.INTFLAG.reg = USB_DEVICE_INTENCLR_EORST;
  //   bus_reset();
  //   dcd_event_bus_signal(0, DCD_EVENT_BUS_RESET, true);
  // }
  //
  // // Setup packet received.
  // maybe_handle_setup_packet();
  //
  //   USB->DEVICE.INTFLAG.reg = USB_DEVICE_INTFLAG_SOF;
  //   dcd_event_bus_signal(0, DCD_EVENT_SOF, true);
  //   uint32_t epints = USB->DEVICE.EPINTSMRY.reg;
  //   for (uint8_t epnum = 0; epnum < USB_EPT_NUM; epnum++) {
  //       if ((epints & (1 << epnum)) == 0) {
  //           continue;
  //       }
  //
  //       if (direction == TUSB_DIR_OUT && maybe_handle_setup_packet()) {
  //           continue;
  //       }
  //       UsbDeviceEndpoint* ep = &USB->DEVICE.DeviceEndpoint[epnum];
  //
  //       UsbDeviceDescBank* bank = &sram_registers[epnum][direction];
  //       uint16_t total_transfer_size = bank->PCKSIZE.bit.BYTE_COUNT;
  //
  //       uint8_t ep_addr = epnum;
  //       if (direction == TUSB_DIR_IN) {
  //           ep_addr |= TUSB_DIR_IN_MASK;
  //       }
  //       dcd_event_xfer_complete(0, ep_addr, total_transfer_size, XFER_RESULT_SUCCESS, true);
  //
  //       // just finished status stage (total size = 0), prepare for next setup packet
  //       if (epnum == 0 && total_transfer_size == 0) {
  //           dcd_edpt_xfer(0, 0, _setup_packet, sizeof(_setup_packet));
  //       }
  //
  //       if (direction == TUSB_DIR_IN) {
  //           ep->EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT1;
  //       } else {
  //           ep->EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT0;
  //       }
  //   }
}

#endif
