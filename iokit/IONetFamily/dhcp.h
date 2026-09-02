/*
 * dhcp.h - minimal DHCP client (DISCOVER/OFFER/REQUEST/ACK).
 */
#pragma once
#include "ionet.h"

void dhcp_init(void);           /* bind UDP port 68 */
void dhcp_start(void);          /* kick the state machine */
void dhcp_poll(void);           /* called from ionet_poll() */
int  dhcp_done(void);           /* lease acquired */
