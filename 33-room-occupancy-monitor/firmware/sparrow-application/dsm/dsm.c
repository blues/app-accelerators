// Copyright 2023 Blues Inc. All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Firmware for a door state monitor ("dsm"). This app sends a note to Notehub
// when a door goes from open to closed and vice-versa.

#include "dsm.h"

#include <framework.h>
#include <note.h>

#define STATE_DOOR_TRANSITION 0

#define POLL_PERIOD_SECS 15
#define ACTIVATION_PERIOD_SECS (60 * 30)

static void dsmPoll(int appID, int state, void *appCtx);
static void dsmISR(int appID, uint16_t pins, void *appCtx);
static void sendNotification(int appID, bool open);

// This app uses the A1 pin on the Sparrow reference node to monitor the state
// of the door. A1 is wired to pin PB2 of the node's STM32 MCU.
#define DOOR_1_PORT  A1_GPIO_Port
#define DOOR_1_PIN   A1_Pin
#define DOOR_1_IRQN  EXTI2_IRQn

bool dsmInit()
{
    schedAppConfig config = {
        .name = "dsm",
        .activationPeriodSecs = ACTIVATION_PERIOD_SECS,
        .pollPeriodSecs = POLL_PERIOD_SECS,
        .activateFn = NULL,
        .interruptFn = dsmISR,
        .pollFn = dsmPoll,
        .responseFn = NULL,
        .appContext = NULL,
    };
    if (schedRegisterApp(&config) < 0) {
        return false;
    }

    HAL_GPIO_DeInit(DOOR_1_PORT, DOOR_1_PIN);
    GPIO_InitTypeDef gpioInit = {0};
    gpioInit.Pin = DOOR_1_PIN;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    // When the switch is open (i.e. the door is open), the GPIO will be pulled
    // high. So, a high logic level on this pin means the door is open. When
    // the switch is closed (i.e. the door is closed), the GPIO will be driven
    // to GND. Thus, a low logic level means the door is closed.
    gpioInit.Pull = GPIO_PULLUP;
    // Kick off the app's notification logic any time the level of the
    // monitoring pin changes.
    gpioInit.Mode = GPIO_MODE_IT_RISING_FALLING;
    HAL_GPIO_Init(DOOR_1_PORT, &gpioInit);
    HAL_NVIC_SetPriority(DOOR_1_IRQN, 15, 0);
    HAL_NVIC_EnableIRQ(DOOR_1_IRQN);

    return true;
}

static void dsmPoll(int appID, int state, void *appCtx)
{
    bool doorOpen = HAL_GPIO_ReadPin(DOOR_1_PORT, DOOR_1_PIN);

    (void)appCtx;

    if (doorOpen) {
        APP_PRINTF("dsm: Door open.\r\n");
    }
    else {
        APP_PRINTF("dsm: Door closed.\r\n");
    }

    switch (state) {
        case STATE_ACTIVATED:
            // Send a heartbeat notification every ACTIVATION_PERIOD_SECS
            // seconds.
            APP_PRINTF("dsm: Sending door state heartbeat.\r\n");
            sendNotification(appID, doorOpen);
            break;
        case STATE_DOOR_TRANSITION:
            sendNotification(appID, doorOpen);
            break;
    }

}

static void dsmISR(int appID, uint16_t pins, void *appCtx)
{
    (void)pins;
    (void)appCtx;

    // On a door state change (open -> closed, or vice-versa), send a
    // notification.
    schedActivateNowFromISR(appID, true, STATE_DOOR_TRANSITION);
}

static void sendNotification(int appID, bool open)
{
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        return;
    }
    JAddStringToObject(req, "file", "*#state.qo");

    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        return;
    }
    JAddBoolToObject(body, "open", open);
    JAddItemToObject(req, "body", body);
    JAddBoolToObject(req, "sync", true);

    atpMaximizePowerLevel();
    ledIndicateAck(1);
    noteSendToGatewayAsync(req, false);

    APP_PRINTF("dsm: Sent notification.\r\n");

    schedSetCompletionState(appID, STATE_DEACTIVATED, STATE_DEACTIVATED);
}
