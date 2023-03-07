// Copyright 2023 Blues Inc. All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Firmware for a panic button.

#include "panic.h"

#include <framework.h>
#include <note.h>

#define STATE_SEND_PANIC 0

static void panicPoll(int appID, int state, void *appCtx);
static void panicISR(int appID, uint16_t pins, void *appCtx);
static void sendPanic(void);

// The Sparrow reference node exposes GPIO pins A1, A2, and A3. This app uses A2
// to send a "panic" alert to Notehub.

// A2 corresponds to pin PA10 on the node's STM32 MCU.
#define PANIC_PORT   A2_GPIO_Port
#define PANIC_PIN    A2_Pin
#define PANIC_IRQN   EXTI15_10_IRQn

// This app is interrupt driven, so we really don't ever need to activate. Set
// the activation interval to a large value (1 day).
#define ACTIVATION_PERIOD_SECS (60 * 60 * 24)
#define POLL_PERIOD_SECS 15

typedef struct {
    // The sending flag is set in an ISR, so it needs to be marked volatile.
    volatile bool sending;
} AppState;

static AppState appState = {0};

bool panicInit()
{
    schedAppConfig config = {
        .name = "panic_button",
        .activationPeriodSecs = ACTIVATION_PERIOD_SECS,
        .pollPeriodSecs = POLL_PERIOD_SECS,
        .activateFn = NULL,
        .interruptFn = panicISR,
        .pollFn = panicPoll,
        .responseFn = NULL,
        .appContext = &appState,
    };
    if (schedRegisterApp(&config) < 0) {
        return false;
    }

    // The panic button is configured to trigger an interrupt on a falling edge,
    // when it's pressed and the connection to the corresponding GPIO pin is
    // driven to GND. The GPIO has an internal pull-up so that when the button
    // isn't pressed, the logic level is high.
    HAL_GPIO_DeInit(PANIC_PORT, PANIC_PIN);
    GPIO_InitTypeDef gpioInit = {0};
    gpioInit.Pin = PANIC_PIN;
    gpioInit.Mode = GPIO_MODE_IT_FALLING;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(PANIC_PORT, &gpioInit);
    HAL_NVIC_SetPriority(PANIC_IRQN, 15, 0);
    HAL_NVIC_EnableIRQ(PANIC_IRQN);

    return true;
}

static void panicPoll(int appID, int state, void *appCtx)
{
    AppState *appState = (AppState *)appCtx;

    switch (state) {
        case STATE_ACTIVATED:
            schedSetState(appID, STATE_DEACTIVATED, "Nothing to do.");
            break;
        case STATE_SEND_PANIC:
            atpMaximizePowerLevel();
            ledIndicateAck(1);
            sendPanic();
            // Clear the sending flag so that we can process subsequent button
            // presses.
            appState->sending = false;
            schedSetCompletionState(appID, STATE_DEACTIVATED,
                STATE_DEACTIVATED);
            break;
    }

}

static void panicISR(int appID, uint16_t pins, void *appCtx)
{
    AppState *appState = (AppState *)appCtx;

    if (appState->sending) {
        // Still processing a prior button press.
        return;
    }

    if ((pins & PANIC_PIN) != 0) {
        appState->sending = true;
        schedActivateNowFromISR(appID, true, STATE_SEND_PANIC);
    }
}

static void sendPanic(void)
{
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        return;
    }
    JAddStringToObject(req, "file", "*#panic.qo");
    JAddBoolToObject(req, "sync", true);

    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        return;
    }
    JAddBoolToObject(body, "panic", true);
    JAddItemToObject(req, "body", body);

    APP_PRINTF("panic: sendPanic: Sent to gateway.\r\n");

    noteSendToGatewayAsync(req, false);
}
